#include "mainwindow.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

namespace {
QString stateText(bool online)
{
    return online ? QStringLiteral("在线") : QStringLiteral("离线");
}

QString stateStyle(bool online)
{
    return online
        ? QStringLiteral("color:#36d399;background:#123b33;border-radius:8px;padding:3px 8px;")
        : QStringLiteral("color:#fda4af;background:#471d28;border-radius:8px;padding:3px 8px;");
}

QString onlineText(bool online, const QString &offlineReason = {})
{
    if (online) return QStringLiteral("在线");
    return offlineReason.isEmpty()
        ? QStringLiteral("离线")
        : QStringLiteral("离线（%1）").arg(offlineReason);
}

QString formatJsonForLog(const QJsonObject &object, const QString &messageId)
{
    const QString device = object.value(QStringLiteral("device_id")).toString(QStringLiteral("--"));
    const int sequence = object.value(QStringLiteral("sequence")).toInt();
    if (object.contains(QStringLiteral("temperature_c")) ||
        object.contains(QStringLiteral("aht20_online"))) {
        return QStringLiteral(
            "MQTT发布    遥测数据    消息ID %1\n"
            "            设备 %2    序号 %3\n"
            "            AHT20 %4    温度 %5 °C    湿度 %6 %RH\n"
            "            INA219 %7    母线电压 %8 V    电流 %9 mA    功率 %10 mW")
            .arg(messageId, device)
            .arg(sequence)
            .arg(onlineText(object.value(QStringLiteral("aht20_online")).toBool()))
            .arg(object.value(QStringLiteral("temperature_c")).toDouble(), 0, 'f', 1)
            .arg(object.value(QStringLiteral("humidity_percent")).toDouble(), 0, 'f', 1)
            .arg(onlineText(object.value(QStringLiteral("ina219_online")).toBool(),
                            QStringLiteral("未接负载")))
            .arg(object.value(QStringLiteral("bus_voltage_v")).toDouble(), 0, 'f', 3)
            .arg(object.value(QStringLiteral("current_ma")).toDouble(), 0, 'f', 0)
            .arg(object.value(QStringLiteral("power_mw")).toDouble(), 0, 'f', 0);
    }
    if (object.contains(QStringLiteral("register0")) ||
        object.contains(QStringLiteral("slave"))) {
        return QStringLiteral(
            "MQTT发布    Modbus状态    消息ID %1\n"
            "            设备 %2    序号 %3    状态 %4    从站 %5\n"
            "            寄存器0 %6    寄存器1 %7    成功 %8    错误 %9")
            .arg(messageId, device)
            .arg(sequence)
            .arg(onlineText(object.value(QStringLiteral("online")).toBool(),
                            QStringLiteral("从机未连接")))
            .arg(object.value(QStringLiteral("slave")).toInt())
            .arg(object.value(QStringLiteral("register0")).toInt())
            .arg(object.value(QStringLiteral("register1")).toInt())
            .arg(object.value(QStringLiteral("success_count")).toVariant().toLongLong())
            .arg(object.value(QStringLiteral("error_count")).toVariant().toLongLong());
    }
    return QString();
}

QString formatLogForDisplay(const QString &line)
{
    QString message = line;
    static const QRegularExpression prefix(
        QStringLiteral("^[VDIWE] \\((\\d+)\\) ([^:]+):\\s*"));
    const QRegularExpressionMatch prefixMatch = prefix.match(message);
    QString component;
    if (prefixMatch.hasMatch()) {
        component = prefixMatch.captured(2);
        message.remove(0, prefixMatch.capturedLength());
    }

    QRegularExpressionMatch match = QRegularExpression(
        QStringLiteral("^STM32 HB: seq=(\\d+)$")).match(message);
    if (match.hasMatch()) {
        return QStringLiteral("心跳        STM32 → ESP32        序号 %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral("^ACK TX: ESP,ACK,(\\d+)$")).match(message);
    if (match.hasMatch()) {
        return QStringLiteral("应答        ESP32 → STM32        序号 %1").arg(match.captured(1));
    }

    match = QRegularExpression(QStringLiteral(
        "^DATA seq=(\\d+) AHT=(\\d+) T=(-?\\d+) H=(\\d+) INA=(\\d+) V=(\\d+) I=(-?\\d+) P=(\\d+)$"))
        .match(message);
    if (match.hasMatch()) {
        const bool ahtOnline = match.captured(2).toInt() != 0;
        const bool inaOnline = match.captured(5).toInt() != 0;
        return QStringLiteral(
            "传感器      序号 %1\n"
            "            AHT20 %2    温度 %3 °C    湿度 %4 %RH\n"
            "            INA219 %5    母线电压 %6 V    电流 %7 mA    功率 %8 mW")
            .arg(match.captured(1))
            .arg(onlineText(ahtOnline))
            .arg(match.captured(3).toDouble() / 10.0, 0, 'f', 1)
            .arg(match.captured(4).toDouble() / 10.0, 0, 'f', 1)
            .arg(onlineText(inaOnline, QStringLiteral("未接负载")))
            .arg(match.captured(6).toDouble() / 1000.0, 0, 'f', 3)
            .arg(match.captured(7))
            .arg(match.captured(8));
    }

    match = QRegularExpression(QStringLiteral(
        "^MODBUS seq=(\\d+) online=(\\d+) slave=(\\d+) r0=(-?\\d+) r1=(-?\\d+) ok=(\\d+) err=(\\d+)$"))
        .match(message);
    if (match.hasMatch()) {
        return QStringLiteral(
            "Modbus     序号 %1    状态 %2    从站 %3\n"
            "            寄存器0 %4    寄存器1 %5    成功 %6    错误 %7")
            .arg(match.captured(1))
            .arg(onlineText(match.captured(2).toInt() != 0, QStringLiteral("从机未连接")))
            .arg(match.captured(3), match.captured(4), match.captured(5),
                 match.captured(6), match.captured(7));
    }

    const qsizetype payloadIndex = message.indexOf(QStringLiteral("payload="));
    if (message.startsWith(QStringLiteral("MQTT TX ")) && payloadIndex >= 0) {
        const QRegularExpressionMatch idMatch = QRegularExpression(
            QStringLiteral("msg_id=(-?\\d+)")).match(message);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(
            message.mid(payloadIndex + 8).trimmed().toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            const QString formatted = formatJsonForLog(
                document.object(), idMatch.hasMatch() ? idMatch.captured(1) : QStringLiteral("--"));
            if (!formatted.isEmpty()) return formatted;
        }
    }

    if (message == QStringLiteral("MQTT connected")) return QStringLiteral("网络        MQTT已连接");
    if (message.startsWith(QStringLiteral("Wi-Fi connected, IP="))) {
        return QStringLiteral("网络        %1").arg(message);
    }
    return component.isEmpty() ? message : QStringLiteral("%1        %2").arg(component, message);
}

QLabel *makeText(const QString &text, const QString &objectName = {})
{
    auto *label = new QLabel(text);
    label->setObjectName(objectName);
    return label;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), serial_(new QSerialPort(this))
{
    buildUi();
    refreshPorts();

    connect(serial_, &QSerialPort::readyRead, this, [this] { readSerialData(); });
    connect(serial_, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError error) {
                if (error == QSerialPort::NoError) return;
                appendLog(QStringLiteral("串口错误：%1").arg(serial_->errorString()),
                          QStringLiteral("ERR"));
                if (serial_->isOpen()) serial_->close();
                setConnectionState(false, QStringLiteral("串口异常"));
            });
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("工业设备远程运维与故障诊断终端"));
    resize(1180, 760);
    setMinimumSize(980, 650);

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(16);

    auto *titleRow = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    auto *title = makeText(QStringLiteral("工业网关 · 运行监控"), QStringLiteral("pageTitle"));
    auto *subtitle = makeText(QStringLiteral("STM32 + FreeRTOS · ESP32 · RS485/Modbus · MQTT"),
                              QStringLiteral("subtitle"));
    titles->addWidget(title);
    titles->addWidget(subtitle);
    connectionBadge_ = makeText(QStringLiteral("未连接"));
    connectionBadge_->setAlignment(Qt::AlignCenter);
    connectionBadge_->setMinimumWidth(92);
    connectionBadge_->setStyleSheet(stateStyle(false));
    titleRow->addLayout(titles);
    titleRow->addStretch();
    titleRow->addWidget(connectionBadge_);
    root->addLayout(titleRow);

    auto *connectionFrame = new QFrame;
    connectionFrame->setObjectName(QStringLiteral("panel"));
    auto *connectionRow = new QHBoxLayout(connectionFrame);
    sourceBox_ = new QComboBox;
    sourceBox_->addItem(QStringLiteral("串口（ESP32 USB 日志）"));
    sourceBox_->addItem(QStringLiteral("MQTT（模块待接入）"));
    portBox_ = new QComboBox;
    baudBox_ = new QComboBox;
    baudBox_->addItems({QStringLiteral("115200"), QStringLiteral("9600")});
    auto *refreshButton = new QPushButton(QStringLiteral("刷新端口"));
    connectButton_ = new QPushButton(QStringLiteral("连接"));
    connectButton_->setObjectName(QStringLiteral("primaryButton"));
    auto *demoButton = new QPushButton(QStringLiteral("载入演示数据"));
    connectionRow->addWidget(makeText(QStringLiteral("数据源")));
    connectionRow->addWidget(sourceBox_, 2);
    connectionRow->addWidget(makeText(QStringLiteral("端口")));
    connectionRow->addWidget(portBox_, 1);
    connectionRow->addWidget(makeText(QStringLiteral("波特率")));
    connectionRow->addWidget(baudBox_);
    connectionRow->addWidget(refreshButton);
    connectionRow->addWidget(connectButton_);
    connectionRow->addWidget(demoButton);
    root->addWidget(connectionFrame);

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshPorts(); });
    connect(connectButton_, &QPushButton::clicked, this, [this] { toggleConnection(); });
    connect(demoButton, &QPushButton::clicked, this, [this] { loadDemoData(); });
    connect(sourceBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const bool serialMode = index == 0;
        portBox_->setEnabled(serialMode);
        baudBox_->setEnabled(serialMode);
        connectButton_->setEnabled(serialMode);
        if (!serialMode) statusBar()->showMessage(QStringLiteral("MQTT 数据源将在加入 Qt MQTT 后启用"), 5000);
    });

    auto *cards = new QGridLayout;
    cards->setHorizontalSpacing(12);
    temperatureCard_ = createCard(cards, 0, QStringLiteral("温度"), QStringLiteral("°C"), QStringLiteral("#38bdf8"));
    humidityCard_ = createCard(cards, 1, QStringLiteral("湿度"), QStringLiteral("%RH"), QStringLiteral("#a78bfa"));
    voltageCard_ = createCard(cards, 2, QStringLiteral("母线电压"), QStringLiteral("V"), QStringLiteral("#fbbf24"));
    currentCard_ = createCard(cards, 3, QStringLiteral("负载电流"), QStringLiteral("mA"), QStringLiteral("#fb7185"));
    powerCard_ = createCard(cards, 4, QStringLiteral("负载功率"), QStringLiteral("mW"), QStringLiteral("#34d399"));
    root->addLayout(cards);

    auto *splitter = new QSplitter(Qt::Horizontal);
    auto *statusPanel = new QFrame;
    statusPanel->setObjectName(QStringLiteral("panel"));
    auto *statusLayout = new QVBoxLayout(statusPanel);
    statusLayout->addWidget(makeText(QStringLiteral("设备与 Modbus 状态"), QStringLiteral("sectionTitle")));
    auto *deviceGrid = new QGridLayout;
    deviceLabel_ = makeText(QStringLiteral("--"), QStringLiteral("dataText"));
    sequenceLabel_ = makeText(QStringLiteral("--"), QStringLiteral("dataText"));
    lastUpdateLabel_ = makeText(QStringLiteral("尚未收到数据"), QStringLiteral("dataText"));
    modbusStateLabel_ = makeText(QStringLiteral("离线"));
    modbusStateLabel_->setStyleSheet(stateStyle(false));
    modbusDetailLabel_ = makeText(QStringLiteral("等待 Modbus 数据"), QStringLiteral("mutedText"));
    register0Label_ = makeText(QStringLiteral("--"), QStringLiteral("largeData"));
    register1Label_ = makeText(QStringLiteral("--"), QStringLiteral("largeData"));
    successLabel_ = makeText(QStringLiteral("0"), QStringLiteral("dataText"));
    errorLabel_ = makeText(QStringLiteral("0"), QStringLiteral("dataText"));
    deviceGrid->addWidget(makeText(QStringLiteral("设备 ID"), QStringLiteral("mutedText")), 0, 0);
    deviceGrid->addWidget(deviceLabel_, 0, 1);
    deviceGrid->addWidget(makeText(QStringLiteral("最新序号"), QStringLiteral("mutedText")), 1, 0);
    deviceGrid->addWidget(sequenceLabel_, 1, 1);
    deviceGrid->addWidget(makeText(QStringLiteral("更新时间"), QStringLiteral("mutedText")), 2, 0);
    deviceGrid->addWidget(lastUpdateLabel_, 2, 1);
    deviceGrid->addWidget(makeText(QStringLiteral("Modbus"), QStringLiteral("mutedText")), 3, 0);
    deviceGrid->addWidget(modbusStateLabel_, 3, 1);
    statusLayout->addLayout(deviceGrid);
    statusLayout->addWidget(modbusDetailLabel_);
    auto *registerGrid = new QGridLayout;
    registerGrid->addWidget(makeText(QStringLiteral("寄存器 0"), QStringLiteral("mutedText")), 0, 0);
    registerGrid->addWidget(makeText(QStringLiteral("寄存器 1"), QStringLiteral("mutedText")), 0, 1);
    registerGrid->addWidget(register0Label_, 1, 0);
    registerGrid->addWidget(register1Label_, 1, 1);
    registerGrid->addWidget(makeText(QStringLiteral("成功次数"), QStringLiteral("mutedText")), 2, 0);
    registerGrid->addWidget(makeText(QStringLiteral("错误次数"), QStringLiteral("mutedText")), 2, 1);
    registerGrid->addWidget(successLabel_, 3, 0);
    registerGrid->addWidget(errorLabel_, 3, 1);
    statusLayout->addLayout(registerGrid);
    statusLayout->addStretch();

    auto *logPanel = new QFrame;
    logPanel->setObjectName(QStringLiteral("panel"));
    auto *logLayout = new QVBoxLayout(logPanel);
    auto *logHeader = new QHBoxLayout;
    logHeader->addWidget(makeText(QStringLiteral("通信日志"), QStringLiteral("sectionTitle")));
    logHeader->addStretch();
    pauseLogButton_ = new QPushButton(QStringLiteral("暂停滚动"));
    auto *clearButton = new QPushButton(QStringLiteral("清空"));
    logHeader->addWidget(pauseLogButton_);
    logHeader->addWidget(clearButton);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(1500);
    logView_->setPlaceholderText(QStringLiteral("连接 ESP32 串口，或点击“载入演示数据”验证界面。"));
    logLayout->addLayout(logHeader);
    logLayout->addWidget(logView_);
    connect(pauseLogButton_, &QPushButton::clicked, this, [this] {
        logPaused_ = !logPaused_;
        if (logPaused_) {
            pauseLogButton_->setText(QStringLiteral("继续滚动"));
            statusBar()->showMessage(QStringLiteral("日志画面已暂停；串口仍在接收，新日志将暂存。"), 5000);
            return;
        }

        const QStringList pendingLines = pausedLogLines_;
        pausedLogLines_.clear();
        pauseLogButton_->setText(QStringLiteral("暂停滚动"));
        for (const QString &line : pendingLines) {
            logView_->appendPlainText(line);
        }
        logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
        statusBar()->showMessage(
            QStringLiteral("已继续滚动，并补入暂停期间的 %1 条日志。").arg(pendingLines.size()),
            5000);
    });
    connect(clearButton, &QPushButton::clicked, this, [this] {
        logView_->clear();
        pausedLogLines_.clear();
        pauseLogButton_->setText(logPaused_ ? QStringLiteral("继续滚动")
                                            : QStringLiteral("暂停滚动"));
    });

    splitter->addWidget(statusPanel);
    splitter->addWidget(logPanel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    root->addWidget(splitter, 1);

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("上位机就绪"));
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget { background:#0b1220; color:#e5edf7; }
        #pageTitle { font-size:24px; font-weight:700; color:#f8fafc; }
        #subtitle, #mutedText { color:#8091a7; }
        #sectionTitle { font-size:16px; font-weight:700; color:#f1f5f9; }
        #panel, #metricCard { background:#111c2e; border:1px solid #20304a; border-radius:12px; }
        #metricValue { font-size:28px; font-weight:700; color:#ffffff; }
        #metricUnit { color:#8da0b8; }
        #dataText { color:#dbeafe; font-weight:600; }
        #largeData { color:#7dd3fc; font-size:24px; font-weight:700; }
        QComboBox, QPlainTextEdit { background:#0d1728; border:1px solid #2b3b55; border-radius:7px; padding:7px; }
        QPushButton { background:#1c2b42; border:1px solid #334762; border-radius:7px; padding:7px 12px; }
        QPushButton:hover { background:#263954; }
        #primaryButton { background:#0284c7; border-color:#0ea5e9; font-weight:700; }
        QStatusBar { color:#8da0b8; }
    )"));
}

MainWindow::ValueCard MainWindow::createCard(QGridLayout *layout, int column,
                                              const QString &title, const QString &unit,
                                              const QString &accent)
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("metricCard"));
    auto *box = new QVBoxLayout(frame);
    auto *heading = makeText(title, QStringLiteral("mutedText"));
    heading->setStyleSheet(QStringLiteral("color:%1;font-weight:700;").arg(accent));
    auto *valueRow = new QHBoxLayout;
    auto *value = makeText(QStringLiteral("--"), QStringLiteral("metricValue"));
    auto *unitLabel = makeText(unit, QStringLiteral("metricUnit"));
    valueRow->addWidget(value);
    valueRow->addWidget(unitLabel, 0, Qt::AlignBottom);
    valueRow->addStretch();
    auto *state = makeText(QStringLiteral("传感器离线"), QStringLiteral("mutedText"));
    box->addWidget(heading);
    box->addLayout(valueRow);
    box->addWidget(state);
    layout->addWidget(frame, 0, column);
    layout->setColumnStretch(column, 1);
    return {value, state};
}

void MainWindow::refreshPorts()
{
    const QString selected = portBox_->currentData().toString();
    portBox_->clear();
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        const QString description = info.description().isEmpty()
            ? info.portName()
            : QStringLiteral("%1 · %2").arg(info.portName(), info.description());
        portBox_->addItem(description, info.portName());
    }
    const int previous = portBox_->findData(selected);
    if (previous >= 0) portBox_->setCurrentIndex(previous);
    if (portBox_->count() == 0) portBox_->addItem(QStringLiteral("未发现串口"));
}

