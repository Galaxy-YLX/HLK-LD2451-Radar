#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QMainWindow>

#include <memory>

#include "core/scan.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace ld2451 {

class RadarView;
class SerialTransport;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 供 --demo 命令行参数使用：无硬件时直接进入演示模式
    void startDemo();

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void refreshPorts();
    void onQueryVersionClicked();
    void onQueryParamsClicked();
    void onRestartClicked();
    void onDemoClicked();
    void onDemoTick();

private:
    void stopDemo();
    void setConnectedUi(bool connected);
    void setStatus(const QString& text, bool alarm = false);
    void updateTargetTable(const std::shared_ptr<Scan>& scan);
    void onAck(quint16 command, bool success, const QByteArray& payload);

    RadarView* view_ = nullptr;
    SerialTransport* transport_ = nullptr;
    QComboBox* portCombo_ = nullptr;
    QComboBox* baudCombo_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QComboBox* sizeCombo_ = nullptr;
    QComboBox* rangeCombo_ = nullptr;
    QComboBox* mirrorCombo_ = nullptr;
    QComboBox* rotationCombo_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QPushButton* versionButton_ = nullptr;
    QPushButton* paramsButton_ = nullptr;
    QPushButton* restartButton_ = nullptr;
    QPushButton* demoButton_ = nullptr;
    QLabel* versionLabel_ = nullptr;
    QLabel* paramsLabel_ = nullptr;
    QCheckBox* snrCheck_ = nullptr;
    QSpinBox* snrSpin_ = nullptr;
    QCheckBox* persistenceCheck_ = nullptr;
    QSpinBox* persistenceSpin_ = nullptr;
    QCheckBox* vectorsCheck_ = nullptr;
    QCheckBox* labelsCheck_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    std::shared_ptr<Scan> lastScan_;
    QTimer* demoTimer_ = nullptr;
    int demoStep_ = 0;
    QElapsedTimer fpsTimer_;
    int frameCounter_ = 0;
    double fps_ = 0.0;
};

} // namespace ld2451
