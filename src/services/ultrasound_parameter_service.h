#pragma once
#include "client.h"
#include "parameterpresetstore.h"
#include "protocol/protocol_types.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QVariant>
#include <QVector>
class QTimer;

namespace pa1664::remote {

class RemoteWorkbenchSession;

// Stable synchronous facade used by the existing ParameterPanel. Local mode
// forwards to the vendor SDK. Remote mode serves cached readbacks while all
// writes and readbacks travel asynchronously over the control channel.
class UltrasoundParameterService final : public QObject {
  Q_OBJECT
 public:
  static UltrasoundParameterService& instance();
  void bindRemoteSession(RemoteWorkbenchSession* session);
  bool isRemote() const;
  void requestSnapshot();

  // Host-owned preset model. Remote mode never reads or writes the operator's
  // local default_config.json for hardware presets.
  QList<ParameterPresetInfo> presetList() const { return presets_; }
  int currentPresetId() const { return currentPresetId_; }
  void refreshPresets();
  void capturePreset(int presetId);
  void switchPreset(int presetId);
  void copyPreset(int sourcePresetId);
  void removePreset(int presetId);
  void importPresetFile(const QString& path);
  void exportPresetFile(const QString& path);
  void saveScanPrecisionMmPerCell(int presetId, double mmPerCell);
  void requestScanPrecision(int presetId);
  void saveEncoderCalibration(double wheelDiameterMm, int resolution);
  void requestEncoderCalibration();
  void commitPendingChanges(int presetId);

 public slots:
  void flushPendingGets();
  void flushPendingSets();

 signals:
  void parametersChanged();
  void hardwareSnapshotReady(const QJsonDocument& snapshot);
  void message(const QString& text, bool error);
  void presetsChanged();
  void remoteApplyFinished(bool ok, const QString& message);
  void remotePresetOperationFinished(const QString& operation, bool ok,
                                     int presetId, const QString& message);
  void scanPrecisionChanged(int presetId, double mmPerCell);
  void encoderCalibrationReady(double wheelDiameterMm, int resolution);