void MainWindow::toggleConnection()
{
    if (serial_->isOpen()) {
        serial_->close();
        setConnectionState(false, QStringLiteral("已主动断开"));
        return;
    }

    const QString port = portBox_->currentData().toString();
    if (port.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先连接设备并刷新端口"), 5000);
        return;
    }
    serial_->setPortName(port);
    serial_->setBaudRate(baudBox_->currentText().toInt());
    serial_->setDataBits(QSerialPort::Data8);
    serial_->setParity(QSerialPort::NoParity);
    serial_->setStopBits(QSerialPort::OneStop);
    serial_->setFlowControl(QSerialPort::NoFlowControl);
    if (!serial_->open(QIODevice::ReadOnly)) {
        setConnectionState(false, serial_->errorString());
        appendLog(QStringLiteral("无法打开 %1：%2").arg(port, serial_->errorString()),
                  QStringLiteral("ERR"));
        return;
    }
    // Do not let the desktop serial client hold the ESP32 auto-reset lines.
    // On common DevKit boards an asserted DTR/RTS combination can keep GPIO0
    // low during reset and leave the chip in the ROM download bootloader.
    serial_->setDataTerminalReady(false);
    serial_->setRequestToSend(false);
    receiveBuffer_.clear();
    resetLiveDataView();
    setConnectionState(true, QStringLiteral("%1 @ %2").arg(port, baudBox_->currentText()));
}

