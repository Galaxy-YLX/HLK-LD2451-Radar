#include "ui/radar_view.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace ld2451 {
namespace {

constexpr double kPi = 3.14159265358979323846;

const QColor kApproachColor(0, 220, 120); // 靠近：雷达绿
const QColor kLeaveColor(255, 176, 0);    // 远离：橙黄
const QColor kAlarmColor(255, 64, 64);    // 报警：红

// 按量程选择距离环步进（m），使屏幕上始终有 5~10 圈刻度
double niceStep(double range_m) {
    const double target = range_m / 8.0;
    const double candidates[] = {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0};
    double best = candidates[0];
    for (double s : candidates) {
        if (std::abs(s - target) < std::abs(best - target)) best = s;
    }
    return best;
}

QString signedSpeed(double kmh) {
    return QString::number(kmh, 'f', 0) + QStringLiteral(" km/h");
}

} // namespace

RadarView::RadarView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 400);
}

bool RadarView::visible(const Target& t) const {
    // 超出真实水平波束 ±15° 的上报角度不在扇形视图中绘制
    return std::abs(t.angle_deg) <= kFovHalfDeg;
}

int RadarView::targetCount() const {
    return static_cast<int>(std::count_if(targets_.begin(), targets_.end(),
                                          [this](const Target& t) { return visible(t); }));
}

void RadarView::setScan(const std::shared_ptr<const Scan>& scan) {
    if (scan == nullptr) {
        targets_.clear();
        alarm_ = false;
        point_count_ = 0;
        update();
        return;
    }

    alarm_ = scan->alarm_approaching;
    targets_ = scan->targets;
    point_count_ = targetCount();
    update();
}

void RadarView::setPointRadius(double r) {
    point_radius_ = std::max(1.0, r);
    update();
}

void RadarView::setRangeM(double range_m) {
    range_m_ = std::clamp(range_m, 5.0, 100.0); // 协议距离上限 100 m
    update();
}

void RadarView::setMirror(int mode) {
    mirror_ = mode & 3;
    update();
}

void RadarView::setShowVectors(bool show) {
    show_vectors_ = show;
    update();
}

void RadarView::setShowLabels(bool show) {
    show_labels_ = show;
    update();
}

void RadarView::setVectorTime(double seconds) {
    vector_time_s_ = std::clamp(seconds, 0.1, 5.0);
    update();
}

