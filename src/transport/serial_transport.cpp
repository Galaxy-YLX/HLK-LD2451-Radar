#include "transport/serial_transport.h"

#include <QSerialPortInfo>

#include <algorithm>

namespace ld2451 {
namespace {

// 协议 §5.8 波特率选择索引
int baudIndex(int baudRate) {
    switch (baudRate) {
    case 9600: return 0x0001;
    case 19200: return 0x0002;
    case 38400: return 0x0003;
    case 57600: return 0x0004;
    case 115200: return 0x0005;
    case 230400: return 0x0006;
    case 256000: return 0x0007;
    case 460800: return 0x0008;
    default: return 0;
    }
}

std::uint16_t cmd(Command c) { return static_cast<std::uint16_t>(c); }

} // namespace

SerialTransport::SerialTransport(QObject* parent)
    : QObject(parent),
      assembler_([this](FrameKind kind, const std::uint8_t* data, std::size_t len) {
          onFrame(kind, data, len);
      }) {
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    connect(&serial_, &QSerialPort::readyRead, this, &SerialTransport::onReadyRead);
    connect(&serial_, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError err) {
                if (err == QSerialPort::NoError) return;
                const QString reason = serial_.errorString();
                const bool fatal = err == QSerialPort::DeviceNotFoundError ||
                                   err == QSerialPort::ResourceError ||
                                   err == QSerialPort::PermissionError ||
                                   err == QSerialPort::OpenError;
                if (fatal) {
                    if (portLostSignaled_) return;
                    portLostSignaled_ = true;
                    ackTimer_.stop();
                    retryTimer_.stop();
                    sequencer_.reset();
                    emit portLost(reason);
                    return;
                }
                emit frameError(reason);
            });

    // 单条命令超时 → 快速重发当前命令；重试用尽才报告失败。
    // 实测模块偶发忽略某条命令但下一条正常应答，整条流程重跑既慢又容易反复失败。
    ackTimer_.setSingleShot(true);
    retryTimer_.setSingleShot(true);
    connect(&ackTimer_, &QTimer::timeout, this,
            [this] { handleAction(sequencer_.onTimeout()); });
    connect(&retryTimer_, &QTimer::timeout, this, [this] { sendCurrent(); });
}

SerialTransport::~SerialTransport() {
    close();
}

QStringList SerialTransport::availablePorts() {
    QStringList names;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto& p : ports) {
        names << p.portName();
    }
    return names;
}

bool SerialTransport::open(const QString& portName, int baudRate) {
    close();
    serial_.setPortName(portName);
    serial_.setBaudRate(baudRate);
    if (!serial_.open(QIODevice::ReadWrite)) return false;
    assembler_.reset();
    pendingBaudRate_ = 0;
    lastCommand_ = 0;
    portLostSignaled_ = false;
    return true;
}

void SerialTransport::close() {
    ackTimer_.stop();
    retryTimer_.stop();
    sequencer_.reset();
    pendingBaudRate_ = 0;
    lastCommand_ = 0;
    if (serial_.isOpen()) serial_.close();
}

bool SerialTransport::isOpen() const {
    return serial_.isOpen();
}

QString SerialTransport::errorString() const {
    return serial_.errorString();
}

bool SerialTransport::sendCommand(Command command,
                                  const std::vector<std::uint8_t>& value) {
    if (!serial_.isOpen()) return false;
    const auto frame = buildCommand(static_cast<std::uint16_t>(command), value);
    const qint64 size = static_cast<qint64>(frame.size());
    return serial_.write(reinterpret_cast<const char*>(frame.data()), size) == size;
}

bool SerialTransport::startSequence(std::vector<CommandStep> commands) {
    if (!serial_.isOpen() || commands.empty()) return false;
    if (!sequencer_.start(std::move(commands))) return false; // 上一条流程尚未走完
    ackTimer_.stop();
    retryTimer_.stop();
    sendCurrent();
    return true;
}

void SerialTransport::sendCurrent() {
    if (!sequencer_.busy()) return;
    const CommandStep& step = sequencer_.current();
    lastCommand_ = step.command;
    sequencer_.noteSend();
    if (!sendCommand(static_cast<Command>(step.command), step.value)) {
        handleAction(sequencer_.onWriteFailure());
        return;
    }
    ackTimer_.start(ackTimeoutMs_);
}