void MainWindow::readSerialData()
{
    receiveBuffer_.append(serial_->readAll());
    while (true) {
        const qsizetype newline = receiveBuffer_.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = receiveBuffer_.left(newline).trimmed();
        receiveBuffer_.remove(0, newline + 1);
        if (!line.isEmpty()) processLine(QString::fromUtf8(line));
    }
    if (receiveBuffer_.size() > 16384) {
        receiveBuffer_.clear();
        appendLog(QStringLiteral("接收缓存溢出，已重新同步"), QStringLiteral("WARN"));
    }
}

void MainWindow::processLine(const QString &line)
{
    static const QRegularExpression ansiEscape(
        QStringLiteral("\\x1B\\[[0-?]*[ -/]*[@-~]"));
    QString cleanLine = line;
    cleanLine.remove(ansiEscape);
    cleanLine.remove(QRegularExpression(
        QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F\\x7F\\uFFFD]")));

    appendLog(formatLogForDisplay(cleanLine));
    const qsizetype payloadIndex = cleanLine.indexOf(QStringLiteral("payload="));
    if (payloadIndex >= 0) {
        processJson(cleanLine.mid(payloadIndex + 8).trimmed().toUtf8());
        return;
    }
    const qsizetype jsonStart = cleanLine.indexOf(QLatin1Char('{'));
    if (jsonStart >= 0 && processJson(cleanLine.mid(jsonStart).trimmed().toUtf8())) return;
    processGatewayFrame(cleanLine);
}

bool MainWindow::processJson(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("temperature_c")) ||
        object.contains(QStringLiteral("aht20_online"))) {
        applyTelemetry(object);
        return true;
    }
    if (object.contains(QStringLiteral("register0")) ||
        object.contains(QStringLiteral("slave"))) {
        applyModbus(object);
        return true;
    }
    return false;
}

