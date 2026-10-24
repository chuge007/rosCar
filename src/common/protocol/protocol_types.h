#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace pa1664::remote {

inline constexpr quint32 kWireMagic = 0x50413136u;  // "PA16" big-endian bytes
inline constexpr quint16 kProtocolVersion = 1;
inline constexpr quint32 kDefaultControlPort = 43100;
inline constexpr quint16 kDefaultRealtimePort = 43101;
inline constexpr quint16 kDefaultMediaPort = 43102;
inline constexpr quint32 kDefaultBulkPort = 43103;
inline constexpr int kControlHeaderBytes = 50;
inline constexpr int kMaxControlPayloadBytes = 1024 * 1024;
// Preset files are Base64-wrapped in a JSON control request.  Keep the
// source file small enough that the complete request remains below the
// 1 MiB control-frame limit even with operation/data/JSON escaping overhead.
inline constexpr int kMaxPresetImportBytes = 640 * 1024;
inline constexpr int kMaxRealtimeDatagramBytes = 1200;
inline constexpr int kRealtimeAuthTagBytes = 16;
inline constexpr int kRealtimeHeaderBytes = 44;
inline constexpr int kMotionSendIntervalMs = 50;
inline constexpr int kMotionLeaseMs = 200;
inline constexpr int kHeartbeatIntervalMs = 250;
inline constexpr int kSessionTimeoutMs = 1000;

// Capabilities are negotiated during the control handshake.  The UI must
// gate optional operations on these values instead of assuming that every
// host build implements every message family.
inline constexpr char kCapabilityRobotControlV1[] = "robot.control.v1";
inline constexpr char kCapabilityRobotTelemetryV1[] = "robot.telemetry.v1";
inline constexpr char kCapabilityRobotSettingsV1[] = "robot.settings.v1";
inline constexpr quint16 kMediaHeaderBytes = 58;
// 28-byte wire header; the 16-byte HMAC tag is appended separately.
inline constexpr quint16 kBulkHeaderBytes = 28;
inline constexpr int kMediaAuthTagBytes = 16;
inline constexpr int kMediaHeartbeatIntervalMs = 500;
inline constexpr int kMediaAssemblyTimeoutMs = 1500;
inline constexpr int kMaxMediaMessageBytes = 8 * 1024 * 1024;
inline constexpr int kMaxMediaFragments = 8192;
inline constexpr int kMaxBulkPayloadBytes = 32 * 1024 * 1024;
inline constexpr int kBulkConnectTimeoutMs = 3000;
inline constexpr int kBulkWriteHighWaterBytes = 8 * 1024 * 1024;

inline constexpr char kCapabilityUltrasoundControlV1[] = "ultrasound.control.v1";
inline constexpr char kCapabilityUltrasoundParametersV1[] =
    "ultrasound.parameters.v1";
inline constexpr char kCapabilityUltrasoundMediaV1[] = "ultrasound.media.v1";
inline constexpr char kCapabilityUltrasoundBulkV1[] = "ultrasound.bulk.v1";
inline constexpr char kCapabilityRobotSensorsV1[] = "robot.sensors.v1";
inline constexpr char kCapabilityRobotMediaV1[] = "robot.media.v1";
inline constexpr char kCapabilityRobotBulkV1[] = "robot.bulk.v1";

// Stable media stream IDs.  IDs are intentionally grouped by producer so the
// operator can route payloads without parsing a second envelope.
inline constexpr quint16 kMediaStreamUltrasoundLiveBase = 0x0100;
inline constexpr quint16 kMediaStreamLaserImage = 0x0201;
inline constexpr quint16 kMediaStreamLaserPointCloud = 0x0202;
inline constexpr quint16 kMediaStreamImu = 0x0203;
inline constexpr quint16 kMediaStreamUsbCameraBase = 0x0300;
inline constexpr quint16 kMediaStreamCorrectionStatus = 0x0401;

enum class ControlMessageType : quint16 {
  Hello = 1,
  HelloAck = 2,
  Request = 3,
  Response = 4,
  Event = 5,
  Ping = 6,
  Pong = 7,
  Error = 8,
};

enum class ControlFlags : quint16 {
  None = 0,
  ResponseToRequest = 1u << 0,
  Urgent = 1u << 1,
};

