#include "app/mainwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>

#include "protocol/parser.h"
#include "transport/serial_transport.h"
#include "ui/radar_view.h"

namespace ld2451 {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    view_ = new RadarView(this);
    transport_ = new SerialTransport(this);
    setCentralWidget(view_);
    setWindowTitle(tr("LD2451 Radar Viewer"));
    resize(900, 640);

    auto* toolbar = addToolBar(tr("串口"));
    toolbar->setMovable(false);

    portCombo_ = new QComboBox(this);
    portCombo_->setEditable(true);

    baudCombo_ = new QComboBox(this);
    const int bauds[] = {9600, 19200, 38400, 57600, 115200, 230400, 256000, 460800};
    for (int b : bauds) {
        baudCombo_->addItem(QString::number(b), b);
    }
    baudCombo_->setCurrentText(QStringLiteral("115200"));

    connectButton_ = new QPushButton(tr("连接"), this);
    disconnectButton_ = new QPushButton(tr("断开"), this);
    disconnectButton_->setEnabled(false);
    auto* refreshButton = new QPushButton(tr("刷新"), this);

    sizeCombo_ = new QComboBox(this);
    for (double r : {2.0, 3.0, 4.0, 6.0, 8.0}) {
        sizeCombo_->addItem(tr("%1 px").arg(r, 0, 'f', 1), r);
    }
    sizeCombo_->setCurrentIndex(2); // 默认 4 px
    sizeCombo_->setToolTip(tr("目标点大小（半径 px）"));

    mirrorCombo_ = new QComboBox(this);
    mirrorCombo_->addItem(tr("无镜像"), 0);
    mirrorCombo_->addItem(tr("左右镜像"), 1);
    mirrorCombo_->addItem(tr("上下镜像"), 2);
    mirrorCombo_->addItem(tr("双向镜像"), 3);
    mirrorCombo_->setToolTip(tr("目标镜像显示"));

    toolbar->addWidget(new QLabel(tr(" 端口 "), this));
    toolbar->addWidget(portCombo_);
    toolbar->addWidget(new QLabel(tr(" 波特率 "), this));
    toolbar->addWidget(baudCombo_);
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(connectButton_);
    toolbar->addWidget(disconnectButton_);
    toolbar->addSeparator();

    QFont barFont = toolbar->font();
    barFont.setPointSize(12);
    toolbar->setFont(barFont);

    // 「读取版本 / 读取参数」放入上边栏；「重启模块」保留在侧边控制面板
    versionButton_ = new QPushButton(tr("读取版本"), this);
    paramsButton_ = new QPushButton(tr("读取参数"), this);
    restartButton_ = new QPushButton(tr("重启模块"), this);
    QFont sideFont = versionButton_->font();
    sideFont.setPointSize(14);
    sideFont.setBold(true);
    restartButton_->setFont(sideFont);
    versionButton_->setFont(barFont);
    paramsButton_->setFont(barFont);
    versionButton_->setToolTip(tr("使能配置 → 读取固件版本 → 结束配置"));
    paramsButton_->setToolTip(tr("读取目标检测参数与灵敏度参数"));
    toolbar->addWidget(versionButton_);
    toolbar->addWidget(paramsButton_);
    toolbar->addSeparator();
    toolbar->addWidget(mirrorCombo_);
    toolbar->addWidget(sizeCombo_);
    setConnectedUi(false);

    demoTimer_ = new QTimer(this);
    demoTimer_->setInterval(100);
    connect(demoTimer_, &QTimer::timeout, this, &MainWindow::onDemoTick);

    versionLabel_ = new QLabel(tr("固件：--"), this);
    paramsLabel_ = new QLabel(tr("参数：--"), this);
    versionLabel_->setWordWrap(true);
    paramsLabel_->setWordWrap(true);

    rangeCombo_ = new QComboBox(this);
    for (int m : {10, 20, 30, 50, 100}) {
        rangeCombo_->addItem(tr("%1 m").arg(m), m * 1.0);
    }
    rangeCombo_->setCurrentText(QStringLiteral("30 m"));
    rangeCombo_->setToolTip(tr("视图量程（半径），协议距离上限 100 m"));