bool MainWindow::processGatewayFrame(const QString &line)
{
    const QString frame = line.contains(QStringLiteral("GW,"))
        ? line.mid(line.indexOf(QStringLiteral("GW,"))) : line;
    const QStringList fields = frame.split(QLatin1Char(','));
    if (fields.size() == 10 && fields[0] == QStringLiteral("GW") &&
        fields[1] == QStringLiteral("DATA")) {
        QJsonObject object;
        object[QStringLiteral("sequence")] = fields[2].toInt();
        object[QStringLiteral("aht20_online")] = fields[3].toInt() != 0;
        object[QStringLiteral("temperature_c")] = fields[4].toDouble() / 10.0;
        object[QStringLiteral("humidity_percent")] = fields[5].toDouble() / 10.0;
        object[QStringLiteral("ina219_online")] = fields[6].toInt() != 0;
        object[QStringLiteral("bus_voltage_v")] = fields[7].toDouble() / 1000.0;
        object[QStringLiteral("current_ma")] = fields[8].toDouble();
        object[QStringLiteral("power_mw")] = fields[9].toDouble();
        applyTelemetry(object);
        return true;
    }
    if (fields.size() == 10 && fields[0] == QStringLiteral("GW") &&
        fields[1] == QStringLiteral("MB")) {
        QJsonObject object;
        object[QStringLiteral("sequence")] = fields[2].toInt();
        object[QStringLiteral("online")] = fields[3].toInt() != 0;
        object[QStringLiteral("slave")] = fields[4].toInt();
        object[QStringLiteral("exception")] = fields[5].toInt();
        object[QStringLiteral("register0")] = fields[6].toInt();
        object[QStringLiteral("register1")] = fields[7].toInt();
        object[QStringLiteral("success_count")] = fields[8].toDouble();
        object[QStringLiteral("error_count")] = fields[9].toDouble();
        applyModbus(object);
        return true;
    }
    return false;
}

