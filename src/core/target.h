#pragma once

#include <cstdint>

namespace ld2451 {

// 单个探测目标（雷达上报帧中的 5 字节目标信息，已换算到直角坐标）
struct Target {
    double angle_deg = 0.0;        // 上报值 - 0x80（°），0° 为正前方
    double distance_m = 0.0;       // 距离（m）
    std::uint8_t direction_flag = 0; // 原始速度方向字节
    double speed_kmh = 0.0;        // 速度幅值（km/h）
    double signed_speed_kmh = 0.0; // 按协议 §6.2：靠近为正，远离为负
    bool approaching = false;      // 按协议 §6.2 解释
    int snr = 0;                   // 信噪比 0~255
    double x_m = 0.0;             // 直角坐标 X = d·sin(θ)
    double y_m = 0.0;             // 直角坐标 Y = d·cos(θ)（正前方）
};

} // namespace ld2451
