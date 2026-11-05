#include "app/mainwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>
#include <cstdint>
#include <vector>

#include "transport/serial_transport.h"
#include "ui/radar_view.h"

namespace ld2451 {
namespace {

// 协议 §5.5：模块用 0x00 表示「程序默认」的信噪比阈值等级 4，
// 下拉只提供 3~8 的显式等级，因此把读回字节映射为下拉中的等级；
// 文档未定义的其它值回落到最小等级 3，避免误判为「用户已改动」。
int snrLevelFromRaw(int raw) {
    if (raw == 0) return 4;
    return (raw >= 3 && raw <= 8) ? raw : 3;
}

} // namespace

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
    baudCombo_->setCurrentText(QStringLiteral("460800"));
    baudCombo_->setToolTip(tr("连接时作为本地波特率；连接后改选并点「应用」可同步修改模块波特率"));

    connectButton_ = new QPushButton(tr("连接"), this);
    disconnectButton_ = new QPushButton(tr("断开"), this);
    disconnectButton_->setEnabled(false);
    auto* refreshButton = new QPushButton(tr("刷新"), this);
    applyBaudButton_ = new QPushButton(tr("应用"), this);
    applyBaudButton_->setToolTip(tr("把当前选中的波特率写入模块并重启，随后软件本地同步切换"));

    sizeCombo_ = new QComboBox(this);
    for (double r : {2.0, 3.0, 4.0, 6.0, 8.0}) {
        sizeCombo_->addItem(tr("%1 px").arg(r, 0, 'f', 1), r);
    }
    sizeCombo_->setCurrentIndex(2); // 默认 4 px
    sizeCombo_->setToolTip(tr("目标点大小（半径 px）"));

    mirrorCombo_ = new QComboBox(this);
    mirrorCombo_->addItem(tr("无镜像"), 0);
    mirrorCombo_->addItem(tr("左右镜像"), 1);
    mirrorCombo_->setToolTip(tr("目标左右镜像显示"));

    toolbar->addWidget(new QLabel(tr(" 端口 "), this));
    toolbar->addWidget(portCombo_);
    toolbar->addWidget(new QLabel(tr(" 波特率 "), this));
    toolbar->addWidget(baudCombo_);
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(connectButton_);
    toolbar->addWidget(disconnectButton_);
    toolbar->addWidget(applyBaudButton_);
    toolbar->addSeparator();

    QFont barFont = toolbar->font();
    barFont.setPointSize(12);
    toolbar->setFont(barFont);

    // 模块命令按钮（读取版本 / 读取参数）放入上边栏
    versionButton_ = new QPushButton(tr("读取版本"), this);
    paramsButton_ = new QPushButton(tr("读取参数"), this);
    versionButton_->setFont(barFont);
    paramsButton_->setFont(barFont);
    versionButton_->setToolTip(tr("使能配置 → 读取固件版本 → 结束配置"));
    paramsButton_->setToolTip(tr("读取目标检测参数与灵敏度参数"));
    toolbar->addWidget(versionButton_);
    toolbar->addWidget(paramsButton_);
    toolbar->addSeparator();
    toolbar->addWidget(mirrorCombo_);
    toolbar->addWidget(sizeCombo_);

    demoTimer_ = new QTimer(this);
    demoTimer_->setInterval(100);
    connect(demoTimer_, &QTimer::timeout, this, &MainWindow::onDemoTick);

    // 参数控件改动后合并 300 ms 内的连续变化，再自动下发 0x0002 / 0x0003
    paramsWriteTimer_ = new QTimer(this);
    paramsWriteTimer_->setSingleShot(true);
    paramsWriteTimer_->setInterval(300);
    connect(paramsWriteTimer_, &QTimer::timeout, this, [this] { applyParamsFromControls(); });

    versionLabel_ = new QLabel(tr("固件：--"), view_);
    paramsLabel_ = new QLabel(tr("参数：--"), view_);

