#include "protocol/frame_assembler.h"

#include <algorithm>
#include <limits>

namespace ld2451 {
namespace {

constexpr std::size_t kNpos = std::numeric_limits<std::size_t>::max();
// 尾部至少保留这么多字节，避免丢弃「半个帧头」
constexpr std::size_t kReserve = kHeaderSize + kLengthSize - 1;

// 找 [from, limit) 内第一个合法帧头（4 字节完全匹配）
std::size_t findHeader(const std::vector<std::uint8_t>& buf, std::size_t from,
                       std::size_t limit) {
    for (std::size_t j = from; j + kHeaderSize <= limit; ++j) {
        if (buf[j] != kDataHeader[0] && buf[j] != kCmdHeader[0]) continue;
        if (frameKind(buf.data() + j, buf.size() - j) != FrameKind::Invalid) return j;
    }
    return kNpos;
}

// 找 [from, limit) 内第一个「能构成完整合法帧」的帧头（长度字段合理且帧尾匹配）。
// 用于判定当前帧头后的数据其实属于下一帧，避免坏长度字段把后续帧卡住。
std::size_t findCompleteFrame(const std::vector<std::uint8_t>& buf, std::size_t from,
                              std::size_t limit) {
    limit = std::min(limit, buf.size());
    for (std::size_t j = from; j + kFrameOverhead <= limit; ++j) {
        if (buf[j] != kDataHeader[0] && buf[j] != kCmdHeader[0]) continue;
        const FrameKind kind = frameKind(buf.data() + j, limit - j);
        if (kind == FrameKind::Invalid) continue;
        const std::size_t total = frameLength(buf.data() + j, limit - j);
        if (total == 0 || total > kFrameOverhead + kMaxPayload || j + total > limit) continue;
        if (verifyTail(kind, buf.data() + j, total)) return j;
    }
    return kNpos;
}

} // namespace

void FrameAssembler::push(const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0) return;
    buf_.insert(buf_.end(), data, data + len);

    // 异常流（波特率不匹配 / 噪声）下限制缓冲，同时对齐到最近的合法帧头
    if (buf_.size() > kMaxBufferSize) {
        const std::size_t cut = buf_.size() - kMaxBufferSize / 2;
        const std::size_t head = findHeader(buf_, cut, buf_.size());
        const std::size_t keep = (head != kNpos) ? head
                               : (buf_.size() > kReserve ? buf_.size() - kReserve : 0);
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(keep));
    }

    std::size_t i = 0;
    while (i + kHeaderSize + kLengthSize <= buf_.size()) {
        const std::size_t available = buf_.size() - i;
        const FrameKind kind = frameKind(buf_.data() + i, available);
        if (kind == FrameKind::Invalid) {
            const std::size_t next = findHeader(buf_, i + 1, buf_.size());
            if (next == kNpos) {
                i = std::max(i, buf_.size() - kReserve);
                break;
            }
            i = next;
            continue;
        }

        const std::size_t total = frameLength(buf_.data() + i, available);
        if (total == 0 || total > kFrameOverhead + kMaxPayload) {
            const std::size_t next = findHeader(buf_, i + 1, buf_.size());
            if (next == kNpos) {
                i = std::max(i, buf_.size() - kReserve);
                break;
            }
            i = next;
            continue;
        }

        if (i + total > buf_.size()) {
            // 帧未收满：后面若已出现完整合法帧，说明当前长度字段是坏值，立即重同步。
            // 实测空闲帧间隔约 700 ms，盲等 total 字节会让后续帧卡住数秒。
            const std::size_t next = findCompleteFrame(buf_, i + 1, buf_.size());
            if (next != kNpos) {
                i = next;
                continue;
            }
            break;
        }

        if (!verifyTail(kind, buf_.data() + i, total)) {
            // 后面已有完整帧时优先重同步到它，避免 ±4 恢复把后续帧一并吞掉
            const std::size_t next =
                findCompleteFrame(buf_, i + 1, std::min(buf_.size(), i + total + kTailSize));
            if (next != kNpos) {
                i = next;
                continue;
            }

            // 帧尾不符：优先怀疑长度字段有偏差（协议文档 §5.7 有此类笔误），
            // 在 ±4 字节内重新定位帧尾；仍失败才跳过该帧头重同步。
            bool recovered = false;
            for (int delta = -4; delta <= 4; ++delta) {
                if (delta == 0) continue;
                const auto candidate = static_cast<std::ptrdiff_t>(total) + delta;
                if (candidate < static_cast<std::ptrdiff_t>(kFrameOverhead)) continue;
                const auto candidateLen = static_cast<std::size_t>(candidate);
                if (candidateLen > kFrameOverhead + kMaxPayload ||
                    i + candidateLen > buf_.size()) continue;
                if (!verifyTail(kind, buf_.data() + i, candidateLen)) continue;
                onFrame_(kind, buf_.data() + i, candidateLen);
                i += candidateLen;
                recovered = true;
                break;
            }
            if (!recovered) {
                const std::size_t nh = findHeader(buf_, i + 1, buf_.size());
                if (nh == kNpos) {
                    i = std::max(i, buf_.size() - kReserve);
                    break;
                }
                i = nh;
            }
            continue;
        }
        onFrame_(kind, buf_.data() + i, total);
        i += total;
    }

    // 丢弃已消费字节，保留未消费尾部（可能含半个帧头）
    if (i > 0) buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(i));
}

} // namespace ld2451
