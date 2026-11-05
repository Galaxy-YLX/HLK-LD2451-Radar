#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>

#include <memory>

#include "core/scan.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

namespace ld2451 {

class RadarView;
class SerialTransport;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 仅供 --demo 命令行参数使用：界面上不再提供演示入口
    void startDemo();

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void refreshPorts();
    void onQueryVersionClicked();
    void onQueryParamsClicked();
    void onApplyBaudClicked();
    void onDemoTick();

private:
    void stopDemo();
    void setConnectedUi(bool connected);
    void setStatus(const QString& text, bool alarm = false);
    void updateTargetTable(const std::shared_ptr<Scan>& scan);
    void onAck(quint16 command, bool success, const QByteArray& payload);
    void onVerifyFailed(const QString& reason);

    RadarView* view_ = nullptr;
    SerialTransport* transport_ = nullptr;
    QComboBox* portCombo_ = nullptr;
    QComboBox* baudCombo_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QPushButton* applyBaudButton_ = nullptr;
    QComboBox* sizeCombo_ = nullptr;
    QComboBox* rangeCombo_ = nullptr;
    QComboBox* mirrorCombo_ = nullptr;
    QPushButton* versionButton_ = nullptr;
    QPushButton* paramsButton_ = nullptr;
    QLabel* versionLabel_ = nullptr;
    QLabel* paramsLabel_ = nullptr;
    QCheckBox* vectorsCheck_ = nullptr;
    QDoubleSpinBox* vectorTimeSpin_ = nullptr;
    QCheckBox* labelsCheck_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QTimer* demoTimer_ = nullptr;
    int demoStep_ = 0;
    int currentBaudRate_ = 0; // 当前实际通信波特率，用于判断是否需要下发设置
    bool verifying_ = false;      // 连接握手：等待固件版本 ACK
    bool deviceVerified_ = false; // 已通过固件版本校验，设备确认为 LD2451
    QElapsedTimer fpsTimer_;
    int frameCounter_ = 0;
    double fps_ = 0.0;
};

} // namespace ld2451