    rangeCombo_ = new QComboBox(this);
    for (int m : {10, 20, 30, 50, 100}) {
        rangeCombo_->addItem(tr("%1 m").arg(m), m * 1.0);
    }
    rangeCombo_->setCurrentText(QStringLiteral("30 m"));
    rangeCombo_->setToolTip(tr("既是视图量程（半径），也对应模块最远检测距离；协议上限 100 m"));
    motionDirCombo_ = new QComboBox(this);
    motionDirCombo_->addItem(tr("只检测远离"), 0);
    motionDirCombo_->addItem(tr("只检测靠近"), 1);
    motionDirCombo_->addItem(tr("均检测"), 2);
    motionDirCombo_->setCurrentIndex(2);
    motionDirCombo_->setToolTip(tr("协议 0x0002 第 2 字节：00 只远离 / 01 只靠近 / 02 均检测"));
    minSpeedSpin_ = new QSpinBox(this);
    minSpeedSpin_->setRange(0, 120);
    minSpeedSpin_->setValue(0);
    minSpeedSpin_->setSuffix(tr(" km/h"));
    minSpeedSpin_->setToolTip(tr("速度门：最小运动速度，低于该值的目标被模块忽略；协议范围 0~120 km/h"));
    triggerCombo_ = new QComboBox(this);
    for (int n = 1; n <= 10; ++n) {
        triggerCombo_->addItem(tr("%1 次").arg(n), n);
    }
    // 初始值固定为 1 次；按值查找，避免依赖选项顺序
    constexpr int kDefaultTriggerCount = 1;
    const int defaultTriggerIdx = triggerCombo_->findData(kDefaultTriggerCount);
    if (defaultTriggerIdx >= 0) triggerCombo_->setCurrentIndex(defaultTriggerIdx);
    triggerCombo_->setToolTip(tr("协议 0x0003 第 1 字节：连续检测次数满足才上报报警（1~10 次）"));
    snrThresholdCombo_ = new QComboBox(this);
    for (int level = 3; level <= 8; ++level) {
        snrThresholdCombo_->addItem(tr("%1 级").arg(level), level);
    }
    // 初始值固定为 3（最灵敏）；按值查找，避免依赖选项顺序
    constexpr int kDefaultSnrLevel = 3;
    const int defaultSnrIdx = snrThresholdCombo_->findData(kDefaultSnrLevel);
    if (defaultSnrIdx >= 0) snrThresholdCombo_->setCurrentIndex(defaultSnrIdx);
    snrThresholdCombo_->setToolTip(
        tr("协议 0x0003 第 2 字节：3~8 级，值越大越难检测；模块返回 0 表示程序默认等级 4"));

    vectorsCheck_ = new QCheckBox(tr("速度线段"), this);
    vectorsCheck_->setChecked(true);
    vectorTimeCombo_ = new QComboBox(this);
    for (int s = 1; s <= 10; ++s) {
        vectorTimeCombo_->addItem(tr("%1 s").arg(s), s);
    }
    vectorTimeCombo_->setCurrentIndex(0); // 默认 1 s
    vectorTimeCombo_->setToolTip(tr("线段长度 = 速度(km/h)/3.6 × 该时长，单位：米"));
    labelsCheck_ = new QCheckBox(tr("数值标签"), this);
    labelsCheck_->setChecked(true);

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
    // 所有选择框统一放在第 1 列：左边界、宽度、高度由网格对齐
    auto* grid = new QGridLayout();
    grid->setVerticalSpacing(6);
    grid->setHorizontalSpacing(8);
    grid->setColumnStretch(1, 1);
    const auto addField = [&](int row, const QString& text, QWidget* field) {
        grid->addWidget(new QLabel(text, dockWidget), row, 0);
        grid->addWidget(field, row, 1);
    };
    addField(0, tr("量程"), rangeCombo_);
    addField(1, tr("运动方向"), motionDirCombo_);
    addField(2, tr("速度门"), minSpeedSpin_);
    addField(3, tr("累积触发"), triggerCombo_);
    addField(4, tr("信噪比阈值"), snrThresholdCombo_);
    grid->setRowMinimumHeight(5, 6); // 参数区与显示区之间留白
    grid->addWidget(new QLabel(tr("显示"), dockWidget), 6, 0, 1, 2);
    grid->addWidget(vectorsCheck_, 7, 0);
    grid->addWidget(vectorTimeCombo_, 7, 1);
    grid->addWidget(labelsCheck_, 8, 0, 1, 2);
    dockLayout->addLayout(grid);

    // 统一选择框高度（QComboBox / QSpinBox 的默认 sizeHint 可能差几个像素）
    const int fieldHeight = rangeCombo_->sizeHint().height();
    QWidget* const fields[] = {rangeCombo_, motionDirCombo_, minSpeedSpin_, triggerCombo_,
                               snrThresholdCombo_, vectorTimeCombo_};
    for (QWidget* field : fields) field->setMinimumHeight(fieldHeight);