void MainWindow::applyTelemetry(const QJsonObject &object)
{
    const bool ahtOnline = object.value(QStringLiteral("aht20_online")).toBool();
    const bool inaOnline = object.value(QStringLiteral("ina219_online")).toBool();
    temperatureCard_.value->setText(QString::number(object.value(QStringLiteral("temperature_c")).toDouble(), 'f', 1));
    humidityCard_.value->setText(QString::number(object.value(QStringLiteral("humidity_percent")).toDouble(), 'f', 1));
    voltageCard_.value->setText(QString::number(object.value(QStringLiteral("bus_voltage_v")).toDouble(), 'f', 2));
    currentCard_.value->setText(QString::number(object.value(QStringLiteral("current_ma")).toDouble(), 'f', 0));
    powerCard_.value->setText(QString::number(object.value(QStringLiteral("power_mw")).toDouble(), 'f', 0));
    temperatureCard_.state->setText(QStringLiteral("AHT20 %1").arg(stateText(ahtOnline)));
    humidityCard_.state->setText(QStringLiteral("AHT20 %1").arg(stateText(ahtOnline)));
    voltageCard_.state->setText(QStringLiteral("INA219 %1").arg(stateText(inaOnline)));
    currentCard_.state->setText(QStringLiteral("INA219 %1").arg(stateText(inaOnline)));
    powerCard_.state->setText(QStringLiteral("INA219 %1").arg(stateText(inaOnline)));
    deviceLabel_->setText(object.value(QStringLiteral("device_id")).toString(QStringLiteral("STM32/ESP32 网关")));
    sequenceLabel_->setText(QString::number(object.value(QStringLiteral("sequence")).toInt()));
    lastUpdateLabel_->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    statusBar()->showMessage(QStringLiteral("已更新传感器遥测"), 2500);
}

