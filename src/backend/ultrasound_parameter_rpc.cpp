#include "ultrasound_parameter_rpc.h"

#include "client.h"

#include <QJsonValue>
#include <QStringList>
#include <exception>

namespace pa1664::remote {
namespace {

bool failRead(QString* error, const QString& detail) {
  if (error) *error = detail;
  return false;
}

bool failWrite(QString* error, const QString& detail) {
  if (error) *error = detail;
  return false;
}

bool checkArgs(const QJsonArray& args, int expected, QString* error) {
  if (args.size() == expected) return true;
  if (error)
    *error = QStringLiteral("参数个数错误：期望 %1，收到 %2")
                 .arg(expected).arg(args.size());
  return false;
}

}  // namespace

bool UltrasoundParameterRpc::read(Client& client, const QString& method,
                                   const QJsonArray& args, QJsonValue* value,
                                   QString* error) {
  if (error) error->clear();
  if (!value) return failRead(error, QStringLiteral("读取结果指针为空"));
  try {
    if (method == QStringLiteral("getBeamAngleLinear")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getBeamAngleLinear(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getBeamAngleMax")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getBeamAngleMax(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getBeamAngleMin")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getBeamAngleMin(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getBeamAngleStep")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getBeamAngleStep(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getBeamAperture")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getBeamAperture(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getBeamElementStep")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getBeamElementStep(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getBeamFirstElement")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getBeamFirstElement(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getBeamLastElement")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getBeamLastElement(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getBeamMode")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getBeamMode(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getDualCrystalEnable")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getDualCrystalEnable(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getFilterHigh")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getFilterHigh(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getFilterLow")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getFilterLow(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getFocalPosition")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getFocalPosition(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getFocusMode")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getFocusMode(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getFrameRate")) {
      if (!checkArgs(args, 0, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getFrameRate()));
      return true;
    }
    if (method == QStringLiteral("getGain")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGain(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateAEnable")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateAEnable(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateAEnd")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateAEnd(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateAMeasureType")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getGateAMeasureType(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getGateAStart")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateAStart(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateASynchronMode")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getGateASynchronMode(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getGateAThreshold")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateAThreshold(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateBEnable")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateBEnable(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateBEnd")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateBEnd(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateBMeasureType")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getGateBMeasureType(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getGateBStart")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateBStart(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateBSynchronMode")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getGateBSynchronMode(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getGateBThreshold")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateBThreshold(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateCEnable")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateCEnable(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateCEnd")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateCEnd(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateCMeasureType")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getGateCMeasureType(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getGateCStart")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateCStart(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateCSynchronMode")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getGateCSynchronMode(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getGateCThreshold")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateCThreshold(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateIEnable")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateIEnable(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateIEnd")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateIEnd(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateIMeasureType")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getGateIMeasureType(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getGateIStart")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateIStart(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateISyncSample")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateISyncSample(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getGateISynchronMode")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getGateISynchronMode(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getGateIThreshold")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getGateIThreshold(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getMaxAmplitude")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getMaxAmplitude(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getPaVoltage")) {
      if (!checkArgs(args, 0, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getPaVoltage()));
      return true;
    }
    if (method == QStringLiteral("getPointQuantity")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getPointQuantity(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getProbeFrequency")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getProbeFrequency(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getProbePrimaryElements")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getProbePrimaryElements(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getProbePrimaryElementsPitch")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getProbePrimaryElementsPitch(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getProbeSecondaryElements")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getProbeSecondaryElements(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getProbeSecondaryElementsPitch")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getProbeSecondaryElementsPitch(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getPulseWidth")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getPulseWidth(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getRangeEnd")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getRangeEnd(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getRangeStart")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getRangeStart(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getReceiverReject")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getReceiverReject(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getReceptionChannel")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getReceptionChannel(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getRectifierMode")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getRectifierMode(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getReverseEnable")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getReverseEnable(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getScanMode")) {
      if (!checkArgs(args, 0, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getScanMode()));
      return true;
    }
    if (method == QStringLiteral("getScanType")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getScanType(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getScannerDirection")) {
      if (!checkArgs(args, 0, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getScannerDirection()));
      return true;
    }
    if (method == QStringLiteral("getTcgEnable")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getTcgEnable(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getTcgPointDepth")) {
      if (!checkArgs(args, 3, error)) return false;
      *value = QJsonValue(client.getTcgPointDepth(args.at(0).toInt(), args.at(1).toInt(), args.at(2).toInt()));
      return true;
    }
    if (method == QStringLiteral("getTcgPointDepthIndex")) {
      if (!checkArgs(args, 2, error)) return false;
      *value = QJsonValue(client.getTcgPointDepthIndex(args.at(0).toInt(), args.at(1).toInt()));
      return true;
    }
    if (method == QStringLiteral("getTcgPointGain")) {
      if (!checkArgs(args, 3, error)) return false;
      *value = QJsonValue(client.getTcgPointGain(args.at(0).toInt(), args.at(1).toInt(), args.at(2).toInt()));
      return true;
    }
    if (method == QStringLiteral("getTcgPointGainIndex")) {
      if (!checkArgs(args, 2, error)) return false;
      *value = QJsonValue(client.getTcgPointGainIndex(args.at(0).toInt(), args.at(1).toInt()));
      return true;
    }
    if (method == QStringLiteral("getTfmRecDelay")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getTfmRecDelay(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getThickness")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getThickness(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getTransmissionChannel")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getTransmissionChannel(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getUtVoltage")) {
      if (!checkArgs(args, 0, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getUtVoltage()));
      return true;
    }
    if (method == QStringLiteral("getVideoFilterMHz")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getVideoFilterMHz(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getVoltagePolarity")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(static_cast<int>(client.getVoltagePolarity(args.at(0).toInt())));
      return true;
    }
    if (method == QStringLiteral("getWedgeAngle")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeAngle(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeEnable")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeEnable(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeGCp")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeGCp(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeHeight")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeHeight(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeLength")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeLength(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeRoofAngle")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeRoofAngle(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeVelocity")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeVelocity(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeWidth")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeWidth(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeX")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeX(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWedgeZ")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWedgeZ(args.at(0).toInt()));
      return true;
    }
    if (method == QStringLiteral("getWorkpieceVelocity")) {
      if (!checkArgs(args, 1, error)) return false;
      *value = QJsonValue(client.getWorkpieceVelocity(args.at(0).toInt()));
      return true;
    }
  } catch (const std::exception& exception) {
    return failRead(error, QStringLiteral("SDK 读取 %1 异常：%2")
                               .arg(method, QString::fromLocal8Bit(exception.what())));
  } catch (...) {
    return failRead(error, QStringLiteral("SDK 读取 %1 抛出未知异常").arg(method));
  }
  return failRead(error, QStringLiteral("不支持的参数读取：%1").arg(method));
}

bool UltrasoundParameterRpc::write(Client& client, const QString& method,
                                    const QJsonArray& args, QString* error) {
  if (error) error->clear();
  try {
    if (method == QStringLiteral("setBeamAngleLinear")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamAngleLinear(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamAngleLinear"));
      }
      return true;
    }
    if (method == QStringLiteral("setBeamAngleMax")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamAngleMax(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamAngleMax"));
      }
      return true;
    }
    if (method == QStringLiteral("setBeamAngleMin")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamAngleMin(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamAngleMin"));
      }
      return true;
    }
    if (method == QStringLiteral("setBeamAngleStep")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamAngleStep(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamAngleStep"));
      }
      return true;
    }
    if (method == QStringLiteral("setBeamAperture")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamAperture(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamAperture"));
      }
      return true;
    }
    if (method == QStringLiteral("setBeamElementStep")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamElementStep(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamElementStep"));
      }
      return true;
    }
    if (method == QStringLiteral("setBeamFirstElement")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamFirstElement(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamFirstElement"));
      }
      return true;
    }
    if (method == QStringLiteral("setBeamLastElement")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamLastElement(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamLastElement"));
      }
      return true;
    }
    if (method == QStringLiteral("setBeamMode")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setBeamMode(static_cast<BeamMode>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setBeamMode"));
      }
      return true;
    }
    if (method == QStringLiteral("setDualCrystalEnable")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setDualCrystalEnable(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setDualCrystalEnable"));
      }
      return true;
    }
    if (method == QStringLiteral("setFilterHigh")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setFilterHigh(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setFilterHigh"));
      }
      return true;
    }
    if (method == QStringLiteral("setFilterLow")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setFilterLow(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setFilterLow"));
      }
      return true;
    }
    if (method == QStringLiteral("setFocalPosition")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setFocalPosition(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setFocalPosition"));
      }
      return true;
    }
    if (method == QStringLiteral("setFocusMode")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setFocusMode(static_cast<FocalType>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setFocusMode"));
      }
      return true;
    }
    if (method == QStringLiteral("setFrameRate")) {
      if (!checkArgs(args, 1, error)) return false;
      if (!client.setFrameRate(args.at(0).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setFrameRate"));
      }
      return true;
    }
    if (method == QStringLiteral("setGain")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGain(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGain"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateAEnable")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateAEnable(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateAEnable"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateAEnd")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateAEnd(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateAEnd"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateAMeasureType")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateAMeasureType(static_cast<measureType>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateAMeasureType"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateAStart")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateAStart(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateAStart"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateASynchronMode")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateASynchronMode(static_cast<GateSynchron>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateASynchronMode"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateAThreshold")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateAThreshold(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateAThreshold"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateBEnable")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateBEnable(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateBEnable"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateBEnd")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateBEnd(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateBEnd"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateBMeasureType")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateBMeasureType(static_cast<measureType>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateBMeasureType"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateBStart")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateBStart(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateBStart"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateBSynchronMode")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateBSynchronMode(static_cast<GateSynchron>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateBSynchronMode"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateBThreshold")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateBThreshold(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateBThreshold"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateCEnable")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateCEnable(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateCEnable"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateCEnd")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateCEnd(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateCEnd"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateCMeasureType")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateCMeasureType(static_cast<measureType>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateCMeasureType"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateCStart")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateCStart(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateCStart"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateCSynchronMode")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateCSynchronMode(static_cast<GateSynchron>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateCSynchronMode"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateCThreshold")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateCThreshold(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateCThreshold"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateIEnable")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateIEnable(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateIEnable"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateIEnd")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateIEnd(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateIEnd"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateIMeasureType")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateIMeasureType(static_cast<measureType>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateIMeasureType"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateIStart")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateIStart(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateIStart"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateISyncSample")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateISyncSample(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateISyncSample"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateISynchronMode")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateISynchronMode(static_cast<GateSynchron>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateISynchronMode"));
      }
      return true;
    }
    if (method == QStringLiteral("setGateIThreshold")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setGateIThreshold(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setGateIThreshold"));
      }
      return true;
    }
    if (method == QStringLiteral("setMaxAmplitude")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setMaxAmplitude(static_cast<MaxAmplitude>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setMaxAmplitude"));
      }
      return true;
    }
    if (method == QStringLiteral("setPaVoltage")) {
      if (!checkArgs(args, 1, error)) return false;
      if (!client.setPaVoltage(args.at(0).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setPaVoltage"));
      }
      return true;
    }
    if (method == QStringLiteral("setPointQuantity")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setPointQuantity(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setPointQuantity"));
      }
      return true;
    }
    if (method == QStringLiteral("setProbeFrequency")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setProbeFrequency(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setProbeFrequency"));
      }
      return true;
    }
    if (method == QStringLiteral("setProbePrimaryElements")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setProbePrimaryElements(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setProbePrimaryElements"));
      }
      return true;
    }
    if (method == QStringLiteral("setProbePrimaryElementsPitch")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setProbePrimaryElementsPitch(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setProbePrimaryElementsPitch"));
      }
      return true;
    }
    if (method == QStringLiteral("setProbeSecondaryElements")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setProbeSecondaryElements(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setProbeSecondaryElements"));
      }
      return true;
    }
    if (method == QStringLiteral("setProbeSecondaryElementsPitch")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setProbeSecondaryElementsPitch(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setProbeSecondaryElementsPitch"));
      }
      return true;
    }
    if (method == QStringLiteral("setPulseWidth")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setPulseWidth(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setPulseWidth"));
      }
      return true;
    }
    if (method == QStringLiteral("setRangeEnd")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setRangeEnd(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setRangeEnd"));
      }
      return true;
    }
    if (method == QStringLiteral("setRangeStart")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setRangeStart(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setRangeStart"));
      }
      return true;
    }
    if (method == QStringLiteral("setReceiverReject")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setReceiverReject(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setReceiverReject"));
      }
      return true;
    }
    if (method == QStringLiteral("setReceptionChannel")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setReceptionChannel(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setReceptionChannel"));
      }
      return true;
    }
    if (method == QStringLiteral("setRectifierMode")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setRectifierMode(static_cast<RectifierType>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setRectifierMode"));
      }
      return true;
    }
    if (method == QStringLiteral("setReverseEnable")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setReverseEnable(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setReverseEnable"));
      }
      return true;
    }
    if (method == QStringLiteral("setScanMode")) {
      if (!checkArgs(args, 1, error)) return false;
      if (!client.setScanMode(args.at(0).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setScanMode"));
      }
      return true;
    }
    if (method == QStringLiteral("setScanType")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setScanType(static_cast<scanType>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setScanType"));
      }
      return true;
    }
    if (method == QStringLiteral("setScannerDirection")) {
      if (!checkArgs(args, 1, error)) return false;
      if (!client.setScannerDirection(static_cast<ScannerDir>(args.at(0).toInt()))) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setScannerDirection"));
      }
      return true;
    }
    if (method == QStringLiteral("setTcgEnable")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setTcgEnable(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setTcgEnable"));
      }
      return true;
    }
    if (method == QStringLiteral("setTcgPointDepth")) {
      if (!checkArgs(args, 4, error)) return false;
      if (!client.setTcgPointDepth(args.at(0).toInt(), args.at(1).toDouble(), args.at(2).toInt(), args.at(3).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setTcgPointDepth"));
      }
      return true;
    }
    if (method == QStringLiteral("setTcgPointDepthIndex")) {
      if (!checkArgs(args, 3, error)) return false;
      if (!client.setTcgPointDepthIndex(args.at(0).toInt(), args.at(1).toDouble(), args.at(2).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setTcgPointDepthIndex"));
      }
      return true;
    }
    if (method == QStringLiteral("setTcgPointGain")) {
      if (!checkArgs(args, 4, error)) return false;
      if (!client.setTcgPointGain(args.at(0).toInt(), args.at(1).toDouble(), args.at(2).toInt(), args.at(3).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setTcgPointGain"));
      }
      return true;
    }
    if (method == QStringLiteral("setTcgPointGainIndex")) {
      if (!checkArgs(args, 3, error)) return false;
      if (!client.setTcgPointGainIndex(args.at(0).toInt(), args.at(1).toDouble(), args.at(2).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setTcgPointGainIndex"));
      }
      return true;
    }
    if (method == QStringLiteral("setTfmRecDelay")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setTfmRecDelay(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setTfmRecDelay"));
      }
      return true;
    }
    if (method == QStringLiteral("setThickness")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setThickness(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setThickness"));
      }
      return true;
    }
    if (method == QStringLiteral("setTransmissionChannel")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setTransmissionChannel(args.at(0).toInt(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setTransmissionChannel"));
      }
      return true;
    }
    if (method == QStringLiteral("setUtVoltage")) {
      if (!checkArgs(args, 1, error)) return false;
      if (!client.setUtVoltage(args.at(0).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setUtVoltage"));
      }
      return true;
    }
    if (method == QStringLiteral("setVideoFilterMHz")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setVideoFilterMHz(static_cast<VideoFilter>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setVideoFilterMHz"));
      }
      return true;
    }
    if (method == QStringLiteral("setVoltagePolarity")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setVoltagePolarity(static_cast<PolarityType>(args.at(0).toInt()), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setVoltagePolarity"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeAngle")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeAngle(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeAngle"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeEnable")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeEnable(args.at(0).toBool(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeEnable"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeGCp")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeGCp(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeGCp"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeHeight")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeHeight(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeHeight"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeLength")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeLength(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeLength"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeRoofAngle")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeRoofAngle(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeRoofAngle"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeVelocity")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeVelocity(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeVelocity"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeWidth")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeWidth(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeWidth"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeX")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeX(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeX"));
      }
      return true;
    }
    if (method == QStringLiteral("setWedgeZ")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWedgeZ(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWedgeZ"));
      }
      return true;
    }
    if (method == QStringLiteral("setWorkpieceVelocity")) {
      if (!checkArgs(args, 2, error)) return false;
      if (!client.setWorkpieceVelocity(args.at(0).toDouble(), args.at(1).toInt())) {
        return failWrite(error, QStringLiteral("SDK 拒绝参数 setWorkpieceVelocity"));
      }
      return true;
    }
  } catch (const std::exception& exception) {
    return failWrite(error, QStringLiteral("SDK 写入 %1 异常：%2")
                               .arg(method, QString::fromLocal8Bit(exception.what())));
  } catch (...) {
    return failWrite(error, QStringLiteral("SDK 写入 %1 抛出未知异常").arg(method));
  }
  return failWrite(error, QStringLiteral("不支持的参数写入：%1").arg(method));
}

QStringList UltrasoundParameterRpc::getterNames() {
  return QStringList{
      QStringLiteral("getBeamAngleLinear"),
      QStringLiteral("getBeamAngleMax"),
      QStringLiteral("getBeamAngleMin"),
      QStringLiteral("getBeamAngleStep"),
      QStringLiteral("getBeamAperture"),
      QStringLiteral("getBeamElementStep"),
      QStringLiteral("getBeamFirstElement"),
      QStringLiteral("getBeamLastElement"),
      QStringLiteral("getBeamMode"),
      QStringLiteral("getDualCrystalEnable"),
      QStringLiteral("getFilterHigh"),
      QStringLiteral("getFilterLow"),
      QStringLiteral("getFocalPosition"),
      QStringLiteral("getFocusMode"),
      QStringLiteral("getFrameRate"),
      QStringLiteral("getGain"),
      QStringLiteral("getGateAEnable"),
      QStringLiteral("getGateAEnd"),
      QStringLiteral("getGateAMeasureType"),
      QStringLiteral("getGateAStart"),
      QStringLiteral("getGateASynchronMode"),
      QStringLiteral("getGateAThreshold"),
      QStringLiteral("getGateBEnable"),
      QStringLiteral("getGateBEnd"),
      QStringLiteral("getGateBMeasureType"),
      QStringLiteral("getGateBStart"),
      QStringLiteral("getGateBSynchronMode"),
      QStringLiteral("getGateBThreshold"),
      QStringLiteral("getGateCEnable"),
      QStringLiteral("getGateCEnd"),
      QStringLiteral("getGateCMeasureType"),
      QStringLiteral("getGateCStart"),
      QStringLiteral("getGateCSynchronMode"),
      QStringLiteral("getGateCThreshold"),
      QStringLiteral("getGateIEnable"),
      QStringLiteral("getGateIEnd"),
      QStringLiteral("getGateIMeasureType"),
      QStringLiteral("getGateIStart"),
      QStringLiteral("getGateISyncSample"),
      QStringLiteral("getGateISynchronMode"),
      QStringLiteral("getGateIThreshold"),
      QStringLiteral("getMaxAmplitude"),
      QStringLiteral("getPaVoltage"),
      QStringLiteral("getPointQuantity"),
      QStringLiteral("getProbeFrequency"),
      QStringLiteral("getProbePrimaryElements"),
      QStringLiteral("getProbePrimaryElementsPitch"),
      QStringLiteral("getProbeSecondaryElements"),
      QStringLiteral("getProbeSecondaryElementsPitch"),
      QStringLiteral("getPulseWidth"),
      QStringLiteral("getRangeEnd"),
      QStringLiteral("getRangeStart"),
      QStringLiteral("getReceiverReject"),
      QStringLiteral("getReceptionChannel"),
      QStringLiteral("getRectifierMode"),
      QStringLiteral("getReverseEnable"),
      QStringLiteral("getScanMode"),
      QStringLiteral("getScanType"),
      QStringLiteral("getScannerDirection"),
      QStringLiteral("getTcgEnable"),
      QStringLiteral("getTcgPointDepth"),
      QStringLiteral("getTcgPointDepthIndex"),
      QStringLiteral("getTcgPointGain"),
      QStringLiteral("getTcgPointGainIndex"),
      QStringLiteral("getTfmRecDelay"),
      QStringLiteral("getThickness"),
      QStringLiteral("getTransmissionChannel"),
      QStringLiteral("getUtVoltage"),
      QStringLiteral("getVideoFilterMHz"),
      QStringLiteral("getVoltagePolarity"),
      QStringLiteral("getWedgeAngle"),
      QStringLiteral("getWedgeEnable"),
      QStringLiteral("getWedgeGCp"),
      QStringLiteral("getWedgeHeight"),
      QStringLiteral("getWedgeLength"),
      QStringLiteral("getWedgeRoofAngle"),
      QStringLiteral("getWedgeVelocity"),
      QStringLiteral("getWedgeWidth"),
      QStringLiteral("getWedgeX"),
      QStringLiteral("getWedgeZ"),
      QStringLiteral("getWorkpieceVelocity")
  };
}

QJsonArray UltrasoundParameterRpc::defaultArguments(const QString& method) {
  if (method == QStringLiteral("getBeamAngleLinear")) return QJsonArray{0};
  if (method == QStringLiteral("getBeamAngleMax")) return QJsonArray{0};
  if (method == QStringLiteral("getBeamAngleMin")) return QJsonArray{0};
  if (method == QStringLiteral("getBeamAngleStep")) return QJsonArray{0};
  if (method == QStringLiteral("getBeamAperture")) return QJsonArray{0};
  if (method == QStringLiteral("getBeamElementStep")) return QJsonArray{0};
  if (method == QStringLiteral("getBeamFirstElement")) return QJsonArray{0};
  if (method == QStringLiteral("getBeamLastElement")) return QJsonArray{0};
  if (method == QStringLiteral("getBeamMode")) return QJsonArray{0};
  if (method == QStringLiteral("getDualCrystalEnable")) return QJsonArray{0};
  if (method == QStringLiteral("getFilterHigh")) return QJsonArray{0};
  if (method == QStringLiteral("getFilterLow")) return QJsonArray{0};
  if (method == QStringLiteral("getFocalPosition")) return QJsonArray{0};
  if (method == QStringLiteral("getFocusMode")) return QJsonArray{0};
  if (method == QStringLiteral("getFrameRate")) return QJsonArray{};
  if (method == QStringLiteral("getGain")) return QJsonArray{0};
  if (method == QStringLiteral("getGateAEnable")) return QJsonArray{0};
  if (method == QStringLiteral("getGateAEnd")) return QJsonArray{0};
  if (method == QStringLiteral("getGateAMeasureType")) return QJsonArray{0};
  if (method == QStringLiteral("getGateAStart")) return QJsonArray{0};
  if (method == QStringLiteral("getGateASynchronMode")) return QJsonArray{0};
  if (method == QStringLiteral("getGateAThreshold")) return QJsonArray{0};
  if (method == QStringLiteral("getGateBEnable")) return QJsonArray{0};
  if (method == QStringLiteral("getGateBEnd")) return QJsonArray{0};
  if (method == QStringLiteral("getGateBMeasureType")) return QJsonArray{0};
  if (method == QStringLiteral("getGateBStart")) return QJsonArray{0};
  if (method == QStringLiteral("getGateBSynchronMode")) return QJsonArray{0};
  if (method == QStringLiteral("getGateBThreshold")) return QJsonArray{0};
  if (method == QStringLiteral("getGateCEnable")) return QJsonArray{0};
  if (method == QStringLiteral("getGateCEnd")) return QJsonArray{0};
  if (method == QStringLiteral("getGateCMeasureType")) return QJsonArray{0};
  if (method == QStringLiteral("getGateCStart")) return QJsonArray{0};
  if (method == QStringLiteral("getGateCSynchronMode")) return QJsonArray{0};
  if (method == QStringLiteral("getGateCThreshold")) return QJsonArray{0};
  if (method == QStringLiteral("getGateIEnable")) return QJsonArray{0};
  if (method == QStringLiteral("getGateIEnd")) return QJsonArray{0};
  if (method == QStringLiteral("getGateIMeasureType")) return QJsonArray{0};
  if (method == QStringLiteral("getGateIStart")) return QJsonArray{0};
  if (method == QStringLiteral("getGateISyncSample")) return QJsonArray{0};
  if (method == QStringLiteral("getGateISynchronMode")) return QJsonArray{0};
  if (method == QStringLiteral("getGateIThreshold")) return QJsonArray{0};
  if (method == QStringLiteral("getMaxAmplitude")) return QJsonArray{0};
  if (method == QStringLiteral("getPaVoltage")) return QJsonArray{};
  if (method == QStringLiteral("getPointQuantity")) return QJsonArray{0};
  if (method == QStringLiteral("getProbeFrequency")) return QJsonArray{0};
  if (method == QStringLiteral("getProbePrimaryElements")) return QJsonArray{0};
  if (method == QStringLiteral("getProbePrimaryElementsPitch")) return QJsonArray{0};
  if (method == QStringLiteral("getProbeSecondaryElements")) return QJsonArray{0};
  if (method == QStringLiteral("getProbeSecondaryElementsPitch")) return QJsonArray{0};
  if (method == QStringLiteral("getPulseWidth")) return QJsonArray{0};
  if (method == QStringLiteral("getRangeEnd")) return QJsonArray{0};
  if (method == QStringLiteral("getRangeStart")) return QJsonArray{0};
  if (method == QStringLiteral("getReceiverReject")) return QJsonArray{0};
  if (method == QStringLiteral("getReceptionChannel")) return QJsonArray{0};
  if (method == QStringLiteral("getRectifierMode")) return QJsonArray{0};
  if (method == QStringLiteral("getReverseEnable")) return QJsonArray{0};
  if (method == QStringLiteral("getScanMode")) return QJsonArray{};
  if (method == QStringLiteral("getScanType")) return QJsonArray{0};
  if (method == QStringLiteral("getScannerDirection")) return QJsonArray{};
  if (method == QStringLiteral("getTcgEnable")) return QJsonArray{0};
  if (method == QStringLiteral("getTcgPointDepth")) return QJsonArray{0, 0, 0};
  if (method == QStringLiteral("getTcgPointDepthIndex")) return QJsonArray{0, 0};
  if (method == QStringLiteral("getTcgPointGain")) return QJsonArray{0, 0, 0};
  if (method == QStringLiteral("getTcgPointGainIndex")) return QJsonArray{0, 0};
  if (method == QStringLiteral("getTfmRecDelay")) return QJsonArray{0};
  if (method == QStringLiteral("getThickness")) return QJsonArray{0};
  if (method == QStringLiteral("getTransmissionChannel")) return QJsonArray{0};
  if (method == QStringLiteral("getUtVoltage")) return QJsonArray{};
  if (method == QStringLiteral("getVideoFilterMHz")) return QJsonArray{0};
  if (method == QStringLiteral("getVoltagePolarity")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeAngle")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeEnable")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeGCp")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeHeight")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeLength")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeRoofAngle")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeVelocity")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeWidth")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeX")) return QJsonArray{0};
  if (method == QStringLiteral("getWedgeZ")) return QJsonArray{0};
  if (method == QStringLiteral("getWorkpieceVelocity")) return QJsonArray{0};
  return {};
}

}  // namespace pa1664::remote
