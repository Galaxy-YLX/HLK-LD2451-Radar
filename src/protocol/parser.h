#pragma once

#include "core/scan.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ld2451 {

// 上报数据帧：F4 F3 F2 F1 | 长度(2B 小端) | 帧内数据 | F8 F7 F6 F5
inline constexpr std::uint8_t kDataHeader[4] = {0xF4, 0xF3, 0xF2, 0xF1};
inline constexpr std::uint8_t kDataTail[4] = {0xF8, 0xF7, 0xF6, 0xF5};
// 命令 / ACK 帧：FD FC FB FA | 长度(2B 小端) | 帧内数据 | 04 03 02 01
inline constexpr std::uint8_t kCmdHeader[4] = {0xFD, 0xFC, 0xFB, 0xFA};
inline constexpr std::uint8_t kCmdTail[4] = {0x04, 0x03, 0x02, 0x01};

inline constexpr std::size_t kHeaderSize = 4;
inline constexpr std::size_t kLengthSize = 2;
inline constexpr std::size_t kTailSize = 4;
inline constexpr std::size_t kFrameOverhead = kHeaderSize + kLengthSize + kTailSize; // 10
inline constexpr std::size_t kMaxPayload = 512; // 长度字段异常时的重同步保护

enum class FrameKind {
    Invalid, // 帧头 / 长度 / 帧尾错误
    Data,    // 雷达上报数据帧
    Command, // 命令帧（发送）/ ACK 帧（接收）
};

// 命令字（协议 §4）
enum class Command : std::uint16_t {
    EnableConfig = 0x00FF,
    EndConfig = 0x00FE,
    SetTargetParams = 0x0002,
    GetTargetParams = 0x0012,
    SetSensitivity = 0x0003,
    GetSensitivity = 0x0013,
    GetFirmwareVersion = 0x00A0,
    SetBaudRate = 0x00A1,
    FactoryReset = 0x00A2,
    Restart = 0x00A3,
};

// ACK 解析结果（payload 为「状态」之后的返回内容）
struct Ack {
    std::uint16_t command = 0; // ACK 命令字 = 原命令字 | 0x0100
    bool success = false;      // 状态 0x0000 成功 / 0x0001 失败
    std::vector<std::uint8_t> payload;
};

// 判定帧头类型（至少需 4 字节）
FrameKind frameKind(const std::uint8_t* data, std::size_t len);

// 完整帧长（字节）= kFrameOverhead + 长度字段；无效帧返回 0。
// data 指向帧首，len 为当前可用字节数（至少 6）。
std::size_t frameLength(const std::uint8_t* data, std::size_t len);

// 校验整帧尾部是否与该帧类型匹配（data 指向帧首，len 为整帧长度）
bool verifyTail(FrameKind kind, const std::uint8_t* data, std::size_t len);

// 解析上报数据帧（整帧，含头尾）。长度字段为 0 的空帧表示「无目标」。
bool parseDataFrame(const std::uint8_t* data, std::size_t len, Scan& scan);

// 解析 ACK 帧（整帧，含头尾）
bool parseAckFrame(const std::uint8_t* data, std::size_t len, Ack& ack);

// 构造命令帧
std::vector<std::uint8_t> buildCommand(std::uint16_t command,
                                       const std::vector<std::uint8_t>& value = {});

} // namespace ld2451
