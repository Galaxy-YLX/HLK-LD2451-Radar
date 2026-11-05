#include "ui/radar_view.h"

#include <QPaintEvent>
#include <QPainter>

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
    decayTimer_.setInterval(50);
    connect(&decayTimer_, &QTimer::timeout, this, [this] {
        decayExpired();
        update();
    });
}

bool RadarView::visible(const Target& t) const {
    return !snr_enable_ || t.snr >= snr_min_;
}

int RadarView::targetCount() const {
    if (!snr_enable_) return static_cast<int>(targets_.size());
    return static_cast<int>(std::count_if(targets_.begin(), targets_.end(),
                                          [this](const Target& t) { return visible(t); }));
}

void RadarView::setScan(const std::shared_ptr<const Scan>& scan) {
    decayExpired();

    if (scan == nullptr) {
        targets_.clear();
        trail_.clear();
        alarm_ = false;
        point_count_ = 0;
        update();
        return;
    }

    alarm_ = scan->alarm_approaching;
    targets_ = scan->targets;
    point_count_ = targetCount();

    if (persistence_enable_) {
        const auto now = std::chrono::steady_clock::now();
        for (const auto& t : targets_) {
            if (!visible(t)) continue;
            trail_.push_back({t.x_m, t.y_m, t.direction_flag, now});
        }
        if (trail_.size() > kMaxTrailPoints) {
            trail_.erase(trail_.begin(),
                         trail_.begin() + static_cast<std::ptrdiff_t>(trail_.size() - kMaxTrailPoints));
        }
    }
    update();
}

