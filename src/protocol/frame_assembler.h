#pragma once

#include "protocol/parser.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ld2451 {

// 把串口字节流切成完整帧：同时识别「上报数据帧」与「命令/ACK 帧」，
// 按长度字段定长、校验帧尾；帧头/帧尾不匹配时逐字节重同步。
class FrameAssembler {
public:
    using FrameCallback =
        std::function<void(FrameKind kind, const std::uint8_t* data, std::size_t len)>;

    explicit FrameAssembler(FrameCallback onFrame) : onFrame_(std::move(onFrame)) {}

    // 喂入一段字节，可能触发 0 次或多次 onFrame 回调。
    void push(const std::uint8_t* data, std::size_t len);

    void reset() { buf_.clear(); }

private:
    FrameCallback onFrame_;
    std::vector<std::uint8_t> buf_;
};

} // namespace ld2451
