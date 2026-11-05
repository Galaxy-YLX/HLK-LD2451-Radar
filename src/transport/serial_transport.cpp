#include "transport/serial_transport.h"

#include <QSerialPortInfo>

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
                if (err != QSerialPort::NoError) {
                    emit frameError(serial_.errorString());
                }
            });

    // 一问一答流程中，某条命令迟迟没有 ACK 时给出明确提示并中止队列
    ackTimer_.setSingleShot(true);
    ackTimer_.setInterval(1000);
    connect(&ackTimer_, &QTimer::timeout, this, [this] {
        if (pending_.empty()) return;
        const auto cmd = static_cast<std::uint16_t>(pending_.front().command);
        pending_.clear();
        pendingBaudRate_ = 0;
        emit frameError(tr("命令 0x%1 等待 ACK 超时").arg(cmd, 4, 16, QChar('0')));
    });
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
    serial_.setPortName(portName);
    serial_.setBaudRate(baudRate);
    if (!serial_.open(QIODevice::ReadWrite)) return false;
    assembler_.reset();
    pending_.clear();
    pendingBaudRate_ = 0;
    ackTimer_.stop();
    return true;
}

void SerialTransport::close() {
    ackTimer_.stop();
    pending_.clear();
    pendingBaudRate_ = 0;
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

bool SerialTransport::startSequence(std::vector<PendingCommand> commands) {
    if (!serial_.isOpen() || commands.empty()) return false;
    if (!pending_.empty()) return false; // 上一条流程尚未走完，避免中途打断
    pending_.clear();
    for (auto& c : commands) {
        pending_.push_back(std::move(c));
    }
    sendNext();
    return true;
}

void SerialTransport::sendNext() {
    if (pending_.empty()) {
        ackTimer_.stop();
        onSequenceFinished();
        return;
    }
    const PendingCommand& cmd = pending_.front();
    if (!sendCommand(cmd.command, cmd.value)) {
        pending_.clear();
        pendingBaudRate_ = 0;
        emit frameError(tr("命令写入失败"));
        return;
    }
    ackTimer_.start();
}

bool SerialTransport::queryFirmwareVersion() {
    return startSequence({
        {Command::EnableConfig, {0x01, 0x00}},
        {Command::GetFirmwareVersion, {}},
        {Command::EndConfig, {}},
    });
}

bool SerialTransport::queryParameters() {
    return startSequence({
        {Command::EnableConfig, {0x01, 0x00}},
        {Command::GetTargetParams, {}},
        {Command::GetSensitivity, {}},
        {Command::EndConfig, {}},
    });
}

bool SerialTransport::setBaudRate(int baudRate) {
    const int index = baudIndex(baudRate);
    if (index == 0 || !serial_.isOpen()) return false;
    pendingBaudRate_ = baudRate;
    const auto lo = static_cast<std::uint8_t>(index & 0xFF);
    const auto hi = static_cast<std::uint8_t>((index >> 8) & 0xFF);
    if (!startSequence({
            {Command::EnableConfig, {0x01, 0x00}},
            {Command::SetBaudRate, {lo, hi}},
            {Command::Restart, {}}, // 协议 §5.8：新波特率重启后生效
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

        // 推进一问一答队列：只有当前命令的 ACK 成功，才发下一条
        if (!pending_.empty()) {
            ackTimer_.stop();
            const auto expected =
                static_cast<std::uint16_t>(pending_.front().command) | 0x0100u;
            if (!ack.success || ack.command != expected) {
                pending_.clear();
                pendingBaudRate_ = 0;
                emit frameError(tr("命令 ACK 异常：0x%1")
                                    .arg(ack.command, 4, 16, QChar('0')));
            } else {
                pending_.pop_front();
                sendNext();
            }
        }
        break;
    }
    case FrameKind::Invalid:
        break; // 组帧器已做重同步，这里不会收到
    }
}

} // namespace ld2451