void MainWindow::applyModbus(const QJsonObject &object)
{
    const bool online = object.value(QStringLiteral("online")).toBool();
    modbusStateLabel_->setText(stateText(online));
    modbusStateLabel_->setStyleSheet(stateStyle(online));
    modbusDetailLabel_->setText(QStringLiteral("从站 %1 · 异常码 %2")
        .arg(object.value(QStringLiteral("slave")).toInt())
        .arg(object.value(QStringLiteral("exception")).toInt()));
    register0Label_->setText(QString::number(object.value(QStringLiteral("register0")).toInt()));
    register1Label_->setText(QString::number(object.value(QStringLiteral("register1")).toInt()));
    successLabel_->setText(QString::number(object.value(QStringLiteral("success_count")).toVariant().toLongLong()));
    errorLabel_->setText(QString::number(object.value(QStringLiteral("error_count")).toVariant().toLongLong()));
    deviceLabel_->setText(object.value(QStringLiteral("device_id")).toString(QStringLiteral("STM32/ESP32 网关")));
    sequenceLabel_->setText(QString::number(object.value(QStringLiteral("sequence")).toInt()));
    lastUpdateLabel_->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    statusBar()->showMessage(QStringLiteral("已更新 Modbus 状态"), 2500);
}

void MainWindow::loadDemoData()
{
    processLine(QStringLiteral(R"({"device_id":"industrial-gateway-demo","sequence":18,"aht20_online":true,"temperature_c":25.3,"humidity_percent":48.1,"ina219_online":true,"bus_voltage_v":5.02,"current_ma":130,"power_mw":653})"));
    processLine(QStringLiteral(R"({"device_id":"industrial-gateway-demo","sequence":18,"online":true,"slave":1,"exception":0,"register0":1234,"register1":5678,"success_count":79,"error_count":0})"));
    statusBar()->showMessage(QStringLiteral("演示数据加载成功"), 4000);
}