    vectorsCheck_ = new QCheckBox(tr("速度线段"), this);
    vectorsCheck_->setChecked(true);
    labelsCheck_ = new QCheckBox(tr("数值标签"), this);
    labelsCheck_->setChecked(true);

    directionCombo_ = new QComboBox(this);
    directionCombo_->addItem(tr("按字段定义"), static_cast<int>(DirectionMapping::PerSpec));
    directionCombo_->addItem(tr("按示例解析"), static_cast<int>(DirectionMapping::PerExample));
    directionCombo_->setToolTip(tr("协议 §6.2 定义 0x01=靠近；§7 示例解析相反，接硬件后按实测切换"));

    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({tr("角度"), tr("距离"), tr("速度"), tr("方向"), tr("SNR")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setMinimumHeight(150);

    auto* controlDock = new QDockWidget(tr("控制"), this);
    controlDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    controlDock->setTitleBarWidget(new QWidget(controlDock));
    auto* dockWidget = new QWidget(controlDock);
    auto* dockLayout = new QVBoxLayout(dockWidget);
    dockLayout->addWidget(restartButton_);
    dockLayout->addWidget(versionLabel_);
    dockLayout->addWidget(paramsLabel_);
    dockLayout->addSpacing(8);
    dockLayout->addWidget(new QLabel(tr("量程"), dockWidget));
    dockLayout->addWidget(rangeCombo_);
    dockLayout->addSpacing(10);
    dockLayout->addWidget(new QLabel(tr("显示"), dockWidget));
    dockLayout->addWidget(vectorsCheck_);
    dockLayout->addWidget(labelsCheck_);
    dockLayout->addWidget(directionCombo_);
    dockLayout->addSpacing(10);
    dockLayout->addWidget(new QLabel(tr("目标列表"), dockWidget));
    dockLayout->addWidget(table_, 1);
    controlDock->setWidget(dockWidget);
    addDockWidget(Qt::RightDockWidgetArea, controlDock);

    // HUD：状态信息覆盖在雷达视图左上角
    statusLabel_ = new QLabel(tr("未连接"), view_);
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusLabel_->move(8, 8);
    setStatus(tr("未连接"));

    refreshPorts();

    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(disconnectButton_, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(versionButton_, &QPushButton::clicked, this, &MainWindow::onQueryVersionClicked);
    connect(paramsButton_, &QPushButton::clicked, this, &MainWindow::onQueryParamsClicked);
    connect(restartButton_, &QPushButton::clicked, this, &MainWindow::onRestartClicked);
    connect(sizeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        view_->setPointRadius(sizeCombo_->currentData().toDouble());
    });
    connect(rangeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        view_->setRangeM(rangeCombo_->currentData().toDouble());
    });
    connect(mirrorCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        view_->setMirror(mirrorCombo_->currentData().toInt());
    });
    connect(directionCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        view_->setDirectionMapping(
            static_cast<DirectionMapping>(directionCombo_->currentData().toInt()));
        updateTargetTable(lastScan_);
    });

    // 显示设置：任何变化同步到视图
    auto syncDisplay = [this] {
        view_->setShowVectors(vectorsCheck_->isChecked());
        view_->setShowLabels(labelsCheck_->isChecked());
    };
    connect(vectorsCheck_, &QCheckBox::toggled, this, syncDisplay);
    connect(labelsCheck_, &QCheckBox::toggled, this, syncDisplay);
    syncDisplay();

    connect(transport_, &SerialTransport::scanReady, this,
            [this](const std::shared_ptr<Scan>& s) {
                lastScan_ = s;
                view_->setScan(s);
                updateTargetTable(s);

                if (!fpsTimer_.isValid()) fpsTimer_.start();
                ++frameCounter_;
                const qint64 ms = fpsTimer_.elapsed();
                if (ms >= 1000) {
                    fps_ = frameCounter_ * 1000.0 / static_cast<double>(ms);
                    frameCounter_ = 0;
                    fpsTimer_.restart();
                }
                setStatus(tr("已连接 · %1 目标 · %2 fps · %3")
                              .arg(view_->targetCount())
                              .arg(QString::number(fps_, 'f', 1))
                              .arg(s->alarm_approaching ? tr("有目标靠近!") : tr("正常")),
                          s->alarm_approaching);
            });
    connect(transport_, &SerialTransport::ackReady, this,
            [this](quint16 cmd, bool ok, const QByteArray& payload) {
                onAck(cmd, ok, payload);
            });
    connect(transport_, &SerialTransport::frameError, this,
            [this](const QString& msg) { setStatus(msg); });
}

