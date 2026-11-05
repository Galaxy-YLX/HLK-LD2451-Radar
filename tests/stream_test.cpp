// 基于 2026-11-05 在真实 LD2451（/dev/ttyACM0 @460800）上录制的字节流，
// 回归验证组帧器在真实分片 / 坏长度 / 乱码输入下的行为，以及 ACK 队列状态机。
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "protocol/frame_assembler.h"
#include "protocol/parser.h"
#include "transport/ack_sequencer.h"

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            std::printf("FAIL: %s:%d  ", __FILE__, __LINE__);         \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
            ++g_failures;                                             \
        }                                                             \
    } while (0)

std::vector<std::uint8_t> hexBytes(const std::string& hex) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> out;
    int hi = -1;
    for (char c : hex) {
        const int v = nib(c);
        if (v < 0) continue;
        if (hi < 0) {
            hi = v;
        } else {
            out.push_back(static_cast<std::uint8_t>((hi << 4) | v));
            hi = -1;
        }
    }
    return out;
}

// 实测样本：2 个空闲上报帧 → 握手 5 条 ACK → 3 个空闲上报帧。
// 空闲时模块每约 700 ms 上报一个 10 B 空帧，配置模式下暂停上报、只回 ACK。
const char* kRealStream =
    "f4 f3 f2 f1 00 00 f8 f7 f6 f5 "
    "f4 f3 f2 f1 00 00 f8 f7 f6 f5 "
    "fd fc fb fa 08 00 ff 01 00 00 01 00 00 00 04 03 02 01 "
    "fd fc fb fa 0c 00 a0 01 00 00 51 24 09 01 17 14 10 24 04 03 02 01 "
    "fd fc fb fa 08 00 12 01 00 00 64 01 01 01 04 03 02 01 "
    "fd fc fb fa 08 00 13 01 00 00 02 00 00 00 04 03 02 01 "
    "fd fc fb fa 04 00 fe 01 00 00 04 03 02 01 "
    "f4 f3 f2 f1 00 00 f8 f7 f6 f5 "
    "f4 f3 f2 f1 00 00 f8 f7 f6 f5 "
    "f4 f3 f2 f1 00 00 f8 f7 f6 f5";

const char* kExampleFrame =
    "F4 F3 F2 F1 11 00 03 01 8A 28 00 3C 15 8A 1E 01 3C 0F 76 5F 00 3C 0F F8 F7 F6 F5";

struct Frame {
    ld2451::FrameKind kind;
    std::vector<std::uint8_t> raw;
};

// 按 chunk 大小分片喂入（实测串口每次 read 恰好一帧，也验证逐字节极端分片）
std::vector<Frame> replay(const std::vector<std::uint8_t>& stream, std::size_t chunk,
                          std::size_t* buffered = nullptr) {
    std::vector<Frame> out;
    ld2451::FrameAssembler assembler(
        [&out](ld2451::FrameKind k, const std::uint8_t* d, std::size_t n) {
            out.push_back({k, std::vector<std::uint8_t>(d, d + n)});
        });
    for (std::size_t i = 0; i < stream.size(); i += chunk) {
        assembler.push(stream.data() + i, std::min(chunk, stream.size() - i));
    }
    if (buffered) *buffered = assembler.bufferedBytes();
    return out;
}

std::vector<ld2451::CommandStep> versionFlow() {
    using ld2451::Command;
    return {{static_cast<std::uint16_t>(Command::EnableConfig), {0x01, 0x00}},
            {static_cast<std::uint16_t>(Command::GetFirmwareVersion), {}},
            {static_cast<std::uint16_t>(Command::EndConfig), {}}};
}

} // namespace