void MainWindow::resetLiveDataView()
{
    for (ValueCard *card : {&temperatureCard_, &humidityCard_, &voltageCard_,
                            &currentCard_, &powerCard_}) {
        card->value->setText(QStringLiteral("--"));
        card->state->setText(QStringLiteral("等待真实数据"));
    }
    deviceLabel_->setText(QStringLiteral("等待设备数据"));
    sequenceLabel_->setText(QStringLiteral("--"));
    lastUpdateLabel_->setText(QStringLiteral("尚未收到真实数据"));
    modbusStateLabel_->setText(QStringLiteral("离线"));
    modbusStateLabel_->setStyleSheet(stateStyle(false));
    modbusDetailLabel_->setText(QStringLiteral("等待 Modbus 数据"));
    register0Label_->setText(QStringLiteral("--"));
    register1Label_->setText(QStringLiteral("--"));
    successLabel_->setText(QStringLiteral("0"));
    errorLabel_->setText(QStringLiteral("0"));
}

void MainWindow::appendLog(const QString &text, const QString &kind)
{
    const QString line = QStringLiteral("%1    %2    %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), kind, text);

    if (logPaused_) {
        constexpr qsizetype maximumPausedLines = 1500;
        if (pausedLogLines_.size() >= maximumPausedLines) {
            pausedLogLines_.removeFirst();
        }
        pausedLogLines_.append(line);
        pauseLogButton_->setText(
            QStringLiteral("继续滚动 (%1)").arg(pausedLogLines_.size()));
        return;
    }

    logView_->appendPlainText(line);
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void MainWindow::setConnectionState(bool connected, const QString &detail)
{
    connectionBadge_->setText(connected ? QStringLiteral("串口已连接") : QStringLiteral("未连接"));
    connectionBadge_->setStyleSheet(stateStyle(connected));
    connectButton_->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    portBox_->setEnabled(!connected);
    baudBox_->setEnabled(!connected);
    statusBar()->showMessage(detail, 5000);
}