void MainWindow::setConnectedUi(bool connected) {
    connectButton_->setEnabled(!connected);
    disconnectButton_->setEnabled(connected);
    versionButton_->setEnabled(connected);
    paramsButton_->setEnabled(connected);
    restartButton_->setEnabled(connected);
}

void MainWindow::setStatus(const QString& text, bool alarm) {
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(
        QStringLiteral("background: transparent; color: %1;")
            .arg(alarm ? QStringLiteral("#FF4040") : QStringLiteral("#00DC78")));
    statusLabel_->adjustSize();
}

void MainWindow::refreshPorts() {
    const QString cur = portCombo_->currentText();
    portCombo_->clear();
    portCombo_->addItems(SerialTransport::availablePorts());
    if (!cur.isEmpty()) {
        const int idx = portCombo_->findText(cur);
        if (idx >= 0) portCombo_->setCurrentIndex(idx);
    }
    if (portCombo_->count() == 0 && !transport_->isOpen()) {
        setStatus(tr("未检测到串口，点击「刷新」重试"));
    }
}

void MainWindow::onConnectClicked() {
    if (transport_->isOpen()) return;
    stopDemo();
    const QString port = portCombo_->currentText().trimmed();
    if (port.isEmpty()) {
        setStatus(tr("请先选择串口"));
        return;
    }
    const int baud = baudCombo_->currentData().toInt();
    if (transport_->open(port, baud)) {
        setConnectedUi(true);
        setStatus(tr("已连接 %1 @ %2").arg(port).arg(baud));
    } else {
        setStatus(tr("打开失败：%1").arg(transport_->errorString()));
    }
}

void MainWindow::onDisconnectClicked() {
    transport_->close();
    setConnectedUi(false);
    setStatus(tr("已断开"));
    lastScan_.reset();
    view_->setScan(nullptr);
    updateTargetTable(nullptr);
}

void MainWindow::onQueryVersionClicked() {
    if (!transport_->isOpen()) return;
    setStatus(transport_->queryFirmwareVersion() ? tr("读取固件版本…") : tr("命令发送失败"));
}

void MainWindow::onQueryParamsClicked() {
    if (!transport_->isOpen()) return;
    setStatus(transport_->queryParameters() ? tr("读取模块参数…") : tr("命令发送失败"));
}

void MainWindow::onRestartClicked() {
    if (!transport_->isOpen()) return;
    setStatus(transport_->restartModule() ? tr("模块重启中…") : tr("命令发送失败"));
}

void MainWindow::startDemo() {
    if (demoTimer_->isActive()) return;
    if (transport_->isOpen()) {
        setStatus(tr("请先断开串口再进入演示"));
        return;
    }
    demoStep_ = 0;
    demoTimer_->start();
    onDemoTick();
}

void MainWindow::stopDemo() {
    if (demoTimer_ == nullptr || !demoTimer_->isActive()) return;
    demoTimer_->stop();
    lastScan_.reset();
    view_->setScan(nullptr);
    updateTargetTable(nullptr);
    setStatus(tr("演示已停止"));
}

