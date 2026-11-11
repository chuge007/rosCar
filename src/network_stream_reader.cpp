#include "network_stream_reader.h"

#include "robot_app_logger.h"
#include "utf8_compat.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSize>
#include <QSysInfo>
#include <QStringList>
#include <QTcpSocket>
#include <QUrl>

#include <opencv2/core.hpp>
#include <opencv2/core/version.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>

#include <cstdlib>
#include <string>
#include <vector>

namespace crawling {

namespace {

constexpr int kOpenTimeoutMs = 5000;
constexpr int kReadTimeoutMs = 3000;
// Liveness heartbeat period while the decoder keeps producing frames.
constexpr int kProgressIntervalMs = 1000;

void ensureLowLatencyFfmpegOptions() {
  // Read by OpenCV's FFmpeg backend at open time. Respect a user override.
  if (!qgetenv("OPENCV_FFMPEG_CAPTURE_OPTIONS").isEmpty())
    return;
  // timeout bounds every RTSP socket read inside avformat_open_input /
  // avformat_find_stream_info. FFmpeg renamed this demuxer option from
  // stimeout to timeout (deprecated in 5.0, removed in 6.0) and the backend
  // shipped as opencv_videoio_ffmpeg500_64.dll links FFmpeg 7.x, so stimeout
  // on its own is an unknown key that is silently ignored. Both spellings are
  // written on purpose: the one the backend does not know is dropped by
  // av_opt, the other takes effect.
  // Without an effective timeout a camera that answers DESCRIBE 200 OK but
  // never sends the first RTP packet leaves capture.open() blocked; the 8 s
  // controller watchdog then terminate()s the reader thread, which leaks the
  // half-open RTSP session until the camera refuses every new client.
  qputenv("OPENCV_FFMPEG_CAPTURE_OPTIONS",
          "rtsp_transport;tcp|timeout;5000000|stimeout;5000000|fflags;nobuffer|"
          "flags;low_delay|max_delay;0|reorder_queue_size;0");
}

struct FfmpegBackendProbe {
  QStringList searchedDirectories;
  QStringList foundPaths;
};

QStringList backendSearchDirectories() {
  const QString appDir = QCoreApplication::applicationDirPath();
  QStringList directories{appDir};
  const QStringList relativeSubdirectories = {
      QStringLiteral("bin"),        QStringLiteral("release"),
      QStringLiteral("debug"),      QStringLiteral("runtime"),
      QStringLiteral("x64"),        QStringLiteral("bin/release"),
      QStringLiteral("bin/debug"),  QStringLiteral("x64/release"),
      QStringLiteral("x64/debug"),
  };
  for (const QString& relative : relativeSubdirectories) {
    const QString path = QDir(appDir).filePath(relative);
    if (QDir(path).exists())
      directories.append(path);
  }
  return directories;
}

FfmpegBackendProbe probeFfmpegBackend() {
  const QString pattern = QStringLiteral("opencv_videoio_ffmpeg*_64.dll");
  FfmpegBackendProbe probe;
  const auto scanDirectory = [&pattern, &probe](const QString& directoryPath) {
    if (directoryPath.trimmed().isEmpty())
      return;
    const QString cleanPath = QDir::cleanPath(directoryPath);
    if (probe.searchedDirectories.contains(cleanPath, Qt::CaseInsensitive))
      return;
    probe.searchedDirectories.append(cleanPath);
    const QDir directory(cleanPath);
    const QStringList entries =
        directory.entryList(QStringList{pattern}, QDir::Files);
    for (const QString& entry : entries) {
      const QString fullPath = directory.absoluteFilePath(entry);
      if (!probe.foundPaths.contains(fullPath, Qt::CaseInsensitive))
        probe.foundPaths.append(fullPath);
    }
  };

  for (const QString& directory : backendSearchDirectories())
    scanDirectory(directory);
  scanDirectory(qEnvironmentVariable("OPENCV_FFMPEG_DLL_DIR"));
  const QStringList pathEntries = qEnvironmentVariable("PATH").split(
      QDir::listSeparator(), Qt::SkipEmptyParts);
  for (const QString& pathEntry : pathEntries)
    scanDirectory(pathEntry);
  return probe;
}

bool hasFfmpegBackendDll() {
  return !probeFfmpegBackend().foundPaths.isEmpty();
}

void ensureFfmpegBackendConfigured() {
  const FfmpegBackendProbe probe = probeFfmpegBackend();
  const QString configured = qEnvironmentVariable("OPENCV_FFMPEG_DLL_DIR");
  if (probe.foundPaths.isEmpty()) {
    if (configured.isEmpty()) {
      qputenv("OPENCV_FFMPEG_DLL_DIR",
              QCoreApplication::applicationDirPath().toLocal8Bit());
    }
    return;
  }

  const QString foundDirectory =
      QFileInfo(probe.foundPaths.first()).absolutePath();
  const bool configuredHasBackend =
      !configured.isEmpty() &&
      !QDir(configured)
           .entryList(QStringList{QStringLiteral("opencv_videoio_ffmpeg*_64.dll")},
                      QDir::Files)
           .isEmpty();
  if (!configuredHasBackend) {
    qputenv("OPENCV_FFMPEG_DLL_DIR", foundDirectory.toLocal8Bit());
    if (configured != foundDirectory) {
      AppLogger::write(
          QStringLiteral("CAMERA.CONNECTION"),
          QStringLiteral("event=ffmpeg_backend_dir_override configured=\"%1\" "
                         "found=\"%2\" dll=\"%3\"")
              .arg(configured, foundDirectory, probe.foundPaths.first()));
    }
  }
}

QString streamOpenFailureReason(const QString& backendDetail = {}) {
  if (!hasFfmpegBackendDll()) {
    return CRAWLING_TEXT(
               "OpenCV FFmpeg 后端不可用：程序目录 %1 中没有 "
               "opencv_videoio_ffmpeg*_64.dll。VLC 使用自带解码库，"
               "VLC 能播放不代表本程序后端完整，请把该 DLL 复制到程序目录。")
        .arg(QCoreApplication::applicationDirPath());
  }
  if (!backendDetail.trimmed().isEmpty()) {
    return CRAWLING_TEXT("RTSP 打开异常（FFmpeg 后端已找到）：%1")
        .arg(backendDetail.trimmed());
  }
  return CRAWLING_TEXT(
      "RTSP 打开失败（FFmpeg 后端已找到）。请检查账号参数、视频编码和 "
      "TCP/UDP 传输方式；VLC 能播放只能说明地址和链路基本可用。");
}

QImage convertFrame(const cv::Mat& frame, int targetWidth, int targetHeight,
                    bool flipH, bool flipV) {
  if (frame.empty())
    return {};

  cv::Mat scaled = frame;
  if (targetWidth > 0 && targetHeight > 0 &&
      (frame.cols > targetWidth || frame.rows > targetHeight)) {
    const QSize size =
        QSize(frame.cols, frame.rows).scaled(targetWidth, targetHeight, Qt::KeepAspectRatio);
    if (size.width() > 0 && size.height() > 0)
      cv::resize(frame, scaled, cv::Size(size.width(), size.height()), 0, 0, cv::INTER_AREA);
  }

  if (flipH || flipV) {
    const int code = (flipH && flipV) ? -1 : (flipH ? 1 : 0);
    cv::Mat flipped;
    cv::flip(scaled, flipped, code);
    scaled = flipped;
  }

  if (scaled.channels() == 1 || scaled.channels() == 3) {
    QImage image(scaled.cols, scaled.rows, QImage::Format_RGB32);
    if (image.isNull())
      return {};
    cv::Mat dst(image.height(), image.width(), CV_8UC4, image.bits(),
                static_cast<size_t>(image.bytesPerLine()));
    try {
      cv::cvtColor(scaled, dst, scaled.channels() == 1 ? cv::COLOR_GRAY2BGRA
                                                       : cv::COLOR_BGR2BGRA);
    } catch (const cv::Exception&) {
      return {};
    }
    return image;
  }

  if (scaled.channels() == 4) {
    QImage image(scaled.cols, scaled.rows, QImage::Format_RGB888);
    if (image.isNull())
      return {};
    cv::Mat dst(image.height(), image.width(), CV_8UC3, image.bits(),
                static_cast<size_t>(image.bytesPerLine()));
    try {
      cv::cvtColor(scaled, dst, cv::COLOR_BGRA2RGB);
    } catch (const cv::Exception&) {
      return {};
    }
    return image;
  }

  return {};
}

}  // namespace

QString NetworkStreamReader::redactStreamUrl(const QString& url) {
  QString redacted = url;
  static const QRegularExpression passwordPattern(
      QStringLiteral("([?&;](?:password|passwd|pass|pwd)=)[^&;]*"),
      QRegularExpression::CaseInsensitiveOption);
  redacted.replace(passwordPattern, QStringLiteral("\\1***"));

  // rtsp://user:secret@host -> rtsp://user:***@host
  static const QRegularExpression userInfoPattern(
      QStringLiteral("(://[^/@:]*:)[^/@]*@"));
  redacted.replace(userInfoPattern, QStringLiteral("\\1***@"));
  return redacted;
}

QString NetworkStreamReader::backendDiagnostics() {
  const FfmpegBackendProbe probe = probeFfmpegBackend();
  const QStringList pathEntries = qEnvironmentVariable("PATH").split(
      QDir::listSeparator(), Qt::SkipEmptyParts);
  const QString appDir = QCoreApplication::applicationDirPath();
  const QStringList appDlls = QDir(appDir).entryList(
      QStringList{QStringLiteral("opencv_world*.dll"),
                  QStringLiteral("opencv_videoio*.dll")},
      QDir::Files);

  QStringList dllStates;
  for (const QString& path : probe.foundPaths) {
    QLibrary library(path);
    const bool loaded = library.load();
    if (loaded) {
      dllStates.append(QStringLiteral("%1(load=OK)").arg(path));
      library.unload();
    } else {
      dllStates.append(QStringLiteral("%1(load=FAILED,%2)")
                           .arg(path, library.errorString()));
    }
  }
  if (dllStates.isEmpty())
    dllStates.append(QStringLiteral("MISSING"));

  return QStringLiteral(
             "app_dir=\"%1\" executable=\"%2\" pid=%3 opencv_version=%4 "
             "ffmpeg_backend=%5 ffmpeg_dlls=\"%6\" searched_dirs=\"%7\" "
             "path_entries=%8 opencv_ffmpeg_dll_dir=\"%9\" capture_options=\"%10\" "
             "user=\"%11\" computer=\"%12\" app_dlls=\"%13\"")
      .arg(appDir, QCoreApplication::applicationFilePath())
      .arg(QCoreApplication::applicationPid())
      .arg(QStringLiteral(CV_VERSION))
      .arg(probe.foundPaths.isEmpty() ? QStringLiteral("MISSING")
                                      : QStringLiteral("FOUND"))
      .arg(dllStates.join(QLatin1String(";")))
      .arg(probe.searchedDirectories.join(QLatin1String(";")))
      .arg(static_cast<int>(pathEntries.size()))
      .arg(qEnvironmentVariable("OPENCV_FFMPEG_DLL_DIR"))
      .arg(qEnvironmentVariable("OPENCV_FFMPEG_CAPTURE_OPTIONS"))
      .arg(qEnvironmentVariable("USERNAME"))
      .arg(QSysInfo::machineHostName())
      .arg(appDlls.join(QLatin1String(";")));
}

void NetworkStreamReader::logEnvironmentSnapshot(const QString& trigger) {
  static QMutex mutex;
  static bool logged = false;
  QMutexLocker locker(&mutex);
  if (logged)
    return;
  logged = true;
  ensureLowLatencyFfmpegOptions();
  ensureFfmpegBackendConfigured();
  const QString diagnostics = backendDiagnostics();
  const QString message =
      QStringLiteral("event=camera_environment trigger=\"%1\" %2")
          .arg(trigger, diagnostics);
  if (probeFfmpegBackend().foundPaths.isEmpty()) {
    AppLogger::warning(QStringLiteral("CAMERA.CONNECTION"), message);
  } else {
    AppLogger::write(QStringLiteral("CAMERA.CONNECTION"), message);
  }
}

NetworkStreamReader::NetworkStreamReader(const QString& url, int slot, QObject* parent)
    : QThread(parent), url_(url), slot_(slot) {
  // Set here (owner thread) rather than in run() so concurrent readers do not race on the env.
  ensureLowLatencyFfmpegOptions();
  ensureFfmpegBackendConfigured();
}

NetworkStreamReader::~NetworkStreamReader() {
  requestStop();
  wait();
}

void NetworkStreamReader::setTargetSize(int width, int height) {
  targetWidth_.storeRelease(width);
  targetHeight_.storeRelease(height);
}

void NetworkStreamReader::setFlip(bool horizontal, bool vertical) {
  flipH_.storeRelease(horizontal ? 1 : 0);
  flipV_.storeRelease(vertical ? 1 : 0);
}

void NetworkStreamReader::releaseFrame() {
  busy_.storeRelease(0);
  diagReleased_.fetchAndAddRelaxed(1);
}

void NetworkStreamReader::maybeLogReaderStats(qint64 nowMs) {
  if (nowMs - lastReaderLogMs_ < 2000) return;
  const qint64 windowMs = nowMs - lastReaderLogMs_;
  lastReaderLogMs_ = nowMs;
  const int released = diagReleased_.fetchAndStoreRelaxed(0);
  crawling::AppLogger::write(
      QStringLiteral("CAMERA.READER"),
      QStringLiteral("event=reader_backpressure slot=%1 window_ms=%2 "
                     "emitted=%3 released=%4 busy_skips=%5 busy_now=%6")
          .arg(slot_)
          .arg(windowMs)
          .arg(diagEmitted_)
          .arg(released)
          .arg(diagBusySkips_)
          .arg(busy_.loadAcquire()));
  diagEmitted_ = 0;
  diagBusySkips_ = 0;
}

void NetworkStreamReader::requestStop() {
  stop_.storeRelease(1);
}

void NetworkStreamReader::probeEndpoint(const QString& safeUrl) {
  const auto report = [this, &safeUrl](const QString& stage, const QString& detail) {
    const QString text =
        QStringLiteral("event=rtsp_probe slot=%1 stage=%2 %3 url=\"%4\"")
            .arg(slot_)
            .arg(stage, detail, safeUrl);
    AppLogger::write(QStringLiteral("USB_CAMERA.CONNECTION"), text);
    emit diagnostic(slot_, text);
  };

  const QUrl parsed(url_);
  const QString host = parsed.host();
  if (host.isEmpty()) {
    report(QStringLiteral("parse"), QStringLiteral("result=FAILED reason=empty_host"));
    return;
  }
  const int port = parsed.port(554) > 0 ? parsed.port(554) : 554;

  QElapsedTimer timer;
  timer.start();
  QHostAddress literal;
  if (literal.setAddress(host)) {
    // The camera URLs are IP literals. Resolving them through the system
    // resolver cost ~9.4 s on the robot host (dead DNS server), which pushed
    // every open attempt into the 15 s timeout window.
    report(QStringLiteral("dns"),
           QStringLiteral("result=SKIP reason=ip_literal addr=%1 elapsed_ms=%2")
               .arg(literal.toString())
               .arg(timer.elapsed()));
  } else {
    const QHostInfo info = QHostInfo::fromName(host);
    QStringList addresses;
    for (const QHostAddress& address : info.addresses())
      addresses << address.toString();
    if (info.error() != QHostInfo::NoError || addresses.isEmpty()) {
      report(QStringLiteral("dns"),
             QStringLiteral("result=FAILED host=%1 error=\"%2\" elapsed_ms=%3")
                 .arg(host, info.errorString())
                 .arg(timer.elapsed()));
      return;
    }
    report(QStringLiteral("dns"),
           QStringLiteral("result=OK host=%1 addrs=%2 elapsed_ms=%3")
               .arg(host, addresses.join(QLatin1Char(',')),
                    QString::number(timer.elapsed())));
  }

  QTcpSocket socket;
  timer.restart();
  socket.connectToHost(host, static_cast<quint16>(port));
  if (!socket.waitForConnected(3000)) {
    report(QStringLiteral("tcp"),
           QStringLiteral("result=FAILED peer=%1:%2 error=\"%3\" elapsed_ms=%4")
               .arg(host)
               .arg(port)
               .arg(socket.errorString())
               .arg(timer.elapsed()));
    return;
  }
  report(QStringLiteral("tcp"),
         QStringLiteral("result=OK peer=%1:%2 local=%3 elapsed_ms=%4")
             .arg(host)
             .arg(port)
             .arg(socket.localPort())
             .arg(timer.elapsed()));

  QString path = parsed.path();
  if (path.isEmpty()) path = QStringLiteral("/");
  if (!parsed.query().isEmpty()) path += QLatin1Char('?') + parsed.query();
  QByteArray request = "DESCRIBE " + path.toUtf8() + " RTSP/1.0\r\n";
  request += "CSeq: 1\r\n";
  request += "User-Agent: PA1664Probe\r\n";
  request += "Accept: application/sdp\r\n";
  if (!parsed.userName().isEmpty()) {
    const QByteArray credentials =
        (parsed.userName() + QLatin1Char(':') + parsed.password()).toUtf8();
    request += "Authorization: Basic " + credentials.toBase64() + "\r\n";
  }
  request += "\r\n";

  timer.restart();
  socket.write(request);
  socket.waitForBytesWritten(1000);
  if (!socket.waitForReadyRead(3000)) {
    report(QStringLiteral("rtsp"),
           QStringLiteral("result=NO_REPLY elapsed_ms=%1 error=\"%2\"")
               .arg(timer.elapsed())
               .arg(socket.errorString()));
    socket.abort();
    return;
  }
  const QByteArray reply = socket.readAll();
  socket.abort();
  const int lineEnd = reply.indexOf('\r');
  const QString status = QString::fromUtf8(
      lineEnd > 0 ? reply.left(lineEnd) : reply.left(80));
  report(QStringLiteral("rtsp"),
         QStringLiteral("result=REPLY status=\"%1\" elapsed_ms=%2")
             .arg(status)
             .arg(timer.elapsed()));
}

void NetworkStreamReader::run() {
  logEnvironmentSnapshot(QStringLiteral("network_open slot=%1").arg(slot_));

  const QString safeUrl = redactStreamUrl(url_);
  QElapsedTimer openTimer;
  openTimer.start();
  probeEndpoint(safeUrl);
  AppLogger::write(
      QStringLiteral("USB_CAMERA.CONNECTION"),
      QStringLiteral("event=stream_open_attempt slot=%1 url=\"%2\" "
                     "open_timeout_ms=%3 read_timeout_ms=%4 transport=\"tcp\"")
          .arg(slot_)
          .arg(safeUrl)
          .arg(kOpenTimeoutMs)
          .arg(kReadTimeoutMs));

  cv::VideoCapture capture;
  const std::vector<int> params = {
      cv::CAP_PROP_OPEN_TIMEOUT_MSEC, kOpenTimeoutMs,
      cv::CAP_PROP_READ_TIMEOUT_MSEC, kReadTimeoutMs,
  };
  bool ok = false;
  try {
    const QByteArray encodedUrl = url_.toUtf8();
    ok = capture.open(std::string(encodedUrl.constData(), encodedUrl.size()), cv::CAP_FFMPEG, params);
  } catch (const cv::Exception& e) {
    const QString reason = streamOpenFailureReason(QString::fromUtf8(e.what()));
    AppLogger::error(
        QStringLiteral("USB_CAMERA.CONNECTION"),
        QStringLiteral("event=stream_open_failed slot=%1 url=\"%2\" elapsed_ms=%3 reason=\"%4\"")
            .arg(slot_)
            .arg(safeUrl)
            .arg(openTimer.elapsed())
            .arg(reason));
    emit failed(slot_, reason);
    return;
  }
  if (!ok || !capture.isOpened()) {
    const QString reason = streamOpenFailureReason();
    AppLogger::error(
        QStringLiteral("USB_CAMERA.CONNECTION"),
        QStringLiteral("event=stream_open_failed slot=%1 url=\"%2\" elapsed_ms=%3 reason=\"%4\"")
            .arg(slot_)
            .arg(safeUrl)
            .arg(openTimer.elapsed())
            .arg(reason));
    emit failed(slot_, reason);
    return;
  }
  if (stop_.loadAcquire())
    return;

  const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
  const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
  const double fps = capture.get(cv::CAP_PROP_FPS);
  AppLogger::write(
      QStringLiteral("USB_CAMERA.CONNECTION"),
      QStringLiteral("event=stream_open_ok slot=%1 url=\"%2\" elapsed_ms=%3 "
                     "width=%4 height=%5 fps=%6")
          .arg(slot_)
          .arg(safeUrl)
          .arg(openTimer.elapsed())
          .arg(width)
          .arg(height)
          .arg(fps));
  emit opened(slot_, width, height, fps);

  cv::Mat frame;
  quint64 grabbedFrames = 0;
  quint64 emittedFrames = 0;
  QElapsedTimer streamTimer;
  streamTimer.start();
  qint64 lastProgressMs = -kProgressIntervalMs;
  // OpenCV throws cv::Exception from grab()/retrieve() when the RTSP session is
  // reset (camera drops a client, network hiccup, decoder error). Those calls
  // used to sit outside any try/catch, so the exception escaped this QThread,
  // hit std::terminate and aborted the whole robot host ~9-15 s after start —
  // right when the cameras begin streaming.
  try {
  while (!stop_.loadAcquire()) {
    // grab() only demuxes/decodes; it keeps the network/decoder buffers empty.
    if (!capture.grab()) {
      if (!stop_.loadAcquire()) {
        AppLogger::warning(
            QStringLiteral("USB_CAMERA.CONNECTION"),
            QStringLiteral("event=stream_read_failed slot=%1 url=\"%2\" "
                           "grabbed_frames=%3 emitted_frames=%4 elapsed_ms=%5")
                .arg(slot_)
                .arg(safeUrl)
                .arg(grabbedFrames)
                .arg(emittedFrames)
                .arg(streamTimer.elapsed()));
        emit failed(slot_, CRAWLING_TEXT("视频流读取中断"));
      }
      break;
    }
    ++grabbedFrames;

    // Report liveness once per second regardless of consumer backpressure.
    const qint64 elapsedMs = streamTimer.elapsed();
    if (elapsedMs - lastProgressMs >= kProgressIntervalMs) {
      lastProgressMs = elapsedMs;
      emit progress(slot_);
    }
    maybeLogReaderStats(QDateTime::currentMSecsSinceEpoch());

    // While the consumer is busy, grab() advances the decoder and drops the
    // skipped frame. Keep strict backpressure so stale frames cannot pile up
    // in the UI event queue.
    if (busy_.loadAcquire()) {
      ++diagBusySkips_;
      continue;
    }

    // retrieve() does the colour conversion, so only pay for it on frames we send.
    if (!capture.retrieve(frame) || frame.empty())
      continue;

    QImage image = convertFrame(frame, targetWidth_.loadAcquire(), targetHeight_.loadAcquire(),
                                flipH_.loadAcquire() != 0, flipV_.loadAcquire() != 0);
    if (image.isNull())
      continue;

    busy_.storeRelease(1);
    ++emittedFrames;
    ++diagEmitted_;
    emit frameReady(slot_, image);
  }
  } catch (const cv::Exception& e) {
    const QString reason = streamOpenFailureReason(QString::fromUtf8(e.what()));
    AppLogger::error(
        QStringLiteral("USB_CAMERA.CONNECTION"),
        QStringLiteral("event=stream_exception slot=%1 url=\"%2\" "
                       "grabbed_frames=%3 emitted_frames=%4 reason=\"%5\"")
            .arg(slot_)
            .arg(safeUrl)
            .arg(grabbedFrames)
            .arg(emittedFrames)
            .arg(reason));
    if (!stop_.loadAcquire()) emit failed(slot_, reason);
  } catch (const std::exception& e) {
    const QString reason = QString::fromUtf8(e.what());
    AppLogger::error(
        QStringLiteral("USB_CAMERA.CONNECTION"),
        QStringLiteral("event=stream_exception slot=%1 url=\"%2\" kind=std reason=\"%3\"")
            .arg(slot_)
            .arg(safeUrl)
            .arg(reason));
    if (!stop_.loadAcquire())
      emit failed(slot_, reason.isEmpty() ? CRAWLING_TEXT("视频流异常中断")
                                          : reason);
  } catch (...) {
    AppLogger::error(
        QStringLiteral("USB_CAMERA.CONNECTION"),
        QStringLiteral("event=stream_exception slot=%1 url=\"%2\" kind=unknown")
            .arg(slot_)
            .arg(safeUrl));
    if (!stop_.loadAcquire())
      emit failed(slot_, CRAWLING_TEXT("视频流异常中断"));
  }
  capture.release();
  AppLogger::write(
      QStringLiteral("USB_CAMERA.CONNECTION"),
      QStringLiteral("event=stream_stop slot=%1 url=\"%2\" requested_stop=%3 "
                     "grabbed_frames=%4 emitted_frames=%5 elapsed_ms=%6")
          .arg(slot_)
          .arg(safeUrl)
          .arg(stop_.loadAcquire() ? 1 : 0)
          .arg(grabbedFrames)
          .arg(emittedFrames)
          .arg(streamTimer.elapsed()));
}

}  // namespace crawling
