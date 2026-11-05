#include "protocol/parser.h"

#include <algorithm>
#include <cmath>

namespace ld2451 {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint16_t readU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

const std::uint8_t* tailOf(FrameKind kind) {
    switch (kind) {
    case FrameKind::Data: return kDataTail;
    case FrameKind::Command: return kCmdTail;
    case FrameKind::Invalid: return nullptr;
    }
    return nullptr;
}

} // namespace

FrameKind frameKind(const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len < kHeaderSize) return FrameKind::Invalid;
    if (std::equal(kDataHeader, kDataHeader + kHeaderSize, data)) return FrameKind::Data;
    if (std::equal(kCmdHeader, kCmdHeader + kHeaderSize, data)) return FrameKind::Command;
    return FrameKind::Invalid;
}

std::size_t frameLength(const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len < kHeaderSize + kLengthSize) return 0;
    if (frameKind(data, len) == FrameKind::Invalid) return 0;
    return kFrameOverhead + readU16(data + kHeaderSize);
}

bool verifyTail(FrameKind kind, const std::uint8_t* data, std::size_t len) {
    const std::uint8_t* tail = tailOf(kind);
    if (tail == nullptr || data == nullptr || len < kFrameOverhead) return false;
    return std::equal(tail, tail + kTailSize, data + len - kTailSize);
}

bool parseDataFrame(const std::uint8_t* data, std::size_t len, Scan& scan) {
    scan = Scan{};
    if (len < kFrameOverhead || frameKind(data, len) != FrameKind::Data) return false;

    // 帧内数据长度以整帧长度为准，长度字段仅作参考：
    // 协议文档 §5.7 的固件版本 ACK 把 12 字节数据标成了 0x0B，按字段切帧会丢帧。
    const std::size_t payload = len - kFrameOverhead;
    if (payload > kMaxPayload) return false;
    if (!verifyTail(FrameKind::Data, data, len)) return false;
    if (payload == 0) return true; // 未检测到目标：仅帧头、长度、帧尾

    // 帧内数据 = 目标数量(1B) + 报警信息(1B) + N × 5B
    if (payload < 2 || (payload - 2) % 5 != 0) return false;
    const std::uint8_t* p = data + kHeaderSize + kLengthSize;
    const int count = static_cast<int>(p[0]);
    const std::size_t available = (payload - 2) / 5;
    if (count < 0 || static_cast<std::size_t>(count) > available) return false;

    scan.target_count = count;
    scan.alarm_approaching = (p[1] == 0x01);
    scan.targets.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        const std::uint8_t* t = p + 2 + 5 * i;
        Target tg;
        tg.angle_deg = static_cast<double>(t[0]) - 0x80; // 实际角度 = 上报值 - 0x80
        tg.distance_m = static_cast<double>(t[1]);
        tg.direction_flag = t[2];
        tg.speed_kmh = static_cast<double>(t[3]);
        tg.snr = static_cast<int>(t[4]);
        tg.approaching = (t[2] == 0x01); // 协议 §6.2：0x01 靠近 / 0x00 远离
        tg.signed_speed_kmh = tg.approaching ? tg.speed_kmh : -tg.speed_kmh;

        const double rad = tg.angle_deg * kPi / 180.0;
        tg.x_m = tg.distance_m * std::sin(rad);
        tg.y_m = tg.distance_m * std::cos(rad);
        scan.targets.push_back(tg);
    }
    return true;
}

bool parseAckFrame(const std::uint8_t* data, std::size_t len, Ack& ack) {
    ack = Ack{};
    if (len < kFrameOverhead || frameKind(data, len) != FrameKind::Command) return false;

    // 同上：长度字段可能有笔误，按整帧长度推断
    const std::size_t payload = len - kFrameOverhead;
    if (payload < 4 || payload > kMaxPayload) return false;
    if (!verifyTail(FrameKind::Command, data, len)) return false;

    const std::uint8_t* p = data + kHeaderSize + kLengthSize;
    ack.command = readU16(p);
    if ((ack.command & 0x0100u) == 0) return false; // 不是 ACK（可能是自发命令帧）
    ack.success = readU16(p + 2) == 0x0000;
    ack.payload.assign(p + 4, p + payload);
    return true;
}

std::vector<std::uint8_t> buildCommand(std::uint16_t command,
                                       const std::vector<std::uint8_t>& value) {
    const std::size_t payload = 2 + value.size(); // 命令字 2 字节 + 命令值
    std::vector<std::uint8_t> frame;
    frame.reserve(kFrameOverhead + payload);
    frame.insert(frame.end(), kCmdHeader, kCmdHeader + kHeaderSize);
    frame.push_back(static_cast<std::uint8_t>(payload & 0xFF));
    frame.push_back(static_cast<std::uint8_t>((payload >> 8) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(command & 0xFF));
    frame.push_back(static_cast<std::uint8_t>((command >> 8) & 0xFF));
    frame.insert(frame.end(), value.begin(), value.end());
    frame.insert(frame.end(), kCmdTail, kCmdTail + kTailSize);
    return frame;
}

} // namespace ld2451
