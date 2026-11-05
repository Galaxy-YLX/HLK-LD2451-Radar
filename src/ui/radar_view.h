#pragma once

#include <QTimer>
#include <QWidget>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/scan.h"
#include "protocol/parser.h"

namespace ld2451 {

// 2D 极坐标目标视图（PPI）：雷达位于中心，0° 为正前方（向上）。
// 与 EaiLidar Viewer 的 ScanView 同一套方案：绿黑主题、距离环/标尺、
// 镜像旋转适配安装方位、可调点大小、余辉控制。目标按运动方向着色，
// 并绘制速度矢量与数值标签。
class RadarView : public QWidget {
    Q_OBJECT
public:
    explicit RadarView(QWidget* parent = nullptr);

    void setScan(const std::shared_ptr<const Scan>& scan);

    // 当前显示的目标数（已计入信噪比过滤）
    int targetCount() const;

    // 点的绘制半径（px）
    void setPointRadius(double r);
    double pointRadius() const { return point_radius_; }

    // 视图半幅量程（m）：距离环、标尺随量程缩放
    void setRangeM(double range_m);
    double rangeM() const { return range_m_; }

    // 镜像模式：0=无 1=左右(X翻转) 2=上下(Y翻转) 3=双向
    void setMirror(int mode);
    int mirror() const { return mirror_; }

    // 目标绕中心旋转（°），用于适配雷达安装方位
    void setRotationDeg(double deg);
    double rotationDeg() const { return rotation_deg_; }

    // 信噪比下限：低于阈值的目标判为噪声（默认关）
    void setSnrFilter(bool enable, int min_snr);
    // 余辉控制：目标轨迹保留指定秒数后清除（默认关，保留 2 s）
    void setPersistence(bool enable, int seconds);
    // 速度矢量 / 数值标签显示开关
    void setShowVectors(bool show);
    void setShowLabels(bool show);
    // 方向定义：协议 §6.2 字段定义 / §7 示例解析（实测后可切换）
    void setDirectionMapping(DirectionMapping mapping);
    DirectionMapping directionMapping() const { return mapping_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct TrailPoint {
        double x_m = 0.0;
        double y_m = 0.0;
        std::uint8_t direction_flag = 0;
        std::chrono::steady_clock::time_point stamp;
    };

    void decayExpired();
    bool visible(const Target& t) const;

    static constexpr std::size_t kMaxTrailPoints = 600;

    std::vector<Target> targets_; // 最近一帧的目标
    std::vector<TrailPoint> trail_; // 余辉轨迹
    bool alarm_ = false;
    int point_count_ = 0;
    double range_m_ = 30.0;
    double point_radius_ = 4.0;
    int mirror_ = 0;
    double rotation_deg_ = 0.0;
    DirectionMapping mapping_ = DirectionMapping::PerSpec;

    bool snr_enable_ = false;
    int snr_min_ = 20;
    bool persistence_enable_ = false;
    int persistence_seconds_ = 2;
    bool show_vectors_ = true;
    bool show_labels_ = true;
    QTimer decayTimer_; // 周期性清除过期轨迹并重绘
};

} // namespace ld2451
