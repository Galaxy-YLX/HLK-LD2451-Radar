#pragma once

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/scan.h"
#include "protocol/frame_assembler.h"
#include "protocol/parser.h"

namespace ld2451 {

// 串口传输 + 帧解析：读字节流 → 组帧 → 解析成 Scan / Ack → 发出信号。
// 基于 readyRead 异步非阻塞，运行在主线程（与 EaiLidar Viewer 同一方案）。
class SerialTransport : public QObject {
    Q_OBJECT
public:
    explicit SerialTransport(QObject* parent = nullptr);
    ~SerialTransport() override;

    // 列出可用串口（如 /dev/ttyUSB0、COM3）
    static QStringList availablePorts();

    // 打开串口，返回是否成功；失败时用 errorString() 取原因。
    bool open(const QString& portName, int baudRate = 115200);
    void close();
    bool isOpen() const;
    QString errorString() const;

    // 协议 §3：配置/查询命令必须夹在「使能配置」与「结束配置」之间。
    // 以下方法把整条流程一次发出，雷达会按字节流顺序依次回 ACK。
    bool queryFirmwareVersion();
    bool queryParameters();

signals:
    void scanReady(const std::shared_ptr<ld2451::Scan>& scan);
    void ackReady(quint16 command, bool success, const QByteArray& payload);
    void frameError(const QString& message);

private slots:
    void onReadyRead();

private:
    void onFrame(FrameKind kind, const std::uint8_t* data, std::size_t len);
    bool sendCommand(Command command, const std::vector<std::uint8_t>& value = {});

    QSerialPort serial_;
    FrameAssembler assembler_;
};

} // namespace ld2451
