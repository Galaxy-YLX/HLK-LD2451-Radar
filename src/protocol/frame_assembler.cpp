#include "protocol/frame_assembler.h"

namespace ld2451 {

void FrameAssembler::push(const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0) return;
    buf_.insert(buf_.end(), data, data + len);

    std::size_t i = 0;
    while (i + kHeaderSize + kLengthSize <= buf_.size()) {
        const std::size_t available = buf_.size() - i;
        const FrameKind kind = frameKind(buf_.data() + i, available);
        if (kind == FrameKind::Invalid) {
            ++i;
            continue;
        }

        const std::size_t total = frameLength(buf_.data() + i, available);
        if (total == 0 || total > kFrameOverhead + kMaxPayload) {
            ++i; // 长度字段异常，跳过该帧头重新同步
            continue;
        }
        if (i + total > buf_.size()) break; // 帧尚未收完

        if (!verifyTail(kind, buf_.data() + i, total)) {
            // 帧尾不符：优先怀疑长度字段有偏差（协议文档 §5.7 有此类笔误），
            // 在 ±4 字节内重新定位帧尾；仍失败才跳过该帧头重同步。
            bool recovered = false;
            for (int delta = -4; delta <= 4; ++delta) {
                if (delta == 0) continue;
                const auto candidate = static_cast<std::ptrdiff_t>(total) + delta;
                if (candidate < static_cast<std::ptrdiff_t>(kFrameOverhead)) continue;
                const auto len = static_cast<std::size_t>(candidate);
                if (len > kFrameOverhead + kMaxPayload || i + len > buf_.size()) continue;
                if (!verifyTail(kind, buf_.data() + i, len)) continue;
                onFrame_(kind, buf_.data() + i, len);
                i += len;
                recovered = true;
                break;
            }
            if (!recovered) ++i;
            continue;
        }
        onFrame_(kind, buf_.data() + i, total);
        i += total;
    }

    // 丢弃已消费字节，保留未消费尾部（可能含半个帧头）
    if (i > 0) buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(i));
}

} // namespace ld2451