int main() {
    using namespace ld2451;

    const auto real = hexBytes(kRealStream);
    CHECK(real.size() == 140, "real sample should be 140 bytes, got %zu", real.size());

    // 1. 真实样本整段喂入：10 帧，顺序与内容都应与示波器/串口录制一致
    {
        std::size_t buffered = 999;
        auto frames = replay(real, real.size(), &buffered);
        CHECK(frames.size() == 10, "real stream should yield 10 frames, got %zu", frames.size());
        CHECK(buffered == 0, "all real bytes should be consumed, left %zu", buffered);
        int dataFrames = 0, ackFrames = 0;
        std::vector<std::uint16_t> ackCommands;
        for (const auto& f : frames) {
            if (f.kind == FrameKind::Data) {
                ++dataFrames;
                Scan scan;
                CHECK(parseDataFrame(f.raw.data(), f.raw.size(), scan) &&
                          scan.target_count == 0 && scan.targets.empty(),
                      "idle frame should parse as an empty scan");
            } else if (f.kind == FrameKind::Command) {
                ++ackFrames;
                Ack ack;
                if (parseAckFrame(f.raw.data(), f.raw.size(), ack)) {
                    ackCommands.push_back(ack.command);
                    CHECK(ack.success, "real ACK 0x%04X should report success", ack.command);
                } else {
                    CHECK(false, "real ACK frame should parse");
                }
            }
        }
        CHECK(dataFrames == 5 && ackFrames == 5,
              "expect 5 idle data frames + 5 ACKs, got %d + %d", dataFrames, ackFrames);
        const std::vector<std::uint16_t> expected = {0x01FF, 0x01A0, 0x0112, 0x0113, 0x01FE};
        CHECK(ackCommands == expected, "ACK command sequence mismatch");

        // 实测固件版本 ACK 的长度字段是 0x0C（12 B），并非协议文档示例的 0x0B
        CHECK(frames.size() > 3 && frames[3].raw.size() == 22 &&
                  frames[3].raw[4] == 0x0C,
              "firmware ACK should use length 0x0C (total 22 B)");
    }

    // 2. 真实分片特性：实测每次 read 恰好一帧；逐字节喂入也必须等价
    for (std::size_t chunk : {std::size_t(10), std::size_t(1), std::size_t(7)}) {
        std::size_t buffered = 999;
        auto frames = replay(real, chunk, &buffered);
        CHECK(frames.size() == 10, "chunk=%zu should still yield 10 frames, got %zu",
              chunk, frames.size());
        CHECK(buffered == 0, "chunk=%zu should consume all bytes, left %zu", chunk, buffered);
    }

    // 3. 坏长度字段：噪声帧头声称 255 B，后面紧跟真实帧；不能等到凑满 265 B 才重同步
    {
        auto stream = hexBytes("F4 F3 F2 F1 FF 00");
        const auto tail = hexBytes(kExampleFrame);
        stream.insert(stream.end(), tail.begin(), tail.end());
        auto frames = replay(stream, stream.size());
        CHECK(frames.size() == 1 && frames[0].kind == FrameKind::Data,
              "should resync to the following real frame, got %zu frames", frames.size());
        if (!frames.empty()) {
            Scan scan;
            CHECK(parseDataFrame(frames[0].raw.data(), frames[0].raw.size(), scan) &&
                      scan.target_count == 3,
                  "resynced frame should parse with 3 targets");
        }
    }

    // 4. 帧尾损坏的伪帧不应吞掉紧随其后的完整帧
    {
        auto stream = hexBytes("F4 F3 F2 F1 00 00 00 00 00 00");
        const auto tail = hexBytes(kExampleFrame);
        stream.insert(stream.end(), tail.begin(), tail.end());
        auto frames = replay(stream, stream.size());
        CHECK(frames.size() == 1, "broken frame should be skipped without eating next one, got %zu",
              frames.size());
        if (frames.size() == 1) {
            Scan scan;
            CHECK(parseDataFrame(frames[0].raw.data(), frames[0].raw.size(), scan) &&
                      scan.target_count == 3,
                  "following frame should remain intact");
        }
    }

    // 5. 波特率不匹配（实测为纯乱码）时缓冲区必须有界，且不得误报帧
    {
        ld2451::FrameAssembler assembler(
            [](ld2451::FrameKind, const std::uint8_t*, std::size_t) {
                CHECK(false, "garbage input must not produce frames");
            });
        std::vector<std::uint8_t> noise(200000);
        for (std::size_t i = 0; i < noise.size(); ++i) {
            std::uint8_t v = static_cast<std::uint8_t>((i * 37 + 11) & 0xFF);
            if (v == 0xF4 || v == 0xFD) v = 0x6B; // 避免偶然出现帧头首字节
            noise[i] = v;
        }
        for (std::size_t i = 0; i < noise.size(); i += 997) {
            assembler.push(noise.data() + i, std::min<std::size_t>(997, noise.size() - i));
        }
        CHECK(assembler.bufferedBytes() <= FrameAssembler::kMaxBufferSize,
              "buffer must stay bounded, got %zu", assembler.bufferedBytes());
    }

    // 6. ACK 队列：正常一问一答
    {
        AckSequencer seq(3);
        CHECK(seq.start(versionFlow()), "sequence should start");
        CHECK(seq.busy() && seq.remaining() == 3, "3 steps pending");
        seq.noteSend();
        CHECK(seq.onAck(0x01FF, true) == SequenceAction::SendNext, "enable ACK advances");
        seq.noteSend();
        CHECK(seq.onAck(0x01A0, true) == SequenceAction::SendNext, "version ACK advances");
        seq.noteSend();
        CHECK(seq.onAck(0x01FE, true) == SequenceAction::Done, "end ACK finishes");
        CHECK(!seq.busy(), "sequence should be finished");
    }

    // 7. 实测偶发单条命令无应答：快速重发当前命令，重试用尽才失败
    {
        AckSequencer seq(3);
        CHECK(seq.start(versionFlow()), "sequence should start");
        seq.noteSend();
        CHECK(seq.onTimeout() == SequenceAction::Retry, "1st timeout retries");
        seq.noteSend();
        CHECK(seq.onTimeout() == SequenceAction::Retry, "2nd timeout retries");
        seq.noteSend();
        CHECK(seq.onTimeout() == SequenceAction::Fail, "3rd timeout fails the sequence");
        CHECK(!seq.busy(), "failed sequence should be cleared");
        CHECK(seq.onAck(0x01FF, true) == SequenceAction::Ignore, "late ACK is ignored");
    }

    // 8. 重发带来的迟到/重复 ACK、无关 ACK、失败状态都不得打断队列
    {
        AckSequencer seq(3);
        CHECK(seq.start(versionFlow()), "sequence should start");
        CHECK(!seq.start(versionFlow()), "busy sequence must reject a new flow");
        seq.noteSend();
        CHECK(seq.onAck(0x0112, true) == SequenceAction::Ignore, "unrelated ACK ignored");
        CHECK(seq.onAck(0x01FF, false) == SequenceAction::Retry, "failed ACK retries");
        seq.noteSend();
        CHECK(seq.onAck(0x01FF, true) == SequenceAction::SendNext, "retry ACK advances");
        seq.noteSend();
        CHECK(seq.onAck(0x01FF, true) == SequenceAction::Ignore,
              "late duplicate ACK must not break the queue");
        CHECK(seq.remaining() == 2, "queue should still have 2 steps");
        CHECK(seq.onAck(0x01A0, true) == SequenceAction::SendNext, "version ACK advances");
        seq.noteSend();
        CHECK(seq.onAck(0x01FE, true) == SequenceAction::Done, "end ACK finishes");
    }

    // 9. 用真实录制的 ACK 序列回放（含脚本额外发送 END 造成的重复 0x01FE）
    {
        AckSequencer seq(3);
        CHECK(seq.start(versionFlow()), "sequence should start");
        const std::uint16_t stream[] = {0x01FF, 0x01A0, 0x01FE, 0x01FE};
        seq.noteSend();
        CHECK(seq.onAck(stream[0], true) == SequenceAction::SendNext, "enable");
        // 实测版本查询偶发丢 ACK，重发后收到版本 ACK
        seq.noteSend();
        CHECK(seq.onAck(stream[1], true) == SequenceAction::SendNext, "version");
        seq.noteSend();
        CHECK(seq.onAck(stream[2], true) == SequenceAction::Done, "end");
        CHECK(seq.onAck(stream[3], true) == SequenceAction::Ignore, "duplicate end ignored");
    }

    if (g_failures == 0) {
        std::printf("All stream/sequencer tests passed.\n");
        return 0;
    }
    std::printf("%d stream/sequencer test(s) failed.\n", g_failures);
    return 1;
}