 public:
  double getBeamAngleLinear(int groupId = -1);
  double getBeamAngleMax(int groupId = -1);
  double getBeamAngleMin(int groupId = -1);
  double getBeamAngleStep(int groupId = -1);
  int getBeamAperture(int groupId = -1);
  int getBeamElementStep(int groupId = -1);
  int getBeamFirstElement(int groupId = -1);
  int getBeamLastElement(int groupId = -1);
  BeamMode getBeamMode(int groupId = -1);
  bool getDualCrystalEnable(int groupId = -1);
  double getFilterHigh(int groupId = -1);
  double getFilterLow(int groupId = -1);
  double getFocalPosition(int groupId = -1);
  FocalType getFocusMode(int groupId = -1);
  int getFrameRate();
  QVector<int> getGroupsNo(bool includeHardware = false);
  int getBeamCounts(int groupId = -1);
  double getGain(int groupId = -1);
  bool getGateAEnable(int groupId = -1);
  double getGateAEnd(int groupId = -1);
  measureType getGateAMeasureType(int groupId = -1);
  double getGateAStart(int groupId = -1);
  GateSynchron getGateASynchronMode(int groupId = -1);
  double getGateAThreshold(int groupId = -1);
  bool getGateBEnable(int groupId = -1);
  double getGateBEnd(int groupId = -1);
  measureType getGateBMeasureType(int groupId = -1);
  double getGateBStart(int groupId = -1);
  GateSynchron getGateBSynchronMode(int groupId = -1);
  double getGateBThreshold(int groupId = -1);
  bool getGateCEnable(int groupId = -1);
  double getGateCEnd(int groupId = -1);
  measureType getGateCMeasureType(int groupId = -1);
  double getGateCStart(int groupId = -1);
  GateSynchron getGateCSynchronMode(int groupId = -1);
  double getGateCThreshold(int groupId = -1);
  bool getGateIEnable(int groupId = -1);
  double getGateIEnd(int groupId = -1);
  measureType getGateIMeasureType(int groupId = -1);
  double getGateIStart(int groupId = -1);
  bool getGateISyncSample(int groupId = -1);
  GateSynchron getGateISynchronMode(int groupId = -1);
  double getGateIThreshold(int groupId = -1);
  MaxAmplitude getMaxAmplitude(int groupId = -1);
  int getPaVoltage();
  int getPointQuantity(int groupId = -1);
  double getProbeFrequency(int groupId = -1);
  int getProbePrimaryElements(int groupId = -1);
  double getProbePrimaryElementsPitch(int groupId = -1);
  int getProbeSecondaryElements(int groupId = -1);
  double getProbeSecondaryElementsPitch(int groupId = -1);
  int getPulseWidth(int groupId = -1);
  double getRangeEnd(int groupId = -1);
  double getRangeStart(int groupId = -1);
  int getReceiverReject(int groupId = -1);
  int getReceptionChannel(int groupId = -1);
  RectifierType getRectifierMode(int groupId = -1);
  bool getReverseEnable(int groupId = -1);
  int getScanMode();
  scanType getScanType(int groupId = -1);
  ScannerDir getScannerDirection();
  bool getTcgEnable(int groupId = -1);
  double getTcgPointDepth(int index, int beamIndex, int groupId = -1);
  double getTcgPointDepthIndex(int index, int groupId = -1);
  double getTcgPointGain(int index, int beamIndex, int groupId = -1);
  double getTcgPointGainIndex(int index, int groupId = -1);
  double getTfmRecDelay(int groupId = -1);
  double getThickness(int groupId);
  int getTransmissionChannel(int groupId = -1);
  int getUtVoltage();
  VideoFilter getVideoFilterMHz(int groupId = -1);
  PolarityType getVoltagePolarity(int groupId = -1);
  double getWedgeAngle(int groupId = -1);
  bool getWedgeEnable(int groupId = -1);
  double getWedgeGCp(int groupId = -1);
  double getWedgeHeight(int groupId = -1);
  double getWedgeLength(int groupId = -1);
  double getWedgeRoofAngle(int groupId = -1);
  double getWedgeVelocity(int groupId = -1);
  double getWedgeWidth(int groupId = -1);
  double getWedgeX(int groupId = -1);
  double getWedgeZ(int groupId = -1);
  double getWorkpieceVelocity(int groupId = -1);
  bool setBeamAngleLinear(double angle, int groupId = -1);
  bool setBeamAngleMax(double BeamAngleMax, int groupId = -1);
  bool setBeamAngleMin(double BeamAngleMin, int groupId = -1);
  bool setBeamAngleStep(double BeamAngleStep, int groupId = -1);
  bool setBeamAperture(int BeamAperture, int groupId = -1);
  bool setBeamElementStep(int step, int groupId = -1);
  bool setBeamFirstElement(int firstElement, int groupId = -1);
  bool setBeamLastElement(int lastElement, int groupId = -1);
  bool setBeamMode(BeamMode mode, int groupId = -1);
  bool setDualCrystalEnable(bool enable, int groupId = -1);
  bool setFilterHigh(double freq, int groupId = -1);
  bool setFilterLow(double freq, int groupId = -1);
  bool setFocalPosition(double position, int groupId = -1);
  bool setFocusMode(FocalType mode, int groupId = -1);
  bool setFrameRate(int rate);
  bool setGain(double gain, int groupId = -1);
  bool setGateAEnable(bool enable, int groupId = -1);
  bool setGateAEnd(double end, int groupId = -1);
  bool setGateAMeasureType(measureType measureType, int groupId = -1);
  bool setGateAStart(double start, int groupId = -1);
  bool setGateASynchronMode(GateSynchron synchro, int groupId = -1);
  bool setGateAThreshold(double threshold, int groupId = -1);
  bool setGateBEnable(bool enable, int groupId = -1);
  bool setGateBEnd(double end, int groupId = -1);
  bool setGateBMeasureType(measureType measureType, int groupId = -1);
  bool setGateBStart(double start, int groupId = -1);
  bool setGateBSynchronMode(GateSynchron synchro, int groupId = -1);
  bool setGateBThreshold(double threshold, int groupId = -1);
  bool setGateCEnable(bool enable, int groupId = -1);
  bool setGateCEnd(double end, int groupId = -1);
  bool setGateCMeasureType(measureType measureType, int groupId = -1);
  bool setGateCStart(double start, int groupId = -1);
  bool setGateCSynchronMode(GateSynchron synchro, int groupId = -1);
  bool setGateCThreshold(double threshold, int groupId = -1);
  bool setGateIEnable(bool enable, int groupId = -1);
  bool setGateIEnd(double end, int groupId = -1);
  bool setGateIMeasureType(measureType measureType, int groupId = -1);
  bool setGateIStart(double start, int groupId = -1);
  bool setGateISyncSample(bool enable, int groupId = -1);
  bool setGateISynchronMode(GateSynchron synchro, int groupId = -1);
  bool setGateIThreshold(double threshold, int groupId = -1);
  bool setMaxAmplitude(MaxAmplitude type, int groupId = -1);
  bool setPaVoltage(int voltage);
  bool setPointQuantity(int quantity, int groupId = -1);
  bool setProbeFrequency(double frequency, int groupId = -1);
  bool setProbePrimaryElements(int elements, int groupId = -1);
  bool setProbePrimaryElementsPitch(double pitch, int groupId = -1);
  bool setProbeSecondaryElements(int elements, int groupId = -1);
  bool setProbeSecondaryElementsPitch(double pitch, int groupId = -1);
  bool setPulseWidth(int width, int groupId = -1);
  bool setRangeEnd(double end, int groupId = -1);
  bool setRangeStart(double start, int groupId = -1);
  bool setReceiverReject(int reject, int groupId = -1);
  bool setReceptionChannel(int reception, int groupId = -1);
  bool setRectifierMode(RectifierType mode, int groupId = -1);
  bool setReverseEnable(bool flag, int groupId = -1);
  bool setScanMode(int mode);
  bool setScanType(scanType type, int groupId = -1);
  bool setScannerDirection(ScannerDir mode);
  bool setTcgEnable(bool enable, int groupId = -1);
  bool setTcgPointDepth(int index, double depth, int beamIndex, int groupId = -1);
  bool setTcgPointDepthIndex(int index, double depth, int groupId = -1);
  bool setTcgPointGain(int index, double gain, int beamIndex, int groupId = -1);
  bool setTcgPointGainIndex(int index, double gain, int groupId = -1);
  bool setTfmRecDelay(double delay, int groupId = -1);
  bool setThickness(double thick, int groupId);
  bool setTransmissionChannel(int transmission, int groupId = -1);
  bool setUtVoltage(int voltage);
  bool setVideoFilterMHz(VideoFilter type, int groupId = -1);
  bool setVoltagePolarity(PolarityType mode, int groupId = -1);
  bool setWedgeAngle(double angle, int groupId = -1);
  bool setWedgeEnable(bool enable, int groupId = -1);
  bool setWedgeGCp(double gcp, int groupId = -1);
  bool setWedgeHeight(double wedgeHeight, int groupId = -1);
  bool setWedgeLength(double wedgeLength, int groupId = -1);
  bool setWedgeRoofAngle(double roofangle, int groupId = -1);
  bool setWedgeVelocity(double wedgeVelocity, int groupId = -1);
  bool setWedgeWidth(double wedgeWidth, int groupId = -1);
  bool setWedgeX(double x, int groupId = -1);
  bool setWedgeZ(double z, int groupId = -1);
  bool setWorkpieceVelocity(double velocity, int groupId = -1);