void SerialTransport::handleAction(SequenceAction action) {
    switch (action) {
    case SequenceAction::Ignore:
        return; // 迟到/无关 ACK，继续等当前命令
    case SequenceAction::Retry:
        ackTimer_.stop();
        retryTimer_.start(kRetryDelayMs);
        return;
    case SequenceAction::SendNext:
        ackTimer_.stop();
        sendCurrent();
        return;
    case SequenceAction::Done:
        ackTimer_.stop();
        retryTimer_.stop();
        emit sequenceFinished();
        onSequenceFinished();
        return;
    case SequenceAction::Fail:
        ackTimer_.stop();
        retryTimer_.stop();
        pendingBaudRate_ = 0;
        emit frameError(tr("命令 0x%1 重试 %2 次后仍无有效 ACK")
                            .arg(lastCommand_, 4, 16, QChar('0'))
                            .arg(sequencer_.maxAttempts()));
        return;
    }
}

void SerialTransport::setAckTimeout(int ms) {
    ackTimeoutMs_ = std::clamp(ms, 80, 5000);
}

bool SerialTransport::queryFirmwareVersion() {
    return startSequence({
        {cmd(Command::EnableConfig), {0x01, 0x00}},
        {cmd(Command::GetFirmwareVersion), {}},
        {cmd(Command::EndConfig), {}},
    });
}

bool SerialTransport::queryParameters() {
    return startSequence({
        {cmd(Command::EnableConfig), {0x01, 0x00}},
        {cmd(Command::GetTargetParams), {}},
        {cmd(Command::GetSensitivity), {}},
        {cmd(Command::EndConfig), {}},
    });
}

bool SerialTransport::setTargetParams(const std::vector<std::uint8_t>& params4) {
    return setParameters(params4, {});
}

bool SerialTransport::setParameters(const std::vector<std::uint8_t>& target4,
                                    const std::vector<std::uint8_t>& sensitivity4) {
    if (target4.size() != 4 || (!sensitivity4.empty() && sensitivity4.size() != 4) ||
        !serial_.isOpen()) return false;
    std::vector<CommandStep> steps;
    steps.reserve(4);
    steps.push_back({cmd(Command::EnableConfig), {0x01, 0x00}});
    steps.push_back({cmd(Command::SetTargetParams), target4});
    if (!sensitivity4.empty()) {
        steps.push_back({cmd(Command::SetSensitivity), sensitivity4});
    }
    steps.push_back({cmd(Command::EndConfig), {}});
    return startSequence(std::move(steps));
}

bool SerialTransport::setBaudRate(int baudRate) {
    const int index = baudIndex(baudRate);
    if (index == 0 || !serial_.isOpen()) return false;
    pendingBaudRate_ = baudRate;
    const auto lo = static_cast<std::uint8_t>(index & 0xFF);
    const auto hi = static_cast<std::uint8_t>((index >> 8) & 0xFF);
    if (!startSequence({
            {cmd(Command::EnableConfig), {0x01, 0x00}},
            {cmd(Command::SetBaudRate), {lo, hi}},
            {cmd(Command::Restart), {}}, // 协议 §5.8：新波特率重启后生效
        })) {
        pendingBaudRate_ = 0;
        return false;
    }
    return true;
}

void SerialTransport::onSequenceFinished() {
    if (pendingBaudRate_ == 0) return;
    const int baud = pendingBaudRate_;
    pendingBaudRate_ = 0;
    // 模块在重启 ACK 发送完成后自动重启，等待其就绪再切换本地波特率
    QTimer::singleShot(800, this, [this, baud] {
        if (!serial_.isOpen()) return;
        if (!serial_.setBaudRate(baud)) {
            emit frameError(tr("本地波特率切换失败：%1").arg(serial_.errorString()));
            return;
        }
        emit baudRateChanged(baud);
    });
}

void SerialTransport::onReadyRead() {
    const QByteArray data = serial_.readAll();
    if (data.isEmpty()) return;
    assembler_.push(reinterpret_cast<const std::uint8_t*>(data.constData()),
                    static_cast<std::size_t>(data.size()));
}

void SerialTransport::onFrame(FrameKind kind, const std::uint8_t* data, std::size_t len) {
    switch (kind) {
    case FrameKind::Data: {
        Scan scan;
        if (!parseDataFrame(data, len, scan)) {
            emit frameError(tr("无效数据帧（长度/帧尾错误）"));
            break;
        }
        emit scanReady(std::make_shared<Scan>(std::move(scan)));
        break;
    }
    case FrameKind::Command: {
        Ack ack;
        if (!parseAckFrame(data, len, ack)) {
            emit frameError(tr("无效 ACK 帧"));
            break;
        }
        const QByteArray payload(
            reinterpret_cast<const char*>(ack.payload.data()),
            static_cast<int>(ack.payload.size()));
        emit ackReady(ack.command, ack.success, payload);
        handleAction(sequencer_.onAck(ack.command, ack.success));
        break;
    }
    case FrameKind::Invalid:
        break; // 组帧器已做重同步，这里不会收到
    }
}

} // namespace ld2451
