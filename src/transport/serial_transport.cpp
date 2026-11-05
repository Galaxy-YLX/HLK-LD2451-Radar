#include "transport/serial_transport.h"

#include <QSerialPortInfo>

namespace ld2451 {

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
    return true;
}

void SerialTransport::close() {
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

bool SerialTransport::queryFirmwareVersion() {
    return sendCommand(Command::EnableConfig, {0x01, 0x00}) &&
           sendCommand(Command::GetFirmwareVersion) &&
           sendCommand(Command::EndConfig);
}

bool SerialTransport::queryParameters() {
    return sendCommand(Command::EnableConfig, {0x01, 0x00}) &&
           sendCommand(Command::GetTargetParams) &&
           sendCommand(Command::GetSensitivity) &&
           sendCommand(Command::EndConfig);
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
        break;
    }
    case FrameKind::Invalid:
        break; // 组帧器已做重同步，这里不会收到
    }
}

} // namespace ld2451