 private:
  struct SetRequest {
    QString method;
    QJsonArray args;
    QString readbackMethod;
    QJsonArray readbackArgs;
    QString readbackKey;
  };
  struct PresetRequest {
    QString operation;
    QJsonObject data;
    int presetId = -1;
    QString exportPath;
  };
  explicit UltrasoundParameterService(QObject* parent = nullptr);
  QVariant cachedValue(const QString& method, const QJsonArray& args,
                       const QVariant& fallback);
  void enqueueGet(const QString& method, const QJsonArray& args);
  bool sendSet(const QString& method, const QJsonArray& args,
                const QString& readbackMethod, const QJsonArray& readbackArgs);
  void onResponse(const QString& operation, quint64 requestId, bool ok,
                   const QJsonObject& data, const QString& errorCode,
                   const QString& errorText);
  void scheduleChanged();
  void applyPresetState(const QJsonObject& state, bool emitSnapshot);
  void sendPresetOperation(const QString& operation, const QJsonObject& data,
                           int presetId = -1,
                           const QString& exportPath = QString());
  void dispatchNextPresetOperation();
  void finishRemoteApply(bool ok, const QString& message);

  QPointer<RemoteWorkbenchSession> remote_;
  QHash<QString, QVariant> values_;
  QVector<GetRequest> pendingGets_;
  QHash<QString, SetRequest> pendingSets_;
  QVector<QString> pendingSetOrder_;
  QTimer* getTimer_ = nullptr;
  QTimer* setTimer_ = nullptr;
  QTimer* changedTimer_ = nullptr;
  bool snapshotRequested_ = false;
  QList<ParameterPresetInfo> presets_;
  int currentPresetId_ = 0;
  QHash<quint64, QString> presetRequests_;
  QQueue<PresetRequest> presetRequestQueue_;
  quint64 activePresetRequestId_ = 0;
  QString activePresetOperation_;
  int activePresetId_ = -1;
  QString activeExportPath_;
  QHash<quint64, QPair<int, double>> scanPrecisionRequests_;
  QHash<quint64, QPair<double, int>> encoderCalibrationRequests_;
  bool applyCommitPending_ = false;
  int pendingApplyBatches_ = 0;
  int applyFailureCount_ = 0;
  QStringList applyErrors_;
  int applyPresetId_ = -1;
  bool commitRequested_ = false;
};

}  // namespace pa1664::remote

