from pathlib import Path
p=Path('src/backend/robot_host_service.cpp')
s=p.read_text(encoding='utf-8')
old="""                            QJsonObject{
                                {QStringLiteral("port"), result.port},
                                {QStringLiteral("bitrate"), result.bitrate},
                                {QStringLiteral("success"), result.success},
                                {QStringLiteral("message"), result.message}});
"""
new="""                            QJsonObject{
                                {QStringLiteral("wheelDetected"),
                                 result.wheelDetected},
                                {QStringLiteral("wheelCanPort"),
                                 result.wheelCanPort},
                                {QStringLiteral("wheelCanBitrate"),
                                 result.wheelCanBitrate},
                                {QStringLiteral("details"),
                                 result.details.join(QStringLiteral(" | "))}});
"""
if old not in s: raise SystemExit('can pattern missing')
s=s.replace(old,new,1)
s=s.replace("namespace {\nQString errorText(const QString& code, const QString& detail) {\n  return detail.isEmpty() ? code : detail;\n}\n}  // namespace\n\n",'',1)
with open(p,'w',encoding='utf-8',newline='\n') as f:f.write(s)
print('patched')