    dockLayout->addSpacing(10);
    dockLayout->addWidget(new QLabel(tr("目标列表"), dockWidget));
    dockLayout->addWidget(table_, 1);
    controlDock->setWidget(dockWidget);
    addDockWidget(Qt::RightDockWidgetArea, controlDock);

    // HUD：状态 / 固件版本 / 参数，依次覆盖在雷达视图左上角
    statusLabel_ = new QLabel(tr("未连接"), view_);
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusLabel_->move(8, 8);

    const QString hudStyle = QStringLiteral("background: transparent; color: #00DC78;");
    versionLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    versionLabel_->setStyleSheet(hudStyle);
    versionLabel_->move(8, 30);
    paramsLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    paramsLabel_->setStyleSheet(hudStyle);
    paramsLabel_->move(8, 50);
    versionLabel_->show();
    paramsLabel_->show();
    versionLabel_->adjustSize();
    paramsLabel_->adjustSize();
    setStatus(tr("未连接"));

    setConnectedUi(false);
    refreshPorts();

    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(disconnectButton_, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(versionButton_, &QPushButton::clicked, this, &MainWindow::onQueryVersionClicked);
    connect(paramsButton_, &QPushButton::clicked, this, &MainWindow::onQueryParamsClicked);
    connect(applyBaudButton_, &QPushButton::clicked, this, &MainWindow::onApplyBaudClicked);
    connect(sizeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        view_->setPointRadius(sizeCombo_->currentData().toDouble());
    });
    connect(rangeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        view_->setRangeM(rangeCombo_->currentData().toDouble());
        scheduleParamsWrite();
    });
    connect(motionDirCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        scheduleParamsWrite();
    });
    connect(minSpeedSpin_, &QSpinBox::valueChanged, this, [this](int) {
        scheduleParamsWrite();
    });
    connect(triggerCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        scheduleParamsWrite();
    });
    connect(snrThresholdCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        scheduleParamsWrite();
    });
    connect(mirrorCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        view_->setMirror(mirrorCombo_->currentData().toInt());
    });
    // 显示设置：任何变化同步到视图
    auto syncDisplay = [this] {
        view_->setShowVectors(vectorsCheck_->isChecked());
        view_->setShowLabels(labelsCheck_->isChecked());
        view_->setVectorTime(vectorTimeCombo_->currentData().toInt());
    };
    connect(vectorsCheck_, &QCheckBox::toggled, this, syncDisplay);
    connect(vectorTimeCombo_, &QComboBox::currentIndexChanged, this, syncDisplay);
    connect(labelsCheck_, &QCheckBox::toggled, this, syncDisplay);
    syncDisplay();

    connect(transport_, &SerialTransport::scanReady, this,
            [this](const std::shared_ptr<Scan>& s) {
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
                if (!deviceVerified_) return; // 握手完成前保留校验提示
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
    connect(transport_, &SerialTransport::baudRateChanged, this, [this](int baud) {
        currentBaudRate_ = baud;
        const int idx = baudCombo_->findData(baud);
        if (idx >= 0) baudCombo_->setCurrentIndex(idx);
        setStatus(tr("波特率已同步为 %1").arg(baud));
    });
    // 整条握手队列（含结束配置）全部 ACK 成功，才确认设备可用
    connect(transport_, &SerialTransport::sequenceFinished, this,
            &MainWindow::onSequenceFinished);
    connect(transport_, &SerialTransport::portLost, this,
            [this](const QString& msg) {
                onDisconnectClicked();
                setStatus(tr("串口连接已丢失：%1").arg(msg), true);
            });
    connect(transport_, &SerialTransport::frameError, this,
            [this](const QString& msg) {
                // 传输层已对单条命令快速重发；仍失败则换下一个候选波特率重试
                if (verifying_) {
                    onVerifyFailed(msg);
                    return;
                }
                autoReadParams_ = false; // 队列失败，丢弃挂起的自动回读
                setStatus(msg);
            });
}

void MainWindow::setConnectedUi(bool connected) {
    connectButton_->setEnabled(!connected);
    disconnectButton_->setEnabled(connected);
    versionButton_->setEnabled(connected);
    paramsButton_->setEnabled(connected);
    applyBaudButton_->setEnabled(connected);
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
    targetParams_.clear();      // 新连接不沿用上一台设备的检测参数
    sensitivityParams_.clear(); // 灵敏度参数同理
    paramsWriteTimer_->stop();
    suppressParamWrite_ = false;
    autoReadParams_ = false;
    const QString port = portCombo_->currentText().trimmed();
    if (port.isEmpty()) {
        setStatus(tr("请先选择串口"));
        return;
    }
    const int baud = baudCombo_->currentData().toInt();
    requestedBaudRate_ = baud;
    // 实测模块可能已被切到 460800，也可能仍是出厂 115200；按常见度自动回退
    baudCandidates_.clear();
    for (int b : {115200, 460800, 256000, 230400}) {
        if (b != baud) baudCandidates_.push_back(b);
    }
    openAndVerify(port, baud);
}

bool MainWindow::openAndVerify(const QString& port, int baud) {
    // 串口打开后立即发起握手：实测 0 ms 延时即可正常应答，固定等 400 ms 纯属浪费
    if (!transport_->open(port, baud)) {
        baudCandidates_.clear();
        finalizeFailure(tr("打开失败：%1").arg(transport_->errorString()));
        return false;
    }
    currentBaudRate_ = baud;
    deviceVerified_ = false;
    verifying_ = true;
    setConnectedUi(true);
    versionButton_->setEnabled(false);
    paramsButton_->setEnabled(false);
    applyBaudButton_->setEnabled(false);
    versionLabel_->setText(tr("固件：--"));
    paramsLabel_->setText(tr("参数：--"));
    versionLabel_->adjustSize();
    paramsLabel_->adjustSize();
    setStatus(tr("已打开 %1 @ %2，正在校验设备…").arg(port).arg(baud));
    QTimer::singleShot(0, this, [this] {
        if (!verifying_ || !transport_->isOpen()) return;
        if (!transport_->queryFirmwareVersion()) {
            onVerifyFailed(tr("无法发起版本查询"));
        }
    });
    return true;
}

void MainWindow::onSequenceFinished() {
    if (!verifying_) return;
    // 只有整条「使能配置 → 读取版本 → 结束配置」都成功才算连接成功，
    // 避免结束配置失败时模块停留在配置模式、界面却显示已连接。
    verifying_ = false;
    deviceVerified_ = true;
    autoReadParams_ = false;
    baudCandidates_.clear();
    setConnectedUi(true);
    const int idx = baudCombo_->findData(currentBaudRate_);
    if (idx >= 0) baudCombo_->setCurrentIndex(idx);
    setStatus(tr("设备校验通过 · %1").arg(versionLabel_->text()));
    QTimer::singleShot(0, this, [this] {
        if (!transport_->isOpen() || !deviceVerified_) return;
        if (!transport_->queryParameters()) {
            setStatus(tr("参数回读未启动，请手动点「读取参数」"));
        }
    });
}

void MainWindow::onDisconnectClicked() {
    transport_->close();
    baudCandidates_.clear();
    setConnectedUi(false);
    currentBaudRate_ = 0;
    verifying_ = false;
    deviceVerified_ = false;
    paramsWriteTimer_->stop();
    suppressParamWrite_ = false;
    autoReadParams_ = false;
    setStatus(tr("已断开"));
    view_->setScan(nullptr);
    updateTargetTable(nullptr);
}

void MainWindow::onVerifyFailed(const QString& reason) {
    if (!verifying_) return;
    transport_->close();
    if (!baudCandidates_.empty()) {
        const int tried = currentBaudRate_;
        const int next = baudCandidates_.front();
        baudCandidates_.erase(baudCandidates_.begin());
        setStatus(tr("%1 无响应（%2），正在尝试 %3…").arg(tried).arg(reason).arg(next));
        // 延迟一轮事件循环：frameError 可能来自组帧器回调栈内，避免在那里重开串口
        QTimer::singleShot(0, this, [this, next] {
            if (!verifying_) return;
            openAndVerify(portCombo_->currentText().trimmed(), next);
        });
        return;
    }
    finalizeFailure(reason);
}

void MainWindow::finalizeFailure(const QString& reason) {
    verifying_ = false;
    deviceVerified_ = false;
    currentBaudRate_ = 0;
    paramsWriteTimer_->stop();
    autoReadParams_ = false;
    baudCandidates_.clear();
    transport_->close();
    setConnectedUi(false);
    versionLabel_->setText(tr("固件：--"));
    versionLabel_->adjustSize();
    view_->setScan(nullptr);
    updateTargetTable(nullptr);
    // 恢复用户最初选择的波特率，避免停留在自动尝试过的值
    const int idx = baudCombo_->findData(requestedBaudRate_);
    if (idx >= 0) baudCombo_->setCurrentIndex(idx);
    setStatus(tr("连接失败：%1（未通过固件版本校验，请检查端口与波特率）").arg(reason), true);
}

void MainWindow::onQueryVersionClicked() {
    if (!transport_->isOpen()) return;
    setStatus(transport_->queryFirmwareVersion() ? tr("读取固件版本…") : tr("命令发送失败"));
}

void MainWindow::onQueryParamsClicked() {
    if (!transport_->isOpen()) return;
    setStatus(transport_->queryParameters() ? tr("读取模块参数…") : tr("命令发送失败"));
}

void MainWindow::onApplyBaudClicked() {
    if (!transport_->isOpen()) return;
    const int target = baudCombo_->currentData().toInt();
    if (target == currentBaudRate_) {
        setStatus(tr("模块已是 %1，无需修改").arg(target));
        return;
    }
    setStatus(transport_->setBaudRate(target)
                  ? tr("正在设置模块波特率为 %1…").arg(target)
                  : tr("不支持该波特率或流程进行中"));
}

void MainWindow::scheduleParamsWrite() {
    if (suppressParamWrite_ || !transport_->isOpen() || !deviceVerified_) return;
    paramsWriteTimer_->start(); // singleShot，连续改动会重新计时
}

void MainWindow::applyParamsFromControls() {
    if (!transport_->isOpen() || !deviceVerified_) return;
    if (targetParams_.size() < 4) {
        setStatus(tr("参数尚未就绪，请稍候或点「读取参数」"));
        return;
    }
    const int meters = rangeCombo_->currentData().toInt();
    std::vector<std::uint8_t> params;
    params.reserve(4);
    for (int i = 0; i < 4; ++i) {
        params.push_back(static_cast<std::uint8_t>(targetParams_.at(i)));
    }
    params[0] = static_cast<std::uint8_t>(meters);
    params[1] = static_cast<std::uint8_t>(motionDirCombo_->currentData().toInt());
    params[2] = static_cast<std::uint8_t>(minSpeedSpin_->value());
    // params[3] 无目标延迟：没有对应控件，保持读回的原值

    // 灵敏度参数（协议 §5.5）：仅当累积触发或信噪比阈值与读回值不同时才附带 0x0003，
    // 未改动的那一项与两个扩展字节保持读回原值，两条写命令共用一条 ACK 队列。
    std::vector<std::uint8_t> sensitivity;
    const int trigger = triggerCombo_->currentData().toInt();
    const int snrLevel = snrThresholdCombo_->currentData().toInt();
    if (sensitivityParams_.size() >= 4) {
        const int readTrigger = static_cast<std::uint8_t>(sensitivityParams_.at(0));
        const int readSnrLevel =
            snrLevelFromRaw(static_cast<std::uint8_t>(sensitivityParams_.at(1)));
        const bool triggerChanged = trigger != readTrigger;
        const bool snrChanged = snrLevel != readSnrLevel;
        if (triggerChanged || snrChanged) {
            sensitivity.reserve(4);
            for (int i = 0; i < 4; ++i) {
                sensitivity.push_back(static_cast<std::uint8_t>(sensitivityParams_.at(i)));
            }
            sensitivity[0] = static_cast<std::uint8_t>(trigger);
            if (snrChanged) sensitivity[1] = static_cast<std::uint8_t>(snrLevel);
        }
    }
    if (!transport_->setParameters(params, sensitivity)) {
        if (transport_->isOpen() && deviceVerified_) {
            paramsWriteTimer_->start(); // 队列忙，稍后自动重试，避免改动丢失
        }
        return;
    }
    setStatus(tr("正在写入参数：%1 m / %2 / %3 km/h / 累积触发 %4 次 / 信噪比阈值 %5 级…")
                  .arg(meters)
                  .arg(motionDirCombo_->currentText())
                  .arg(minSpeedSpin_->value())
                  .arg(trigger)
                  .arg(snrLevel));
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
        const bool approaching = t.approaching; // 解析层按 §6.2 固定判定
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
        // 校验通过与否由 sequenceFinished 判定：结束配置 ACK 成功才算握手完成
        if (!verifying_) setStatus(tr("固件版本已读取"));
        break;
    }
    case 0x0012: { // 目标检测参数：每个数据项单独一行
        if (payload.size() >= 4) {
            const QString dir = byte(1) == 0 ? tr("只检测远离")
                                : byte(1) == 1 ? tr("只检测靠近")
                                               : tr("均检测");
            paramsLabel_->setText(tr("最远距离：%1 m\n检测方向：%2\n最小速度：%3 km/h\n无目标延迟：%4 s")
                                      .arg(byte(0))
                                      .arg(dir)
                                      .arg(byte(2))
                                      .arg(byte(3)));
            targetParams_ = payload.left(4);
            // 量程与模块最远距离对应：同步到最接近的档位，同时触发视图量程更新
            int bestIdx = -1;
            int bestDiff = 1000;
            for (int i = 0; i < rangeCombo_->count(); ++i) {
                const int diff =
                    std::abs(rangeCombo_->itemData(i).toInt() - static_cast<int>(byte(0)));
                if (diff < bestDiff) {
                    bestDiff = diff;
                    bestIdx = i;
                }
            }
            suppressParamWrite_ = true; // 回同步控件不应触发新的下发
            if (bestIdx >= 0) rangeCombo_->setCurrentIndex(bestIdx);
            const int dirIdx = motionDirCombo_->findData(static_cast<int>(byte(1)));
            if (dirIdx >= 0) motionDirCombo_->setCurrentIndex(dirIdx);
            minSpeedSpin_->setValue(static_cast<int>(byte(2)));
            suppressParamWrite_ = false;
        }
        setStatus(tr("目标检测参数已读取"));
        break;
    }
    case 0x0013: { // 灵敏度参数：HUD 追加显示，并同步「累积触发 / 信噪比阈值」下拉
        if (payload.size() >= 2) {
            const QString current = paramsLabel_->text();
            const QString prefix = (current == tr("参数：--")) ? QString()
                                                               : current + QStringLiteral("\n");
            const int rawSnr = static_cast<int>(byte(1));
            const QString snrText = rawSnr == 0 ? tr("0（默认 4）") : QString::number(rawSnr);
            paramsLabel_->setText(prefix + tr("累积触发：%1 次\n信噪比阈值：%2")
                                               .arg(byte(0))
                                               .arg(snrText));
        }
        if (payload.size() >= 4) {
            sensitivityParams_ = payload.left(4);
            const int triggerIdx = triggerCombo_->findData(static_cast<int>(byte(0)));
            const int snrIdx = snrThresholdCombo_->findData(
                snrLevelFromRaw(static_cast<int>(byte(1))));
            suppressParamWrite_ = true; // 回同步控件不应触发新的下发
            if (triggerIdx >= 0) triggerCombo_->setCurrentIndex(triggerIdx);
            if (snrIdx >= 0) snrThresholdCombo_->setCurrentIndex(snrIdx);
            suppressParamWrite_ = false;
        }
        setStatus(tr("灵敏度参数已读取"));
        break;
    }
    case 0x0002:
    case 0x0003:
        autoReadParams_ = true; // 写入队列结束后自动回读，刷新参数显示
        setStatus(tr("参数已写入，正在回读刷新…"));
        break;
    case 0x00FF: // 使能配置 ACK：给出中间反馈，便于判断模块是否有响应
        setStatus(verifying_ ? tr("正在校验设备：配置模式已使能…")
                             : tr("配置模式已使能，等待查询…"));
        break;
    case 0x00A1:
        setStatus(tr("波特率已写入，等待模块重启…"));
        break;
    case 0x00A3:
        setStatus(tr("模块重启中，即将同步本地波特率…"));
        break;
    case 0x00FE: // 结束配置：当前队列已收尾，此时可安全发起下一条流程
        if (autoReadParams_) {
            autoReadParams_ = false;
            QTimer::singleShot(0, this, [this] {
                if (!transport_->queryParameters()) {
                    setStatus(tr("参数回读未启动，请手动点「读取参数」"));
                }
            });
        }
        break;
    default:
        break;
    }
    versionLabel_->adjustSize();
    paramsLabel_->adjustSize();
}

} // namespace ld2451