void RadarView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0, 0, 0));

    // 雷达位于视图底部（上下镜像时位于顶部），真实波束 ±15° 的扇形朝前展开。
    const double margin = 28.0;
    const double ox = width() / 2.0;
    const double usable_h = std::max(1.0, height() - 2.0 * margin);
    const double usable_w = std::max(1.0, width() / 2.0 - margin);
    const double half_width_m = range_m_ * std::sin(kFovHalfDeg * kPi / 180.0);
    const double scale = std::min(usable_h / range_m_, usable_w / half_width_m);

    // 上下镜像把扇形翻到下方，原点随之移到顶部
    const bool downward = (mirror_ & 2) != 0;
    const double oy = downward ? margin : height() - margin;

    // 世界坐标 (m) → 屏幕坐标（0° 朝上，屏幕 y 向下），左右镜像翻转 X。
    auto toScreen = [&](double x_m, double y_m) {
        if (mirror_ & 1) x_m = -x_m;
        if (downward) y_m = -y_m;
        return QPointF(ox + x_m * scale, oy - y_m * scale);
    };
    auto onArc = [&](double d, double angle_deg) {
        const double rad = angle_deg * kPi / 180.0;
        return toScreen(d * std::sin(rad), d * std::cos(rad));
    };
    const QPointF origin = toScreen(0.0, 0.0);

    // 扇形背景
    QPainterPath sector;
    sector.moveTo(origin);
    for (int k = 0; k <= 60; ++k) {
        const double a = -kFovHalfDeg + 2.0 * kFovHalfDeg * k / 60.0;
        sector.lineTo(onArc(range_m_, a));
    }
    sector.closeSubpath();
    p.fillPath(sector, QColor(8, 26, 16));

    // 距离弧：只画 ±15° 扇形内的部分（步进随量程自适应）
    const double step = niceStep(range_m_);
    p.setPen(QPen(QColor(34, 42, 36), 1));
    for (double d = step; d <= range_m_ + 1e-9; d += step) {
        QPainterPath arc;
        for (int k = 0; k <= 60; ++k) {
            const double a = -kFovHalfDeg + 2.0 * kFovHalfDeg * k / 60.0;
            const QPointF pt = onArc(d, a);
            if (k == 0) arc.moveTo(pt);
            else arc.lineTo(pt);
        }
        p.drawPath(arc);
    }

    // 角度射线：每 5° 一条，±15° 边界线稍亮
    for (int a = -15; a <= 15; a += 5) {
        const bool edge = (a == -15 || a == 15);
        p.setPen(QPen(edge ? QColor(0, 110, 60) : QColor(34, 42, 36), edge ? 1.5 : 1.0));
        p.drawLine(origin, onArc(range_m_, a));
    }

    QFont smallFont = p.font();
    smallFont.setPointSizeF(8.0);
    p.setFont(smallFont);

    // 距离标尺：沿 0° 轴标注
    p.setPen(QPen(QColor(96, 110, 100), 1));
    for (double d = step; d <= range_m_ + 1e-9; d += step) {
        const QPointF pt = onArc(d, 0.0);
        p.drawLine(QPointF(pt.x() - 4, pt.y()), QPointF(pt.x() + 4, pt.y()));
        const QString label = std::abs(d - std::round(d)) < 1e-9
                                  ? QString::number(static_cast<int>(std::lround(d)))
                                  : QString::number(d, 'f', 1);
        p.drawText(QRectF(pt.x() + 8, pt.y() - 8, 48, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, label + tr("m"));
    }

    // 角度刻度标签：置于扇形外缘
    const int angleTicks[] = {-15, -10, -5, 0, 5, 10, 15};
    p.setPen(QPen(QColor(96, 110, 100), 1));
    for (int a : angleTicks) {
        const QPointF pt = onArc(range_m_, a);
        p.drawText(QRectF(pt.x() - 20, pt.y() - 14, 40, 16),
                   Qt::AlignCenter, tr("%1°").arg(a));
    }

    // 雷达本体标记（扇形顶点）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 220, 120));
    p.drawEllipse(origin, 4.0, 4.0);
    p.setPen(QPen(QColor(0, 220, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(origin, 8.0, 8.0);

    // 当前帧目标
    if (point_count_ == 0) {
        p.setPen(QColor(60, 92, 68));
        p.drawText(rect(), Qt::AlignCenter, tr("No data"));
        return;
    }

    for (const auto& t : targets_) {
        if (!visible(t)) continue;
        const bool approaching = t.approaching; // 解析层按 §6.2 固定判定
        const QColor color = approaching ? kApproachColor : kLeaveColor;
        const QPointF pos = toScreen(t.x_m, t.y_m);

        // 时间矢量：线段长度 = 速度(m/s) × 矢量时长(s)，即该时段内的径向位移。
        // 靠近指向雷达、远离指向外；终点在世界坐标中计算，保证镜像时方向一致。
        if (show_vectors_ && t.speed_kmh > 0.0) {
            const double rad = t.angle_deg * kPi / 180.0;
            const double distance_m = t.speed_kmh / 3.6 * vector_time_s_;
            const double sign = approaching ? -1.0 : 1.0;
            const QPointF end = toScreen(t.x_m + sign * distance_m * std::sin(rad),
                                         t.y_m + sign * distance_m * std::cos(rad));
            p.setPen(QPen(color, 1.5));
            p.drawLine(pos, end);
        }

        // 目标点；报警时靠近目标加红圈
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(pos, point_radius_, point_radius_);
        if (alarm_ && approaching) {
            p.setPen(QPen(kAlarmColor, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pos, point_radius_ + 4.0, point_radius_ + 4.0);
        }

        if (show_labels_) {
            const QString text = tr("%1°  %2 m  %3  SNR %4")
                                     .arg(t.angle_deg, 0, 'f', 0)
                                     .arg(t.distance_m, 0, 'f', 0)
                                     .arg(signedSpeed(approaching ? t.speed_kmh : -t.speed_kmh))
                                     .arg(t.snr);
            p.setPen(color);
            p.drawText(QRectF(pos.x() + point_radius_ + 6, pos.y() - 10, 200, 18),
                       Qt::AlignLeft | Qt::AlignVCenter, text);
        }
    }
}

} // namespace ld2451
