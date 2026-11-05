#pragma once

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "core/scan.h"
#include "protocol/frame_assembler.h"
#include "protocol/parser.h"
#include "transport/ack_sequencer.h"

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

    // 单条命令等待 ACK 的超时（ms）。实测 LD2451 ACK 往返 10~62 ms，默认 200 ms；
    // 超时后自动重发当前命令（最多 3 次，间隔 80 ms），不必要地等 1~2 s 会拖慢连接。
    void setAckTimeout(int ms);

    // 协议 §3：使能配置 → 查询命令 → 结束配置。
    // 严格按「发一条、等该条 ACK 成功、再发下一条」执行，并带 ACK 超时/重发保护。
    bool queryFirmwareVersion();
    bool queryParameters();

    // 修改目标检测参数（协议 §5.3）：使能配置 → 0x0002(4B) → 结束配置。
    bool setTargetParams(const std::vector<std::uint8_t>& params4);

    // 同一条配置流程里写「检测参数(§5.3) + 灵敏度参数(§5.5)」：
    // 使能配置 → 0x0002 → [0x0003] → 结束配置，避免两条 ACK 队列互相打断。
    // sensitivity4 为空时只写检测参数。
    bool setParameters(const std::vector<std::uint8_t>& target4,
                       const std::vector<std::uint8_t>& sensitivity4 = {});

    // 修改模块波特率（协议 §5.8）：使能配置 → 0x00A1 → 重启模块使其生效，
    // 全部 ACK 成功后延时切换本地串口波特率，并发出 baudRateChanged。
    bool setBaudRate(int baudRate);

signals:
    void scanReady(const std::shared_ptr<ld2451::Scan>& scan);
    void ackReady(quint16 command, bool success, const QByteArray& payload);
    void frameError(const QString& message);
    void baudRateChanged(int baudRate);
    void sequenceFinished();              // 一整条命令队列全部 ACK 成功
    void portLost(const QString& reason); // 设备拔出 / 串口失效

private slots:
    void onReadyRead();

private:
    void onFrame(FrameKind kind, const std::uint8_t* data, std::size_t len);
    bool sendCommand(Command command, const std::vector<std::uint8_t>& value = {});
    bool startSequence(std::vector<CommandStep> commands);
    void sendCurrent();
    void handleAction(SequenceAction action);
    void onSequenceFinished();

    QSerialPort serial_;
    FrameAssembler assembler_;
    AckSequencer sequencer_;
    QTimer ackTimer_;  // 单发超时：等待当前命令的 ACK
    QTimer retryTimer_; // 重发间隔：错开模块忙窗口
    static constexpr int kRetryDelayMs = 80;
    int ackTimeoutMs_ = 200;
    int pendingBaudRate_ = 0;
    quint16 lastCommand_ = 0;
    bool portLostSignaled_ = false;
};

} // namespace ld2451