void MainWindow::onDemoTick() {
    constexpr double kPi = 3.14159265358979323846;
    const double t = ++demoStep_ / 10.0;
    auto scan = std::make_shared<Scan>();

    const auto addTarget = [&](double angle_deg, double distance_m, double speed,
                               bool approaching, int snr) {
        Target tg;
        tg.angle_deg = angle_deg;
        tg.distance_m = distance_m;
        tg.direction_flag = approaching ? 0x01 : 0x00;
        tg.speed_kmh = speed;
        tg.signed_speed_kmh = approaching ? speed : -speed;
        tg.approaching = approaching;
        tg.snr = snr;
        const double rad = angle_deg * kPi / 180.0;
        tg.x_m = distance_m * std::sin(rad);
        tg.y_m = distance_m * std::cos(rad);
        scan->targets.push_back(tg);
    };

    // 目标角度控制在 ±15° 真实波束内，距离不超过默认 30 m 量程
    addTarget(-8.0 + 6.0 * std::sin(t * 0.7), 18.0 + 8.0 * std::sin(t * 0.9),
              42.0 + 15.0 * std::sin(t), true, 28);
    addTarget(6.0 + 8.0 * std::cos(t * 0.5), 22.0 - 5.0 * std::cos(t * 0.8),
              30.0 + 10.0 * std::cos(t * 0.6), false, 18);
    addTarget(2.0 + 3.0 * std::sin(t * 0.4), 8.0 + 3.0 * std::cos(t * 1.1), 12.0, true, 45);

    scan->target_count = static_cast<int>(scan->targets.size());
    scan->alarm_approaching = true;
    lastScan_ = scan;
    view_->setScan(scan);
    updateTargetTable(scan);
    setStatus(tr("演示模式 · %1 目标 · 有目标靠近!").arg(scan->target_count), true);
}

void MainWindow::updateTargetTable(const std::shared_ptr<Scan>& scan) {
    table_->setRowCount(0);
    if (!scan) return;

    for (const auto& t : scan->targets) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        const bool approaching =
            isApproaching(t.direction_flag, view_->directionMapping());
        const double speed = approaching ? t.speed_kmh : -t.speed_kmh;
        const auto cell = [](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            return item;
        };
        table_->setItem(row, 0, cell(tr("%1°").arg(t.angle_deg, 0, 'f', 0)));
        table_->setItem(row, 1, cell(tr("%1 m").arg(t.distance_m, 0, 'f', 0)));
        table_->setItem(row, 2, cell(tr("%1 km/h").arg(speed, 0, 'f', 0)));
        table_->setItem(row, 3, cell(approaching ? tr("靠近") : tr("远离")));
        table_->setItem(row, 4, cell(QString::number(t.snr)));
    }
}

void MainWindow::onAck(quint16 command, bool success, const QByteArray& payload) {
    if (!success) {
        setStatus(tr("命令 0x%1 执行失败").arg(command & 0x00FF, 2, 16, QChar('0')));
        return;
    }

    const auto byte = [&payload](int i) { return static_cast<quint8>(payload.at(i)); };
    switch (command & 0x00FF) {
    case 0x00A0: { // 固件版本：类型(2B) + 主版本(2B) + 次版本(4B)
        if (payload.size() >= 8) {
            const quint32 minor = static_cast<quint32>(byte(4)) |
                                  (static_cast<quint32>(byte(5)) << 8) |
                                  (static_cast<quint32>(byte(6)) << 16) |
                                  (static_cast<quint32>(byte(7)) << 24);
            const QString sub = QString::number(minor, 16).rightJustified(8, QChar('0')).toUpper();
            versionLabel_->setText(tr("固件：V%1.%2.%3")
                                       .arg(byte(2))
                                       .arg(byte(3), 2, 10, QChar('0'))
                                       .arg(sub));
        }
        setStatus(tr("固件版本已读取"));
        break;
    }
    case 0x0012: { // 目标检测参数
        if (payload.size() >= 4) {
            const QString dir = byte(1) == 0 ? tr("只检测远离")
                                : byte(1) == 1 ? tr("只检测靠近")
                                               : tr("均检测");
            paramsLabel_->setText(tr("检测：最远 %1 m · %2 · 最小 %3 km/h · 延迟 %4 s")
                                      .arg(byte(0))
                                      .arg(dir)
                                      .arg(byte(2))
                                      .arg(byte(3)));
        }
        setStatus(tr("目标检测参数已读取"));
        break;
    }
    case 0x0013: { // 灵敏度参数
        if (payload.size() >= 2) {
            paramsLabel_->setText(paramsLabel_->text() + tr("\n灵敏度：触发 %1 次 · 阈值等级 %2")
                                                              .arg(byte(0))
                                                              .arg(byte(1)));
        }
        setStatus(tr("灵敏度参数已读取"));
        break;
    }
    case 0x00A3:
        setStatus(tr("模块已重启"));
        break;
    default:
        break; // 使能配置 / 结束配置的 ACK 无需提示
    }
}

} // namespace ld2451