inline ControlFlags operator|(ControlFlags left, ControlFlags right) {
  return static_cast<ControlFlags>(static_cast<quint16>(left) |
                                   static_cast<quint16>(right));
}

inline bool hasFlag(ControlFlags value, ControlFlags flag) {
  return (static_cast<quint16>(value) & static_cast<quint16>(flag)) != 0;
}

enum class RealtimePacketType : quint8 {
  Heartbeat = 1,
  MotionSetpoint = 2,
  Telemetry = 3,
  EmergencyStop = 4,
  Ack = 5,
};

enum class RealtimeFlags : quint8 {
  None = 0,
  AuthPresent = 1u << 0,
  Reply = 1u << 1,
};

inline RealtimeFlags operator|(RealtimeFlags left, RealtimeFlags right) {
  return static_cast<RealtimeFlags>(static_cast<quint8>(left) |
                                    static_cast<quint8>(right));
}

inline bool hasFlag(RealtimeFlags value, RealtimeFlags flag) {
  return (static_cast<quint8>(value) & static_cast<quint8>(flag)) != 0;
}

enum class DecodeResult {
  Ok,
  Incomplete,
  BadMagic,
  UnsupportedVersion,
  InvalidLength,
  ChecksumMismatch,
  AuthenticationFailed,
  InvalidPayload,
};

struct ControlFrame {
  ControlMessageType type = ControlMessageType::Request;
  ControlFlags flags = ControlFlags::None;
  quint64 sessionId = 0;
  quint64 sequence = 0;
  quint64 requestId = 0;
  quint64 timestampUs = 0;
  QByteArray payload;
};

struct RealtimePacket {
  RealtimePacketType type = RealtimePacketType::Heartbeat;
  RealtimeFlags flags = RealtimeFlags::None;
  quint64 sessionId = 0;
  quint64 sequence = 0;
  quint64 timestampUs = 0;
  quint16 ttlMs = 0;
  qint32 linearMilliMps = 0;
  qint32 angularMilliRadps = 0;
  QByteArray payload;
};


// Lower-latency media is intentionally independent from the control TCP
// stream.  Large frames are fragmented and every fragment is authenticated.
enum class MediaStreamKind : quint8 {
  Heartbeat = 1,
  UltrasoundFrame = 2,
  UltrasoundScanBatch = 3,
  LaserImage = 4,
  LaserPointCloud = 5,
  UsbCameraImage = 6,
  CorrectionStatus = 7,
  DeviceEvent = 8,
  ImuSample = 9,
};

enum class BulkStreamKind : quint16 {
  Heartbeat = 1,
  UltrasoundScanBatch = 2,
  RecordingPacket = 3,
  Configuration = 4,
  JsonEvent = 5,
};

struct MediaFragmentPacket {
  MediaStreamKind kind = MediaStreamKind::Heartbeat;
  quint8 flags = 0;
  quint64 sessionId = 0;
  quint16 streamId = 0;
  quint64 messageId = 0;
  quint64 sequence = 0;
  quint64 timestampUs = 0;
  quint32 totalBytes = 0;
  quint32 fragmentIndex = 0;
  quint32 fragmentCount = 0;
  QByteArray payload;
};

struct BulkFrame {
  BulkStreamKind kind = BulkStreamKind::Heartbeat;
  quint64 sessionId = 0;
  quint64 sequence = 0;
  QByteArray payload;
};
struct DecodeError {
  DecodeResult result = DecodeResult::Ok;
  QString detail;

  bool ok() const { return result == DecodeResult::Ok; }
};

QString decodeResultText(DecodeResult result);
QString controlMessageTypeText(ControlMessageType type);

}  // namespace pa1664::remote

Q_DECLARE_METATYPE(pa1664::remote::ControlFrame)
Q_DECLARE_METATYPE(pa1664::remote::RealtimePacket)


Q_DECLARE_METATYPE(pa1664::remote::MediaStreamKind)
Q_DECLARE_METATYPE(pa1664::remote::BulkStreamKind)
Q_DECLARE_METATYPE(pa1664::remote::MediaFragmentPacket)
Q_DECLARE_METATYPE(pa1664::remote::BulkFrame)

