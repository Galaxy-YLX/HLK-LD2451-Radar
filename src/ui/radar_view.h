#pragma once

#include <QWidget>

#include <memory>
#include <vector>

#include "core/scan.h"
#include "protocol/parser.h"

namespace ld2451 {

// 2D 极坐标目标视图：按 LD2451 真实波束画 ±15° 扇形，雷达位于底部中心，
// 0° 为正前方（向上）。与 EaiLidar Viewer 的 ScanView 同一套方案：绿黑主题、
// 距离弧/标尺、镜像适配安装方位、可调点大小。目标按运动方向着色，
// 并绘制速度线段与数值标签；超出波束的目标不在视图中绘制。
class RadarView : public QWidget {
    Q_OBJECT
public:
    // 手册 V1.03 性能参数：水平探测角度 ±15°
    static constexpr double kFovHalfDeg = 15.0;

    explicit RadarView(QWidget* parent = nullptr);

    void setScan(const std::shared_ptr<const Scan>& scan);

    // 当前显示的目标数（已排除 ±15° 波束外的目标）
    int targetCount() const;

    // 点的绘制半径（px）
    void setPointRadius(double r);
    double pointRadius() const { return point_radius_; }

    // 视图半幅量程（m）：距离弧、标尺随量程缩放
    void setRangeM(double range_m);
    double rangeM() const { return range_m_; }

    // 镜像模式：0=无 1=左右(X翻转) 2=上下(Y翻转) 3=双向
    void setMirror(int mode);
    int mirror() const { return mirror_; }

    // 速度线段 / 数值标签显示开关
    void setShowVectors(bool show);
    void setShowLabels(bool show);
    // 方向定义：协议 §6.2 字段定义 / §7 示例解析（实测后可切换）
    void setDirectionMapping(DirectionMapping mapping);
    DirectionMapping directionMapping() const { return mapping_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool visible(const Target& t) const;

    std::vector<Target> targets_; // 最近一帧的目标
    bool alarm_ = false;
    int point_count_ = 0;
    double range_m_ = 30.0;
    double point_radius_ = 4.0;
    int mirror_ = 0;
    DirectionMapping mapping_ = DirectionMapping::PerSpec;
    bool show_vectors_ = true;
    bool show_labels_ = true;
};

} // namespace ld2451
