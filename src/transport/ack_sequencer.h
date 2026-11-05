#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <utility>
#include <vector>

namespace ld2451 {

// 一问一答流程中的一条命令（命令字为原值，不含 ACK 的 0x0100）
struct CommandStep {
    std::uint16_t command = 0;
    std::vector<std::uint8_t> value;
};

enum class SequenceAction {
    Ignore,   // 迟到 / 无关 ACK：继续等待当前命令的 ACK
    SendNext, // 当前 ACK 成功，立即发送下一条
    Retry,    // 重发当前命令（稍等片刻后）
    Fail,     // 重试用尽，放弃整条流程
    Done,     // 队列全部成功
};

// 纯 C++ 的 ACK 队列状态机，不依赖 Qt，便于单测。
// 实测（LD2451 @460800）：单条命令偶发无应答，但后续命令正常应答，
// 且重发会带来「迟到的重复 ACK」，因此非期望 ACK 一律忽略而不打断队列。
class AckSequencer {
public:
    explicit AckSequencer(int maxAttempts = 3)
        : maxAttempts_(maxAttempts < 1 ? 1 : maxAttempts) {}

    bool start(std::vector<CommandStep> steps) {
        if (busy() || steps.empty()) return false;
        steps_.assign(std::make_move_iterator(steps.begin()),
                      std::make_move_iterator(steps.end()));
        attempts_ = 0;
        return true;
    }

    void reset() {
        steps_.clear();
        attempts_ = 0;
    }

    bool busy() const { return !steps_.empty(); }
    std::size_t remaining() const { return steps_.size(); }
    int attempt() const { return attempts_; }
    int maxAttempts() const { return maxAttempts_; }
    const CommandStep& current() const { return steps_.front(); }

    // 每次实际写串口前调用，用于统计当前命令的第几次尝试
    void noteSend() { ++attempts_; }

    SequenceAction onAck(std::uint16_t ackCommand, bool success) {
        if (!busy()) return SequenceAction::Ignore;
        const std::uint16_t expected = static_cast<std::uint16_t>(current().command | 0x0100u);
        if (ackCommand != expected) return SequenceAction::Ignore;
        if (!success) return failedAttempt();
        steps_.pop_front();
        attempts_ = 0;
        return steps_.empty() ? SequenceAction::Done : SequenceAction::SendNext;
    }

    SequenceAction onTimeout() { return failedAttempt(); }

    SequenceAction onWriteFailure() {
        reset();
        return SequenceAction::Fail;
    }

private:
    SequenceAction failedAttempt() {
        if (!busy()) return SequenceAction::Fail;
        if (attempts_ < maxAttempts_) return SequenceAction::Retry;
        reset();
        return SequenceAction::Fail;
    }

    std::deque<CommandStep> steps_;
    int attempts_ = 0;
    int maxAttempts_ = 3;
};

} // namespace ld2451