void RadarView::decayExpired() {
    if (trail_.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    const double max_age_ms = persistence_seconds_ * 1000.0;
    trail_.erase(std::remove_if(trail_.begin(), trail_.end(),
                                [&](const TrailPoint& pt) {
                                    return std::chrono::duration<double, std::milli>(now - pt.stamp)
                                               .count() > max_age_ms;
                                }),
                 trail_.end());
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

void RadarView::setRotationDeg(double deg) {
    rotation_deg_ = deg;
    update();
}

void RadarView::setSnrFilter(bool enable, int min_snr) {
    snr_enable_ = enable;
    snr_min_ = std::clamp(min_snr, 0, 255);
    point_count_ = targetCount();
    update();
}

void RadarView::setPersistence(bool enable, int seconds) {
    persistence_enable_ = enable;
    persistence_seconds_ = std::max(1, seconds);
    if (enable) {
        decayTimer_.start();
    } else {
        decayTimer_.stop();
        trail_.clear();
    }
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

void RadarView::setDirectionMapping(DirectionMapping mapping) {
    mapping_ = mapping;
    update();
}

void RadarView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0, 0, 0));

    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    const double scale = std::min(width(), height()) / (2.0 * range_m_);

    // 世界坐标 (m) → 屏幕坐标（雷达居中，y 轴向上为正，屏幕 y 向下）。
    // 先镜像、再旋转，用于适配雷达安装方位。
    const double rot = rotation_deg_ * kPi / 180.0;
    const double cosR = std::cos(rot);
    const double sinR = std::sin(rot);
    auto toScreen = [&](double x_m, double y_m) {
        double wx = x_m;
        double wy = y_m;
        if (mirror_ & 1) wx = -wx; // 左右镜像
        if (mirror_ & 2) wy = -wy; // 上下镜像
        const double rx = wx * cosR - wy * sinR;
        const double ry = wx * sinR + wy * cosR;
        return QPointF(cx + rx * scale, cy - ry * scale);
    };

    // 距离环 + 距离标尺（步进随量程自适应）
    const double step = niceStep(range_m_);
    p.setPen(QPen(QColor(34, 42, 36), 1));
    for (double d = step; d < range_m_; d += step) {
        const double r = d * scale;
        p.drawEllipse(QPointF(cx, cy), r, r);
    }
    p.drawLine(QPointF(cx - range_m_ * scale, cy), QPointF(cx + range_m_ * scale, cy));
    p.drawLine(QPointF(cx, cy - range_m_ * scale), QPointF(cx, cy + range_m_ * scale));

    QFont smallFont = p.font();
    smallFont.setPointSizeF(8.0);
    p.setFont(smallFont);
    p.setPen(QPen(QColor(96, 110, 100), 1));
    for (double d = step; d < range_m_; d += step) {
        const double x = cx + d * scale;
        p.drawLine(QPointF(x, cy - 4), QPointF(x, cy + 4));
        const QString label = std::abs(d - std::round(d)) < 1e-9
                                  ? QString::number(static_cast<int>(std::lround(d)))
                                  : QString::number(d, 'f', 1);
        p.drawText(QRectF(x - 24, cy + 6, 48, 12), Qt::AlignHCenter | Qt::AlignTop,
                   label + tr("m"));
    }

    // 角度射线与刻度：0° 正前方（向上），每 30° 一条
    p.setPen(QPen(QColor(34, 42, 36), 1));
    for (int a = -120; a <= 120; a += 30) {
        const double rad = a * kPi / 180.0;
        p.drawLine(QPointF(cx, cy),
                   toScreen(range_m_ * std::sin(rad), range_m_ * std::cos(rad)));
    }
    p.setPen(QPen(QColor(96, 110, 100), 1));
    for (int a = -120; a <= 120; a += 30) {
        const double rad = a * kPi / 180.0;
        const QPointF labelPos =
            toScreen(range_m_ * 0.93 * std::sin(rad), range_m_ * 0.93 * std::cos(rad));
        p.drawText(QRectF(labelPos.x() - 20, labelPos.y() - 8, 40, 16),
                   Qt::AlignCenter, tr("%1°").arg(a));
    }

    // 余辉轨迹：按年龄淡出
    if (!trail_.empty()) {
        const auto now = std::chrono::steady_clock::now();
        const double max_age_ms = persistence_seconds_ * 1000.0;
        p.setPen(Qt::NoPen);
        for (const auto& pt : trail_) {
            const double age =
                std::chrono::duration<double, std::milli>(now - pt.stamp).count();
            const double alpha = std::clamp(1.0 - age / max_age_ms, 0.0, 1.0);
            QColor c = isApproaching(pt.direction_flag, mapping_) ? kApproachColor : kLeaveColor;
            c.setAlphaF(static_cast<float>(alpha * 0.55));
            p.setBrush(c);
            p.drawEllipse(toScreen(pt.x_m, pt.y_m), point_radius_ * 0.6, point_radius_ * 0.6);
        }
    }

    // 雷达本体标记
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 220, 120));
    p.drawEllipse(QPointF(cx, cy), 4.0, 4.0);
    p.setPen(QPen(QColor(0, 220, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(cx, cy), 8.0, 8.0);

    // 当前帧目标
    if (point_count_ == 0) {
        p.setPen(QColor(60, 92, 68));
        p.drawText(rect(), Qt::AlignCenter, tr("No data"));
        return;
    }

    for (const auto& t : targets_) {
        if (!visible(t)) continue;
        const bool approaching = isApproaching(t.direction_flag, mapping_);
        const QColor color = approaching ? kApproachColor : kLeaveColor;
        const QPointF pos = toScreen(t.x_m, t.y_m);

        // 速度矢量：靠近指向中心，远离指向外；长度随速度幅值
        if (show_vectors_ && t.speed_kmh > 0.0) {
            const double rad = t.angle_deg * kPi / 180.0;
            const QPointF unit(std::sin(rad), -std::cos(rad)); // 屏幕方向（0° 向上）
            const double len = 8.0 + 40.0 * std::min(t.speed_kmh, 120.0) / 120.0;
            const QPointF tip = pos + (approaching ? -unit : unit) * len;
            const QPointF back = approaching ? unit : -unit;
            const QPointF normal(-back.y(), back.x());
            p.setPen(QPen(color, 1.5));
            p.drawLine(pos, tip);
            p.drawLine(tip, tip + back * 7.0 + normal * 3.5);
            p.drawLine(tip, tip + back * 7.0 - normal * 3.5);
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

    // 报警边框
    if (alarm_) {
        p.setPen(QPen(kAlarmColor, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

} // namespace ld2451
