#pragma once

#include "core/target.h"

#include <vector>

namespace ld2451 {

// 一帧上报数据
struct Scan {
    int target_count = 0;               // 目标数量
    bool alarm_approaching = false;     // 报警信息：存在靠近目标
    std::vector<Target> targets;
};

} // namespace ld2451
