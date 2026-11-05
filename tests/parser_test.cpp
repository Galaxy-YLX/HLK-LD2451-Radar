#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "protocol/frame_assembler.h"
#include "protocol/parser.h"

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

bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) < eps;
}

// 从十六进制字符串构造字节（空格分隔）
std::vector<std::uint8_t> hexBytes(const std::string& hex) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); ++i) {
        if (hex[i] == ' ') continue;
        const int hi = nib(hex[i]);
        const int lo = nib(hex[i + 1]);
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        ++i;
    }
    return out;
}

// 协议文档 §7 的原始示例帧（长度字段 0x11 = 17 = 2 + 3×5）
const char* kExampleFrame =
    "F4 F3 F2 F1 11 00 03 01 8A 28 00 3C 15 8A 1E 01 3C 0F 76 5F 00 3C 0F F8 F7 F6 F5";

} // namespace

int main() {
    using namespace ld2451;

    // 0. 帧类型与帧长
    {
        auto fr = hexBytes(kExampleFrame);
        CHECK(frameKind(fr.data(), fr.size()) == FrameKind::Data, "example should be Data frame");
        CHECK(frameLength(fr.data(), fr.size()) == 27, "frame length should be 27, got %zu",
              frameLength(fr.data(), fr.size()));
        CHECK(verifyTail(FrameKind::Data, fr.data(), fr.size()), "data tail should match");

        const std::uint8_t bad[4] = {0x00, 0x01, 0x02, 0x03};
        CHECK(frameKind(bad, 4) == FrameKind::Invalid, "unknown header should be Invalid");
        CHECK(frameLength(bad, 4) == 0, "too-short buffer should give 0");
    }

    // 1. 上报数据帧解析（协议文档 §7 算例）
    {
        auto fr = hexBytes(kExampleFrame);
        Scan scan;
        CHECK(parseDataFrame(fr.data(), fr.size(), scan), "example frame should parse");
        CHECK(scan.target_count == 3, "3 targets, got %d", scan.target_count);
        CHECK(scan.alarm_approaching, "alarm should be set");
        CHECK(scan.targets.size() == 3, "3 target records, got %zu", scan.targets.size());

        CHECK(near(scan.targets[0].angle_deg, 10.0), "T1 angle 10, got %.2f",
              scan.targets[0].angle_deg);
        CHECK(near(scan.targets[0].distance_m, 40.0), "T1 distance 40 m, got %.2f",
              scan.targets[0].distance_m);
        CHECK(near(scan.targets[0].speed_kmh, 60.0), "T1 speed 60, got %.2f",
              scan.targets[0].speed_kmh);
        CHECK(scan.targets[0].snr == 0x15, "T1 SNR 0x15, got %d", scan.targets[0].snr);
        CHECK(near(scan.targets[0].x_m, 40.0 * std::sin(10.0 * 3.141592653589793 / 180.0)),
              "T1 x mismatch");
        CHECK(near(scan.targets[0].y_m, 40.0 * std::cos(10.0 * 3.141592653589793 / 180.0)),
              "T1 y mismatch");

        CHECK(near(scan.targets[1].distance_m, 30.0), "T2 distance 30 m, got %.2f",
              scan.targets[1].distance_m);
        CHECK(near(scan.targets[2].angle_deg, -10.0), "T3 angle -10, got %.2f",
              scan.targets[2].angle_deg);
        CHECK(near(scan.targets[2].distance_m, 95.0), "T3 distance 95 m, got %.2f",
              scan.targets[2].distance_m);

        // 方向固定按 §6.2 字段定义解析：0x01 靠近、0x00 远离
        CHECK(scan.targets[0].direction_flag == 0x00 && !scan.targets[0].approaching,
              "flag 0x00 should be 远离");
        CHECK(scan.targets[1].direction_flag == 0x01 && scan.targets[1].approaching,
              "flag 0x01 should be 靠近");
        CHECK(near(scan.targets[0].signed_speed_kmh, -60.0), "远离速度应为 -60, got %.2f",
              scan.targets[0].signed_speed_kmh);
        CHECK(near(scan.targets[1].signed_speed_kmh, 60.0), "靠近速度应为 +60, got %.2f",
              scan.targets[1].signed_speed_kmh);
    }

    // 2. 空帧（无目标）
    {
        auto fr = hexBytes("F4 F3 F2 F1 00 00 F8 F7 F6 F5");
        Scan scan;
        CHECK(parseDataFrame(fr.data(), fr.size(), scan), "empty frame should parse");
        CHECK(scan.target_count == 0 && scan.targets.empty(), "empty frame = no targets");
        CHECK(!scan.alarm_approaching, "empty frame = no alarm");
    }

    // 3. 帧尾错误被拒绝；长度字段笔误可容错
    {
        auto fr = hexBytes(kExampleFrame);
        fr.back() ^= 0xFF;
        Scan scan;
        CHECK(!parseDataFrame(fr.data(), fr.size(), scan), "broken tail should be rejected");
        CHECK(!verifyTail(FrameKind::Data, fr.data(), fr.size()), "verifyTail should fail");
        fr = hexBytes(kExampleFrame);
        fr[4] = 0x10; // 长度字段笔误（应为 0x11）
        CHECK(parseDataFrame(fr.data(), fr.size(), scan) && scan.target_count == 3,
              "length-field typo should be tolerated");
    }

    // 4. 命令帧构造（协议 §5.1 使能配置 / §5.2 结束配置）
    {
        auto enable = buildCommand(static_cast<std::uint16_t>(Command::EnableConfig), {0x01, 0x00});
        auto expected = hexBytes("FD FC FB FA 04 00 FF 00 01 00 04 03 02 01");
        CHECK(enable == expected, "enable-config command frame mismatch");

        auto end = buildCommand(static_cast<std::uint16_t>(Command::EndConfig));
        auto expectedEnd = hexBytes("FD FC FB FA 02 00 FE 00 04 03 02 01");
        CHECK(end == expectedEnd, "end-config command frame mismatch");
        CHECK(frameKind(end.data(), end.size()) == FrameKind::Command, "command kind mismatch");
        CHECK(verifyTail(FrameKind::Command, end.data(), end.size()), "command tail should match");

        // 协议 §5.8：索引 0x0007 → 256000
        auto baud = buildCommand(static_cast<std::uint16_t>(Command::SetBaudRate), {0x07, 0x00});
        auto expectedBaud = hexBytes("FD FC FB FA 04 00 A1 00 07 00 04 03 02 01");
        CHECK(baud == expectedBaud, "set-baud command frame mismatch");

        // 协议 §5.3：最远 100 m / 只检测靠近 / 最小 5 km/h / 延迟 2 s
        auto tp = buildCommand(static_cast<std::uint16_t>(Command::SetTargetParams),
                               {0x64, 0x01, 0x05, 0x02});
        auto expectedTp = hexBytes("FD FC FB FA 06 00 02 00 64 01 05 02 04 03 02 01");
        CHECK(tp == expectedTp, "set-target-params command frame mismatch");
    }

    // 5. ACK 帧解析（协议 §5.7 固件版本；文档长度字段 0x0B 疑为笔误，此处按实际 12 字节构造）
    {
        auto fr = hexBytes("FD FC FB FA 0C 00 A0 01 00 00 51 24 01 01 10 15 05 24 04 03 02 01");
        Ack ack;
        CHECK(parseAckFrame(fr.data(), fr.size(), ack), "firmware ACK should parse");
        CHECK(ack.command == 0x01A0, "ACK command should be 0x01A0, got 0x%04X", ack.command);
        CHECK(ack.success, "ACK status should be success");
        CHECK(ack.payload.size() == 8, "ACK payload should be 8 bytes, got %zu", ack.payload.size());
        CHECK(ack.payload[0] == 0x51 && ack.payload[1] == 0x24, "firmware type should be 0x2451");
    }

    // 5b. 文档 §5.7 原始 ACK：长度字段 0x0B 但实际帧内数据 12 字节（疑为笔误）
    {
        auto fr = hexBytes("FD FC FB FA 0B 00 A0 01 00 00 51 24 01 01 10 15 05 24 04 03 02 01");
        Ack ack;
        CHECK(parseAckFrame(fr.data(), fr.size(), ack) && ack.command == 0x01A0 &&
                  ack.payload.size() == 8,
              "firmware ACK with typo length should still parse");

        int count = 0;
        FrameAssembler assembler([&](FrameKind kind, const std::uint8_t* d, std::size_t len) {
            ++count;
            CHECK(kind == FrameKind::Command, "should be Command frame");
            Ack a;
            CHECK(parseAckFrame(d, len, a) && a.success, "recovered ACK should parse");
        });
        assembler.push(fr.data(), fr.size());
        CHECK(count == 1, "assembler should recover exactly 1 ACK frame, got %d", count);
    }

    // 5c. 「读取参数」按钮依赖的两条查询 ACK（协议 §5.4 / §5.6）
    {
        auto fr = hexBytes("FD FC FB FA 08 00 12 01 00 00 64 01 05 02 04 03 02 01");
        Ack ack;
        CHECK(parseAckFrame(fr.data(), fr.size(), ack) && ack.command == 0x0112 &&
                  ack.success && ack.payload.size() == 4,
              "target-params ACK should parse with 4-byte payload");
        CHECK(ack.payload[0] == 0x64 && ack.payload[1] == 0x01 &&
                  ack.payload[2] == 0x05 && ack.payload[3] == 0x02,
              "target-params ACK payload mismatch");

        auto fr2 = hexBytes("FD FC FB FA 08 00 13 01 00 00 01 00 00 00 04 03 02 01");
        Ack ack2;
        CHECK(parseAckFrame(fr2.data(), fr2.size(), ack2) && ack2.command == 0x0113 &&
                  ack2.success && ack2.payload.size() == 4 && ack2.payload[0] == 0x01,
              "sensitivity ACK should parse");
    }

    // 6. 组帧器：垃圾字节 + 分片喂入 + 连续两帧
    {
        auto data = hexBytes(kExampleFrame);
        auto ack = hexBytes("FD FC FB FA 04 00 FE 01 00 00 04 03 02 01");
        std::vector<std::uint8_t> stream = {0xFF, 0xEE}; // 垃圾字节
        stream.insert(stream.end(), data.begin(), data.end());
        stream.insert(stream.end(), ack.begin(), ack.end());

        int dataCount = 0;
        int ackCount = 0;
        FrameAssembler assembler([&](FrameKind kind, const std::uint8_t* d, std::size_t len) {
            if (kind == FrameKind::Data) {
                ++dataCount;
                Scan scan;
                CHECK(parseDataFrame(d, len, scan) && scan.target_count == 3,
                      "assembled data frame should parse");
            } else if (kind == FrameKind::Command) {
                ++ackCount;
                Ack a;
                CHECK(parseAckFrame(d, len, a) && a.command == 0x01FE,
                      "assembled ACK frame should parse");
            }
        });
        assembler.push(stream.data(), 5);                        // 分片：垃圾 + 半个帧头
        assembler.push(stream.data() + 5, stream.size() - 5);   // 剩余全部
        CHECK(dataCount == 1, "should emit exactly 1 data frame, got %d", dataCount);
        CHECK(ackCount == 1, "should emit exactly 1 ACK frame, got %d", ackCount);
    }

    // 7. 组帧器：帧尾错误的伪帧头应被跳过，不误报
    {
        auto fake = hexBytes("F4 F3 F2 F1 00 00 00 00 00 00"); // 帧尾错误
        int count = 0;
        FrameAssembler assembler([&](FrameKind, const std::uint8_t*, std::size_t) { ++count; });
        assembler.push(fake.data(), fake.size());
        CHECK(count == 0, "broken frame should not be emitted, got %d", count);
    }

    if (g_failures == 0) {
        std::printf("All parser tests passed.\n");
        return 0;
    }
    std::printf("%d parser test(s) failed.\n", g_failures);
    return 1;
}
