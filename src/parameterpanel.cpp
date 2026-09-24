#include "parameterpanel.h"

#include "client.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QStringList>
#include <exception>

ParameterPanel::ParameterPanel(QWidget *parent) : QWidget(parent)
{
    m_autoApplyTimer = new QTimer(this);
    m_autoApplyTimer->setSingleShot(true);
    m_autoApplyTimer->setInterval(600);
    connect(m_autoApplyTimer, &QTimer::timeout, this, &ParameterPanel::apply);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    auto *groupRow = new QHBoxLayout;
    groupRow->addWidget(new QLabel(QStringLiteral("工作组")));
    m_group = new QComboBox;
    groupRow->addWidget(m_group, 1);
    auto *copy = new QPushButton(QStringLiteral("复制组"));
    auto *remove = new QPushButton(QStringLiteral("删除组"));
    groupRow->addWidget(copy);
    groupRow->addWidget(remove);
    root->addLayout(groupRow);
    auto *groupHint = new QLabel(QStringLiteral(
        "下方组内参数仅作用于下拉框中的当前工作组；PA/UT 电压、PRF、全局扫查模式和编码器方向为设备全局参数。"));
    groupHint->setWordWrap(true);
    groupHint->setStyleSheet(QStringLiteral("color:#52606d;padding:0 2px 4px 2px"));
    root->addWidget(groupHint);

    connect(m_group, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_group->currentData().isValid()) {
            const int requestedGroup = m_group->currentData().toInt();
            try {
                if (!Client::getInstance().setCurrentGroup(requestedGroup)) {
                    emit message(QStringLiteral("切换到工作组 %1 失败，通道参数未切换")
                                     .arg(requestedGroup), true);
                    refreshGroups();
                    return;
                }
                reload();
                emit groupChanged();
            } catch (const std::exception &e) {
                emit message(QStringLiteral("切换到工作组 %1 时 SDK 异常：%2")
                                 .arg(requestedGroup)
                                 .arg(QString::fromLocal8Bit(e.what())), true);
                refreshGroups();
            } catch (...) {
                emit message(QStringLiteral("切换到工作组 %1 时 SDK 抛出未知异常")
                                 .arg(requestedGroup), true);
                refreshGroups();
            }
        }
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        const int sourceGroup = selectedGroupId();
        const bool ok = Client::getInstance().copyGroup(sourceGroup);
        refreshGroups();
        emit message(ok ? QStringLiteral("工作组已复制") : QStringLiteral("复制工作组失败"), !ok);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        auto &sdk = Client::getInstance();
        const auto groups = sdk.getGroupsNo(true);
        if (groups.size() <= 1) {
            emit message(QStringLiteral("至少必须保留一个工作组，不能继续删除"), true);
            return;
        }
        const int group = m_group->currentData().toInt();
        const bool ok = sdk.removeGroup(group);
        refreshGroups();
        emit message(ok ? QStringLiteral("工作组已删除") : QStringLiteral("删除工作组失败（必须至少保留一组）"), !ok);
    });

    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    root->addWidget(tabs, 1);

    auto *general = new QFormLayout;
    addDouble(general, QStringLiteral("增益"), 0, 120, 2,
              [this] { return Client::getInstance().getGain(selectedGroupId()); },
              [this](double v) { return Client::getInstance().setGain(v, selectedGroupId()); }, QStringLiteral(" dB"));
    addDouble(general, QStringLiteral("范围起点"), 0, 10000, 2,
              [this] { return Client::getInstance().getRangeStart(selectedGroupId()); },
              [this](double v) { return Client::getInstance().setRangeStart(v, selectedGroupId()); }, QStringLiteral(" mm"));
    addDouble(general, QStringLiteral("范围终点"), 0.01, 10000, 2,
              [this] { return Client::getInstance().getRangeEnd(selectedGroupId()); },
              [this](double v) { return Client::getInstance().setRangeEnd(v, selectedGroupId()); }, QStringLiteral(" mm"));
    addDouble(general, QStringLiteral("工件声速"), 100, 20000, 1,
              [this] { return Client::getInstance().getWorkpieceVelocity(selectedGroupId()); },
              [this](double v) { return Client::getInstance().setWorkpieceVelocity(v, selectedGroupId()); }, QStringLiteral(" m/s"));
    addInt(general, QStringLiteral("PA 电压"), 0, 1000,
           [] { return Client::getInstance().getPaVoltage(); },
           [](int v) { return Client::getInstance().setPaVoltage(v); }, QStringLiteral(" V"));
    addInt(general, QStringLiteral("UT 电压"), 0, 1000,
           [] { return Client::getInstance().getUtVoltage(); },
           [](int v) { return Client::getInstance().setUtVoltage(v); }, QStringLiteral(" V"));
    addInt(general, QStringLiteral("脉冲宽度"), 0, 10000,
           [this] { return Client::getInstance().getPulseWidth(selectedGroupId()); },
           [this](int v) { return Client::getInstance().setPulseWidth(v, selectedGroupId()); }, QStringLiteral(" ns"));
    addEnum(general, QStringLiteral("电压极性"), {{QStringLiteral("负脉冲"),0},{QStringLiteral("正脉冲"),1},{QStringLiteral("全脉冲"),2}},
            [this] { return int(Client::getInstance().getVoltagePolarity(selectedGroupId())); },
            [this](int v) { return Client::getInstance().setVoltagePolarity(PolarityType(v), selectedGroupId()); });
    addDouble(general, QStringLiteral("高通滤波"), 0, 100, 3,
              [this] { return Client::getInstance().getFilterHigh(selectedGroupId()); },
              [this](double v) { return Client::getInstance().setFilterHigh(v, selectedGroupId()); }, QStringLiteral(" MHz"));
    addDouble(general, QStringLiteral("低通滤波"), 0, 100, 3,
              [this] { return Client::getInstance().getFilterLow(selectedGroupId()); },
              [this](double v) { return Client::getInstance().setFilterLow(v, selectedGroupId()); }, QStringLiteral(" MHz"));
    addEnum(general, QStringLiteral("图像滤波"), {{"1 MHz",1000},{"2 MHz",2000},{"3 MHz",3000},{"4 MHz",4000},{"5 MHz",5000},{"6 MHz",6000},{"7 MHz",7000},{QStringLiteral("无"),8000}},
            [this] { return int(Client::getInstance().getVideoFilterMHz(selectedGroupId())); },
            [this](int v) { return Client::getInstance().setVideoFilterMHz(VideoFilter(v), selectedGroupId()); });
    addEnum(general, QStringLiteral("检波模式"), {{QStringLiteral("射频"),0},{QStringLiteral("全波"),1},{QStringLiteral("正半波"),2},{QStringLiteral("负半波"),3}},
            [this] { return int(Client::getInstance().getRectifierMode(selectedGroupId())); },
            [this](int v) { return Client::getInstance().setRectifierMode(RectifierType(v), selectedGroupId()); });
    addInt(general, QStringLiteral("采样点数"), 8, 65536,
           [this] { return Client::getInstance().getPointQuantity(selectedGroupId()); },
           [this](int v) { return Client::getInstance().setPointQuantity(v, selectedGroupId()); });
    addDouble(general, QStringLiteral("零点延迟"), -100000, 100000, 2,
              [this] { return Client::getInstance().getTfmRecDelay(selectedGroupId()); },
              [this](double v) { return Client::getInstance().setTfmRecDelay(v, selectedGroupId()); }, QStringLiteral(" ns"));
    addEnum(general, QStringLiteral("最大幅值"), {{"1600%",2048},{"800%",4096},{"400%",8192},{"200%",16384}},
            [this] { return int(Client::getInstance().getMaxAmplitude(selectedGroupId())); },
            [this](int v) { return Client::getInstance().setMaxAmplitude(MaxAmplitude(v), selectedGroupId()); });
    addInt(general, QStringLiteral("接收抑制"), 0, 100,
           [this] { return Client::getInstance().getReceiverReject(selectedGroupId()); },
           [this](int v) { return Client::getInstance().setReceiverReject(v, selectedGroupId()); }, QStringLiteral(" %"));
    addInt(general, QStringLiteral("采集帧率 PRF"), 1, 100000,
           [] { return Client::getInstance().getFrameRate(); },
           [](int v) { return Client::getInstance().setFrameRate(v); }, QStringLiteral(" Hz"));
    addInt(general, QStringLiteral("全局扫查模式"), 0, 100,
           [] { return Client::getInstance().getScanMode(); },
           [](int v) { return Client::getInstance().setScanMode(v); });
    tabs->addTab(pageFor(general), QStringLiteral("基本"));

    auto *beam = new QFormLayout;
    addEnum(beam, QStringLiteral("扫查类型"), {{QStringLiteral("线性扫"),1},{QStringLiteral("扇形扫"),2},{QStringLiteral("超声显微"),3},{QStringLiteral("常规 UT"),4}},
            [this] { return int(Client::getInstance().getScanType(selectedGroupId())); },
            [this](int v) { return Client::getInstance().setScanType(scanType(v), selectedGroupId()); });
    addEnum(beam, QStringLiteral("波束模式"), {{QStringLiteral("单波束"),0},{QStringLiteral("双波束"),1},{QStringLiteral("四波束"),2}},
            [this] { return int(Client::getInstance().getBeamMode(selectedGroupId())); },
            [this](int v) { return Client::getInstance().setBeamMode(BeamMode(v), selectedGroupId()); });
    addInt(beam, QStringLiteral("孔径"), 1, 128, [this] { return Client::getInstance().getBeamAperture(selectedGroupId()); }, [this](int v) { return Client::getInstance().setBeamAperture(v, selectedGroupId()); });
    addInt(beam, QStringLiteral("阵元步进"), 1, 128, [this] { return Client::getInstance().getBeamElementStep(selectedGroupId()); }, [this](int v) { return Client::getInstance().setBeamElementStep(v, selectedGroupId()); });
    addDouble(beam, QStringLiteral("角度步进"), 0.01, 180, 2, [this] { return Client::getInstance().getBeamAngleStep(selectedGroupId()); }, [this](double v) { return Client::getInstance().setBeamAngleStep(v, selectedGroupId()); }, QStringLiteral("°"));
    addInt(beam, QStringLiteral("首阵元"), 1, 1024, [this] { return Client::getInstance().getBeamFirstElement(selectedGroupId()); }, [this](int v) { return Client::getInstance().setBeamFirstElement(v, selectedGroupId()); });
    addInt(beam, QStringLiteral("末阵元"), 1, 1024, [this] { return Client::getInstance().getBeamLastElement(selectedGroupId()); }, [this](int v) { return Client::getInstance().setBeamLastElement(v, selectedGroupId()); });
    addDouble(beam, QStringLiteral("最小角"), -90, 90, 2, [this] { return Client::getInstance().getBeamAngleMin(selectedGroupId()); }, [this](double v) { return Client::getInstance().setBeamAngleMin(v, selectedGroupId()); }, QStringLiteral("°"));
    addDouble(beam, QStringLiteral("最大角"), -90, 90, 2, [this] { return Client::getInstance().getBeamAngleMax(selectedGroupId()); }, [this](double v) { return Client::getInstance().setBeamAngleMax(v, selectedGroupId()); }, QStringLiteral("°"));
    addDouble(beam, QStringLiteral("线性角"), -90, 90, 2, [this] { return Client::getInstance().getBeamAngleLinear(selectedGroupId()); }, [this](double v) { return Client::getInstance().setBeamAngleLinear(v, selectedGroupId()); }, QStringLiteral("°"));
    addEnum(beam, QStringLiteral("聚焦模式"), {{QStringLiteral("真深度"),0},{QStringLiteral("等声程"),1},{QStringLiteral("水平投影"),2},{QStringLiteral("不聚焦"),3}},
            [this] { return int(Client::getInstance().getFocusMode(selectedGroupId())); }, [this](int v) { return Client::getInstance().setFocusMode(FocalType(v), selectedGroupId()); });
    addDouble(beam, QStringLiteral("聚焦位置"), 0, 10000, 2, [this] { return Client::getInstance().getFocalPosition(selectedGroupId()); }, [this](double v) { return Client::getInstance().setFocalPosition(v, selectedGroupId()); }, QStringLiteral(" mm"));
    tabs->addTab(pageFor(beam), QStringLiteral("相控阵"));

    auto *probe = new QFormLayout;
    // PA-1664 当前 USB SDK 的公开 TX/RX 接口使用 1..8。配置 JSON 中可能
    // 出现 0，那是未初始化/内部序列化值，不能作为公开 API 的通道编号。
    // 真机回读已确认：向接口写 0 会被 SDK 修正为 1。
    const QList<QPair<QString, int>> utChannels = {
        {QStringLiteral("通道 1"), 1}, {QStringLiteral("通道 2"), 2},
        {QStringLiteral("通道 3"), 3}, {QStringLiteral("通道 4"), 4},
        {QStringLiteral("通道 5"), 5}, {QStringLiteral("通道 6"), 6},
        {QStringLiteral("通道 7"), 7}, {QStringLiteral("通道 8"), 8}
    };
    auto *channelHint = new QLabel(QStringLiteral(
        "当前工作组的一激一收通道。界面通道与 SDK 公开接口均为 1–8；"
        "激励与接收可选同一路，也可选不同路。"));
    channelHint->setWordWrap(true);
    channelHint->setStyleSheet(QStringLiteral("color:#52606d;padding:4px 0 8px 0"));
    probe->addRow(channelHint);
    addEnum(probe, QStringLiteral("激励通道（TX）"), utChannels,
            [this] { return Client::getInstance().getTransmissionChannel(selectedGroupId()); },
            [this](int v) { return Client::getInstance().setTransmissionChannel(v, selectedGroupId()); });
    addEnum(probe, QStringLiteral("接收通道（RX）"), utChannels,
            [this] { return Client::getInstance().getReceptionChannel(selectedGroupId()); },
            [this](int v) { return Client::getInstance().setReceptionChannel(v, selectedGroupId()); });
    addDouble(probe, QStringLiteral("探头频率"), 0.01, 100, 3, [this] { return Client::getInstance().getProbeFrequency(selectedGroupId()); }, [this](double v) { return Client::getInstance().setProbeFrequency(v, selectedGroupId()); }, QStringLiteral(" MHz"));
    addBool(probe, QStringLiteral("双晶探头"), [this] { return Client::getInstance().getDualCrystalEnable(selectedGroupId()); }, [this](bool v) { return Client::getInstance().setDualCrystalEnable(v, selectedGroupId()); });
    addBool(probe, QStringLiteral("反向安装"), [this] { return Client::getInstance().getReverseEnable(selectedGroupId()); }, [this](bool v) { return Client::getInstance().setReverseEnable(v, selectedGroupId()); });
    addInt(probe, QStringLiteral("主轴阵元数"), 1, 1024, [this] { return Client::getInstance().getProbePrimaryElements(selectedGroupId()); }, [this](int v) { return Client::getInstance().setProbePrimaryElements(v, selectedGroupId()); });
    addDouble(probe, QStringLiteral("主轴间距"), 0.001, 100, 3, [this] { return Client::getInstance().getProbePrimaryElementsPitch(selectedGroupId()); }, [this](double v) { return Client::getInstance().setProbePrimaryElementsPitch(v, selectedGroupId()); }, QStringLiteral(" mm"));
    addInt(probe, QStringLiteral("次轴阵元数"), 1, 1024, [this] { return Client::getInstance().getProbeSecondaryElements(selectedGroupId()); }, [this](int v) { return Client::getInstance().setProbeSecondaryElements(v, selectedGroupId()); });
    addDouble(probe, QStringLiteral("次轴间距"), 0.001, 100, 3, [this] { return Client::getInstance().getProbeSecondaryElementsPitch(selectedGroupId()); }, [this](double v) { return Client::getInstance().setProbeSecondaryElementsPitch(v, selectedGroupId()); }, QStringLiteral(" mm"));
    tabs->addTab(pageFor(probe), QStringLiteral("探头"));

    auto *wedge = new QFormLayout;
    addBool(wedge, QStringLiteral("启用楔块"), [this] { return Client::getInstance().getWedgeEnable(selectedGroupId()); }, [this](bool v) { return Client::getInstance().setWedgeEnable(v, selectedGroupId()); });
    addDouble(wedge, QStringLiteral("楔块角度"), -90, 90, 3, [this] { return Client::getInstance().getWedgeAngle(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeAngle(v, selectedGroupId()); }, QStringLiteral("°"));
    addDouble(wedge, QStringLiteral("楔块声速"), 100, 20000, 1, [this] { return Client::getInstance().getWedgeVelocity(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeVelocity(v, selectedGroupId()); }, QStringLiteral(" m/s"));
    addDouble(wedge, "X", -10000, 10000, 3, [this] { return Client::getInstance().getWedgeX(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeX(v, selectedGroupId()); }, QStringLiteral(" mm"));
    addDouble(wedge, "Z", -10000, 10000, 3, [this] { return Client::getInstance().getWedgeZ(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeZ(v, selectedGroupId()); }, QStringLiteral(" mm"));
    addDouble(wedge, QStringLiteral("屋顶角"), -90, 90, 3, [this] { return Client::getInstance().getWedgeRoofAngle(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeRoofAngle(v, selectedGroupId()); }, QStringLiteral("°"));
    addDouble(wedge, "GCP", -10000, 10000, 3, [this] { return Client::getInstance().getWedgeGCp(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeGCp(v, selectedGroupId()); }, QStringLiteral(" mm"));
    addDouble(wedge, QStringLiteral("长度"), 0, 10000, 3, [this] { return Client::getInstance().getWedgeLength(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeLength(v, selectedGroupId()); }, QStringLiteral(" mm"));
    addDouble(wedge, QStringLiteral("宽度"), 0, 10000, 3, [this] { return Client::getInstance().getWedgeWidth(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeWidth(v, selectedGroupId()); }, QStringLiteral(" mm"));
    addDouble(wedge, QStringLiteral("高度"), 0, 10000, 3, [this] { return Client::getInstance().getWedgeHeight(selectedGroupId()); }, [this](double v) { return Client::getInstance().setWedgeHeight(v, selectedGroupId()); }, QStringLiteral(" mm"));
    tabs->addTab(pageFor(wedge), QStringLiteral("楔块"));

    addGateTab(tabs);
    addTcgTab(tabs);

    auto *other = new QFormLayout;
    addEnum(other, QStringLiteral("编码器方向"), {{QStringLiteral("正向"),0},{QStringLiteral("反向"),1}},
            [] { return int(Client::getInstance().getScannerDirection()); }, [](int v) { return Client::getInstance().setScannerDirection(ScannerDir(v)); });
    addDouble(other, QStringLiteral("工件厚度"), 0, 100000, 3,
              [this] { return Client::getInstance().getThickness(selectedGroupId()); },
              [this](double v) { return Client::getInstance().setThickness(v, selectedGroupId()); }, QStringLiteral(" mm"));
    tabs->addTab(pageFor(other), QStringLiteral("编码器"));

    QTimer::singleShot(0, this, &ParameterPanel::refreshGroups);
}

QWidget *ParameterPanel::pageFor(QFormLayout *form)
{
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *body = new QWidget;
    body->setLayout(form);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    return scroll;
}

void ParameterPanel::addDouble(QFormLayout *form, const QString &name, double min, double max,
                               int decimals, const std::function<double()> &getter,
                               const std::function<bool(double)> &setter, const QString &suffix)
{
    auto *w = new QDoubleSpinBox;
    w->setProperty("parameterName", name);
    w->setRange(min, max); w->setDecimals(decimals); w->setSuffix(suffix); w->setKeyboardTracking(false);
    form->addRow(name, w);
    m_fields.push_back({name, w, [getter] { return getter(); }, [setter](const QVariant &v) { return setter(v.toDouble()); }, {}});
    connect(w, &QDoubleSpinBox::valueChanged, this, &ParameterPanel::scheduleAutoApply);
}

void ParameterPanel::addInt(QFormLayout *form, const QString &name, int min, int max,
                            const std::function<int()> &getter,
                            const std::function<bool(int)> &setter, const QString &suffix)
{
    auto *w = new QSpinBox;
    w->setProperty("parameterName", name);
    w->setRange(min, max); w->setSuffix(suffix); w->setKeyboardTracking(false);
    form->addRow(name, w);
    m_fields.push_back({name, w, [getter] { return getter(); }, [setter](const QVariant &v) { return setter(v.toInt()); }, {}});
    connect(w, &QSpinBox::valueChanged, this, &ParameterPanel::scheduleAutoApply);
}

void ParameterPanel::addBool(QFormLayout *form, const QString &name,
                             const std::function<bool()> &getter,
                             const std::function<bool(bool)> &setter)
{
    auto *w = new QCheckBox(QStringLiteral("启用"));
    w->setProperty("parameterName", name);
    form->addRow(name, w);
    m_fields.push_back({name, w, [getter] { return getter(); }, [setter](const QVariant &v) { return setter(v.toBool()); }, {}});
    connect(w, &QCheckBox::toggled, this, &ParameterPanel::scheduleAutoApply);
}

void ParameterPanel::addEnum(QFormLayout *form, const QString &name,
                             const QList<QPair<QString, int>> &items,
                             const std::function<int()> &getter,
                             const std::function<bool(int)> &setter)
{
    auto *w = new QComboBox;
    w->setProperty("parameterName", name);
    for (const auto &item : items) w->addItem(item.first, item.second);
    form->addRow(name, w);
    m_fields.push_back({name, w, [getter] { return getter(); }, [setter](const QVariant &v) { return setter(v.toInt()); }, {}});
    connect(w, &QComboBox::currentIndexChanged, this, &ParameterPanel::scheduleAutoApply);
}

void ParameterPanel::scheduleAutoApply()
{
    if (!m_applying)
        m_autoApplyTimer->start();
}

void ParameterPanel::addGateTab(QTabWidget *tabs)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *gateTabs = new QTabWidget;
    layout->addWidget(gateTabs);

    struct GateApi {
        QString name;
        std::function<bool()> enabled; std::function<bool(bool)> setEnabled;
        std::function<int()> sync; std::function<bool(int)> setSync;
        std::function<double()> start; std::function<bool(double)> setStart;
        std::function<double()> end; std::function<bool(double)> setEnd;
        std::function<double()> threshold; std::function<bool(double)> setThreshold;
        std::function<int()> measure; std::function<bool(int)> setMeasure;
    };
    const QVector<GateApi> gates = {
        {"A", [this]{return Client::getInstance().getGateAEnable(selectedGroupId());}, [this](bool v){return Client::getInstance().setGateAEnable(v, selectedGroupId());}, [this]{return int(Client::getInstance().getGateASynchronMode(selectedGroupId()));}, [this](int v){return Client::getInstance().setGateASynchronMode(GateSynchron(v), selectedGroupId());}, [this]{return Client::getInstance().getGateAStart(selectedGroupId());}, [this](double v){return Client::getInstance().setGateAStart(v, selectedGroupId());}, [this]{return Client::getInstance().getGateAEnd(selectedGroupId());}, [this](double v){return Client::getInstance().setGateAEnd(v, selectedGroupId());}, [this]{return Client::getInstance().getGateAThreshold(selectedGroupId());}, [this](double v){return Client::getInstance().setGateAThreshold(v, selectedGroupId());}, [this]{return int(Client::getInstance().getGateAMeasureType(selectedGroupId()));}, [this](int v){return Client::getInstance().setGateAMeasureType(measureType(v), selectedGroupId());}},
        {"B", [this]{return Client::getInstance().getGateBEnable(selectedGroupId());}, [this](bool v){return Client::getInstance().setGateBEnable(v, selectedGroupId());}, [this]{return int(Client::getInstance().getGateBSynchronMode(selectedGroupId()));}, [this](int v){return Client::getInstance().setGateBSynchronMode(GateSynchron(v), selectedGroupId());}, [this]{return Client::getInstance().getGateBStart(selectedGroupId());}, [this](double v){return Client::getInstance().setGateBStart(v, selectedGroupId());}, [this]{return Client::getInstance().getGateBEnd(selectedGroupId());}, [this](double v){return Client::getInstance().setGateBEnd(v, selectedGroupId());}, [this]{return Client::getInstance().getGateBThreshold(selectedGroupId());}, [this](double v){return Client::getInstance().setGateBThreshold(v, selectedGroupId());}, [this]{return int(Client::getInstance().getGateBMeasureType(selectedGroupId()));}, [this](int v){return Client::getInstance().setGateBMeasureType(measureType(v), selectedGroupId());}},
        {"C", [this]{return Client::getInstance().getGateCEnable(selectedGroupId());}, [this](bool v){return Client::getInstance().setGateCEnable(v, selectedGroupId());}, [this]{return int(Client::getInstance().getGateCSynchronMode(selectedGroupId()));}, [this](int v){return Client::getInstance().setGateCSynchronMode(GateSynchron(v), selectedGroupId());}, [this]{return Client::getInstance().getGateCStart(selectedGroupId());}, [this](double v){return Client::getInstance().setGateCStart(v, selectedGroupId());}, [this]{return Client::getInstance().getGateCEnd(selectedGroupId());}, [this](double v){return Client::getInstance().setGateCEnd(v, selectedGroupId());}, [this]{return Client::getInstance().getGateCThreshold(selectedGroupId());}, [this](double v){return Client::getInstance().setGateCThreshold(v, selectedGroupId());}, [this]{return int(Client::getInstance().getGateCMeasureType(selectedGroupId()));}, [this](int v){return Client::getInstance().setGateCMeasureType(measureType(v), selectedGroupId());}},
        {"I", [this]{return Client::getInstance().getGateIEnable(selectedGroupId());}, [this](bool v){return Client::getInstance().setGateIEnable(v, selectedGroupId());}, [this]{return int(Client::getInstance().getGateISynchronMode(selectedGroupId()));}, [this](int v){return Client::getInstance().setGateISynchronMode(GateSynchron(v), selectedGroupId());}, [this]{return Client::getInstance().getGateIStart(selectedGroupId());}, [this](double v){return Client::getInstance().setGateIStart(v, selectedGroupId());}, [this]{return Client::getInstance().getGateIEnd(selectedGroupId());}, [this](double v){return Client::getInstance().setGateIEnd(v, selectedGroupId());}, [this]{return Client::getInstance().getGateIThreshold(selectedGroupId());}, [this](double v){return Client::getInstance().setGateIThreshold(v, selectedGroupId());}, [this]{return int(Client::getInstance().getGateIMeasureType(selectedGroupId()));}, [this](int v){return Client::getInstance().setGateIMeasureType(measureType(v), selectedGroupId());}}
    };
    for (const auto &g : gates) {
        auto *form = new QFormLayout;
        const int base = m_fields.size();
        addBool(form, QStringLiteral("使能"), g.enabled, g.setEnabled);
        addEnum(form, QStringLiteral("同步"), {{QStringLiteral("激励"),0},{"Gate I",1},{"Gate A",2},{"Gate B",3}}, g.sync, g.setSync);
        addDouble(form, QStringLiteral("起点"), 0, 10000, 3, g.start, g.setStart, QStringLiteral(" mm"));
        addDouble(form, QStringLiteral("终点"), 0, 10000, 3, g.end, g.setEnd, QStringLiteral(" mm"));
        addDouble(form, QStringLiteral("阈值"), 0, 100, 2, g.threshold, g.setThreshold, QStringLiteral(" %"));
        addEnum(form, QStringLiteral("测量方式"), {{QStringLiteral("最大峰值"),0},{QStringLiteral("波前"),1}}, g.measure, g.setMeasure);
        for (int i = base; i < m_fields.size(); ++i) m_gateEditors.push_back(m_fields[i].editor);
        if (g.name == "I")
            addBool(form, QStringLiteral("同步采集"), [this]{return Client::getInstance().getGateISyncSample(selectedGroupId());}, [this](bool v){return Client::getInstance().setGateISyncSample(v, selectedGroupId());});
        gateTabs->addTab(pageFor(form), "Gate " + g.name);
    }
    tabs->addTab(page, QStringLiteral("闸门"));
}

void ParameterPanel::addTcgTab(QTabWidget *tabs)
{
    auto *form = new QFormLayout;
    addBool(form, QStringLiteral("启用 TCG"), [this]{return Client::getInstance().getTcgEnable(selectedGroupId());}, [this](bool v){return Client::getInstance().setTcgEnable(v, selectedGroupId());});
    auto *scope = new QComboBox;
    scope->addItem(QStringLiteral("整组（所有声束）"), 0);
    scope->addItem(QStringLiteral("指定声束"), 1);
    form->addRow(QStringLiteral("编辑范围"), scope);
    auto *beam = new QSpinBox;
    beam->setRange(0, 4095);
    form->addRow(QStringLiteral("声束索引（从 0 开始）"), beam);
    connect(scope, &QComboBox::currentIndexChanged, beam, [scope, beam] { beam->setEnabled(scope->currentData().toInt() == 1); });
    connect(scope, &QComboBox::currentIndexChanged, this, &ParameterPanel::reload);
    connect(beam, &QSpinBox::editingFinished, this, &ParameterPanel::reload);
    beam->setEnabled(false);
    for (int i = 1; i <= 10; ++i) {
        addDouble(form, QStringLiteral("点 %1 深度").arg(i), 0, 100000, 3,
                  [this, i, scope, beam]{ return scope->currentData().toInt() == 0
                      ? Client::getInstance().getTcgPointDepthIndex(i, selectedGroupId())
                      : Client::getInstance().getTcgPointDepth(i, beam->value(), selectedGroupId()); },
                  [this, i, scope, beam](double v){ return scope->currentData().toInt() == 0
                      ? Client::getInstance().setTcgPointDepthIndex(i, v, selectedGroupId())
                      : Client::getInstance().setTcgPointDepth(i, v, beam->value(), selectedGroupId()); }, QStringLiteral(" mm"));
        addDouble(form, QStringLiteral("点 %1 增益").arg(i), -120, 120, 2,
                  [this, i, scope, beam]{ return scope->currentData().toInt() == 0
                      ? Client::getInstance().getTcgPointGainIndex(i, selectedGroupId())
                      : Client::getInstance().getTcgPointGain(i, beam->value(), selectedGroupId()); },
                  [this, i, scope, beam](double v){ return scope->currentData().toInt() == 0
                      ? Client::getInstance().setTcgPointGainIndex(i, v, selectedGroupId())
                      : Client::getInstance().setTcgPointGain(i, v, beam->value(), selectedGroupId()); }, QStringLiteral(" dB"));
    }
    tabs->addTab(pageFor(form), "TCG");
}

int ParameterPanel::selectedGroupId() const
{
    if (m_group && m_group->currentData().isValid())
        return m_group->currentData().toInt();
    return Client::getInstance().getCurrentGroup();
}

void ParameterPanel::refreshGroups()
{
    const QSignalBlocker blocker(m_group);
    const int current = Client::getInstance().getCurrentGroup();
    m_group->clear();
    // 与官方 SDK 保持一致：只列“使能”的工作组（flag=true）。原用 false 会
    // 把已删除/未使能的组也塞进下拉框，导致选到无效组后几何参数错乱。
    auto groups = Client::getInstance().getGroupsNo(true);
    if (groups.isEmpty()) groups.push_back(current > 0 ? current : 1);
    for (int group : groups) m_group->addItem(QString::number(group), group);
    const int index = m_group->findData(current);
    m_group->setCurrentIndex(index >= 0 ? index : 0);
    reload();
    // 删除/复制/加载组之后，当前组可能已经改变；必须刷新设备几何
    // （点数、波束数、组偏移），否则解码用的是旧几何，A 扫无信号。
    emit groupChanged();
}

void ParameterPanel::reload()
{
    for (auto &field : m_fields) {
        const QSignalBlocker blocker(field.editor);
        const QVariant value = field.getter();
        if (auto *w = qobject_cast<QDoubleSpinBox *>(field.editor)) w->setValue(value.toDouble());
        else if (auto *w = qobject_cast<QSpinBox *>(field.editor)) w->setValue(value.toInt());
        else if (auto *w = qobject_cast<QCheckBox *>(field.editor)) w->setChecked(value.toBool());
        else if (auto *w = qobject_cast<QComboBox *>(field.editor)) {
            const int index = w->findData(value);
            if (index >= 0) w->setCurrentIndex(index);
        }
        if (auto *w = qobject_cast<QDoubleSpinBox *>(field.editor)) field.loadedValue = w->value();
        else if (auto *w = qobject_cast<QSpinBox *>(field.editor)) field.loadedValue = w->value();
        else if (auto *w = qobject_cast<QCheckBox *>(field.editor)) field.loadedValue = w->isChecked();
        else if (auto *w = qobject_cast<QComboBox *>(field.editor)) field.loadedValue = w->currentData();
    }
    emit configurationChanged();
}

void ParameterPanel::apply()
{
    if (m_applying)
        return;

    struct PendingChange {
        Field *field = nullptr;
        QVariant value;
    };
    QVector<PendingChange> changes;
    changes.reserve(m_fields.size());
    for (auto &field : m_fields) {
        QVariant value;
        if (auto *w = qobject_cast<QDoubleSpinBox *>(field.editor)) value = w->value();
        else if (auto *w = qobject_cast<QSpinBox *>(field.editor)) value = w->value();
        else if (auto *w = qobject_cast<QCheckBox *>(field.editor)) value = w->isChecked();
        else if (auto *w = qobject_cast<QComboBox *>(field.editor)) value = w->currentData();
        if (value != field.loadedValue)
            changes.push_back({&field, value});
    }

    if (changes.isEmpty())
        return;

    m_applying = true;
    emit applyStarted();

    int failed = 0;
    int applied = 0;
    QStringList errors;
    for (auto &change : changes) {
        auto &field = *change.field;
        const QVariant &value = change.value;
        bool ok = false;
        try {
            ok = field.setter(value);
            if (ok && (field.name == QStringLiteral("激励通道（TX）")
                       || field.name == QStringLiteral("接收通道（RX）"))) {
                const int readBack = field.getter().toInt();
                if (readBack != value.toInt()) {
                    ok = false;
                    errors << QStringLiteral("%1：写入通道 %2 后回读为通道 %3")
                                  .arg(field.name).arg(value.toInt()).arg(readBack);
                }
            }
        } catch (const std::exception &e) {
            errors << QStringLiteral("%1：SDK 异常 %2").arg(field.name, QString::fromLocal8Bit(e.what()));
        } catch (...) {
            errors << QStringLiteral("%1：SDK 抛出未知异常").arg(field.name);
        }
        if (ok) {
            field.loadedValue = value;
            ++applied;
        } else {
            ++failed;
            if (errors.isEmpty() || !errors.last().startsWith(field.name))
                errors << QStringLiteral("%1：SDK 拒绝该值").arg(field.name);
        }
    }
    // 写入完成后按 SDK 的实际配置重新计算有效 PRF，这样修改孔径/步进/声束数量时也能准确限制。
    int actualFrameRate = Client::getInstance().getFrameRate();
    qint64 totalBeams = 0;
    for (int group : Client::getInstance().getGroupsNo(true)) {
        try {
            const int beams = Client::getInstance().getBeamCounts(group);
            if (beams > 0)
                totalBeams += beams;
        } catch (...) {
            // Disabled SDK tombstones have no complete group parameter map.
        }
    }
    totalBeams = qMax<qint64>(1, totalBeams);
    const qint64 effectivePrf = qint64(actualFrameRate) * totalBeams;
    QString prfAdjustment;
    // 12848 来自 PA-1664 真机 SDK 日志。超过后固件会先出少量帧再停流，
    // 因此这里必须限制实际下发值，而不能只显示警告。
    if (effectivePrf > 12848) {
        const int safeFrameRate = qMax(1, int(12848 / totalBeams));
        bool limited = false;
        try {
            limited = Client::getInstance().setFrameRate(safeFrameRate);
        } catch (...) {
            limited = false;
        }
        if (limited) {
            for (auto &field : m_fields) {
                if (field.name != QStringLiteral("采集帧率 PRF"))
                    continue;
                if (auto *editor = qobject_cast<QSpinBox *>(field.editor)) {
                    const QSignalBlocker blocker(editor);
                    editor->setValue(safeFrameRate);
                }
                field.loadedValue = safeFrameRate;
                break;
            }
            prfAdjustment = QStringLiteral("；为防止 USB 停流，PRF 已由 %1 Hz 自动限制为 %2 Hz"
                                           "（总 beam %3，有效上限 12848）")
                                .arg(actualFrameRate).arg(safeFrameRate).arg(totalBeams);
            actualFrameRate = safeFrameRate;
        } else {
            ++failed;
            errors << QStringLiteral("采集帧率 PRF：有效值 %1 超过硬件上限 12848，且自动限制失败")
                          .arg(effectivePrf);
        }
    }

    m_applying = false;
    // 每次只下发实际改变的字段，随后立即以 SDK 的真实值覆盖界面，
    // 避免设备端限幅或修正后界面仍显示旧输入值。
    reload();
    emit applyFinished();
    if (failed == 0) {
        emit message(QStringLiteral("已自动下发 %1 项修改并从 SDK 回读%2")
                         .arg(applied).arg(prfAdjustment), false);
    } else {
        emit message(QStringLiteral("已应用 %1 项，%2 项失败：%3")
                         .arg(applied).arg(failed).arg(errors.join(QStringLiteral("；"))), true);
    }
}

QVector<QVector<double>> ParameterPanel::currentGates() const
{
    QVector<QVector<double>> result;
    for (int gate = 0; gate < 4; ++gate) {
        const int base = gate * 6;
        if (base + 4 >= m_gateEditors.size()) break;
        auto *enabled = qobject_cast<QCheckBox *>(m_gateEditors[base]);
        auto *start = qobject_cast<QDoubleSpinBox *>(m_gateEditors[base + 2]);
        auto *end = qobject_cast<QDoubleSpinBox *>(m_gateEditors[base + 3]);
        auto *threshold = qobject_cast<QDoubleSpinBox *>(m_gateEditors[base + 4]);
        result.push_back({enabled && enabled->isChecked() ? 1.0 : 0.0,
                          start ? start->value() : 0.0, end ? end->value() : 0.0,
                          threshold ? threshold->value() : 0.0});
    }
    return result;
}
