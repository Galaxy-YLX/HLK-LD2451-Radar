#pragma once

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
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
    bool open(const QString& portName, int baudRate = 460800);
    void close();
    bool isOpen() const;
    QString errorString() const;

    // 协议 §3：使能配置 → 查询命令 → 结束配置。
    // 严格按「发一条、等该条 ACK 成功、再发下一条」执行，并带 ACK 超时保护。
    bool queryFirmwareVersion();
    bool queryParameters();

    // 修改目标检测参数（协议 §5.3）：使能配置 → 0x0002(4B) → 结束配置。
    bool setTargetParams(const std::vector<std::uint8_t>& params4);

    // 修改模块波特率（协议 §5.8）：使能配置 → 0x00A1 → 重启模块使其生效，
    // 全部 ACK 成功后延时切换本地串口波特率，并发出 baudRateChanged。
    bool setBaudRate(int baudRate);

signals:
    void scanReady(const std::shared_ptr<ld2451::Scan>& scan);
    void ackReady(quint16 command, bool success, const QByteArray& payload);
    void frameError(const QString& message);
    void baudRateChanged(int baudRate);

private slots:
    void onReadyRead();

private:
    struct PendingCommand {
        Command command;
        std::vector<std::uint8_t> value;
    };

    void onFrame(FrameKind kind, const std::uint8_t* data, std::size_t len);
    bool sendCommand(Command command, const std::vector<std::uint8_t>& value = {});
    bool startSequence(std::vector<PendingCommand> commands);
    void sendNext();
    void onSequenceFinished();

    QSerialPort serial_;
    FrameAssembler assembler_;
    std::deque<PendingCommand> pending_;
    QTimer ackTimer_; // 单发超时：等待当前命令的 ACK
    int pendingBaudRate_ = 0;
};

} // namespace ld2451
