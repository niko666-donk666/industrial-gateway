#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QStringList>

class QComboBox;
class QGridLayout;
class QJsonObject;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSerialPort;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    struct ValueCard {
        QLabel *value = nullptr;
        QLabel *state = nullptr;
    };

    void buildUi();
    void refreshPorts();
    void toggleConnection();
    void readSerialData();
    void processLine(const QString &line);
    bool processJson(const QByteArray &json);
    bool processGatewayFrame(const QString &line);
    void applyTelemetry(const QJsonObject &object);
    void applyModbus(const QJsonObject &object);
    void loadDemoData();
    void resetLiveDataView();
    void appendLog(const QString &text, const QString &kind = QStringLiteral("RX"));
    void setConnectionState(bool connected, const QString &detail);
    ValueCard createCard(QGridLayout *layout, int column, const QString &title,
                         const QString &unit, const QString &accent);

    QSerialPort *serial_ = nullptr;
    QByteArray receiveBuffer_;

    QComboBox *sourceBox_ = nullptr;
    QComboBox *portBox_ = nullptr;
    QComboBox *baudBox_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *pauseLogButton_ = nullptr;
    QLabel *connectionBadge_ = nullptr;
    QLabel *deviceLabel_ = nullptr;
    QLabel *sequenceLabel_ = nullptr;
    QLabel *lastUpdateLabel_ = nullptr;
    QLabel *modbusStateLabel_ = nullptr;
    QLabel *modbusDetailLabel_ = nullptr;
    QLabel *register0Label_ = nullptr;
    QLabel *register1Label_ = nullptr;
    QLabel *successLabel_ = nullptr;
    QLabel *errorLabel_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QStringList pausedLogLines_;
    bool logPaused_ = false;

    ValueCard temperatureCard_;
    ValueCard humidityCard_;
    ValueCard voltageCard_;
    ValueCard currentCard_;
    ValueCard powerCard_;
};
