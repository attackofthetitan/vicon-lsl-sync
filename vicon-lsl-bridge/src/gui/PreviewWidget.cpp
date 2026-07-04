#include "gui/PreviewWidget.h"

#include "preview/PreviewMath.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vicon_lsl {
namespace {

QColor markerColor() { return QColor("#00b8d9"); }
QColor trailColor() { return QColor(77, 163, 255, 90); }
QColor segmentXColor() { return QColor("#ff5a5f"); }
QColor segmentYColor() { return QColor("#2ecc71"); }
QColor segmentZColor() { return QColor("#4da3ff"); }
QColor stairColor() { return QColor(150, 158, 168, 82); }
QColor gridColor() { return QColor(70, 80, 88, 130); }

QColor gazeColor(const std::string& name) {
    if (name == "Combined") {
        return QColor("#ffd166");
    }
    if (name == "LeftEye") {
        return QColor("#06d6a0");
    }
    if (name == "RightEye") {
        return QColor("#ef476f");
    }
    return QColor("#f5f5f5");
}

double spanAxis(double lower, double upper) {
    return std::max(upper - lower, 0.05);
}

} // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(420, 320);
    setMouseTracking(true);
}

void PreviewWidget::setStairMesh(const PreviewMesh& mesh, const PreviewTransformProfile& transform) {
    stair_mesh_ = mesh;
    stair_transform_ = transform;
    stair_triangles_ = triangulateMesh(stair_mesh_, stair_transform_);
    update();
}

void PreviewWidget::setTrailPointLimit(int points) {
    trail_point_limit_ = std::max(2, points);
}

void PreviewWidget::setFrame(PreviewFrame frame) {
    frame_ = std::move(frame);
    for (const auto& marker : frame_.markers) {
        if (!marker.valid || !isFinite(marker.position)) {
            continue;
        }
        auto& trail = marker_trails_[marker.name];
        trail.push_back(marker.position);
        while (static_cast<int>(trail.size()) > trail_point_limit_) {
            trail.pop_front();
        }
    }
    update();
}

void PreviewWidget::initializeGL() {
}

void PreviewWidget::paintGL() {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#0e1419"));

    const Bounds bounds = sceneBounds();
    if (!bounds.valid) {
        painter.setPen(QColor("#b8c1cc"));
        painter.drawText(rect(), Qt::AlignCenter, "Preview waiting for LSL stream samples");
        return;
    }

    const double z = bounds.lower.z;
    painter.setPen(QPen(gridColor(), 1.0));
    const double x_span = spanAxis(bounds.lower.x, bounds.upper.x);
    const double y_span = spanAxis(bounds.lower.y, bounds.upper.y);
    const double grid_step = std::max(0.25, std::pow(10.0, std::floor(std::log10(std::max(x_span, y_span))) - 1.0));
    for (double x = std::floor(bounds.lower.x / grid_step) * grid_step; x <= bounds.upper.x; x += grid_step) {
        const auto a = project({x, bounds.lower.y, z}, bounds);
        const auto b = project({x, bounds.upper.y, z}, bounds);
        painter.drawLine(a.point, b.point);
    }
    for (double y = std::floor(bounds.lower.y / grid_step) * grid_step; y <= bounds.upper.y; y += grid_step) {
        const auto a = project({bounds.lower.x, y, z}, bounds);
        const auto b = project({bounds.upper.x, y, z}, bounds);
        painter.drawLine(a.point, b.point);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(stairColor());
    for (const auto& triangle : stair_triangles_) {
        QPolygonF polygon;
        polygon << project(triangle.a, bounds).point
                << project(triangle.b, bounds).point
                << project(triangle.c, bounds).point;
        painter.drawPolygon(polygon);
    }

    painter.setPen(QPen(trailColor(), 1.5));
    for (const auto& [_, trail] : marker_trails_) {
        if (trail.size() < 2) {
            continue;
        }
        QPainterPath path;
        path.moveTo(project(trail.front(), bounds).point);
        for (std::size_t index = 1; index < trail.size(); ++index) {
            path.lineTo(project(trail[index], bounds).point);
        }
        painter.drawPath(path);
    }

    constexpr double kAxisLength = 0.18;
    for (const auto& segment : frame_.segments) {
        if (!segment.valid || !isFinite(segment.position)) {
            continue;
        }
        const auto axes = segmentAxes(segment.rotation);
        const QColor colors[3] = {segmentXColor(), segmentYColor(), segmentZColor()};
        for (int axis = 0; axis < 3; ++axis) {
            painter.setPen(QPen(colors[axis], 2.0));
            painter.drawLine(project(segment.position, bounds).point,
                             project(segment.position + axes[axis] * kAxisLength, bounds).point);
        }
    }

    for (const auto& ray : frame_.gaze_rays) {
        if (!ray.valid) {
            continue;
        }
        const auto endpoint = gazeEndpoint(ray, bounds);
        if (!endpoint) {
            continue;
        }
        painter.setPen(QPen(gazeColor(ray.name), ray.name == "Combined" ? 3.0 : 2.0));
        painter.drawLine(project(ray.origin, bounds).point, project(*endpoint, bounds).point);
    }

    painter.setBrush(markerColor());
    painter.setPen(QPen(QColor("#031219"), 1.0));
    for (const auto& marker : frame_.markers) {
        if (!marker.valid || !isFinite(marker.position)) {
            continue;
        }
        const auto projected = project(marker.position, bounds);
        painter.drawEllipse(projected.point, 4.5, 4.5);
    }

    painter.setPen(QColor("#d7dde5"));
    const QString status = QString("markers %1 | segments %2 | gaze %3 | t %4")
        .arg(frame_.markers.size())
        .arg(frame_.segments.size())
        .arg(frame_.gaze_rays.size())
        .arg(frame_.timestamp, 0, 'f', 3);
    painter.drawText(QRectF(10, 8, width() - 20, 24), Qt::AlignLeft | Qt::AlignVCenter, status);
}

void PreviewWidget::mousePressEvent(QMouseEvent* event) {
    last_mouse_pos_ = event->pos();
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = event->pos() - last_mouse_pos_;
        azimuth_degrees_ += delta.x() * 0.4;
        elevation_degrees_ = std::clamp(elevation_degrees_ + delta.y() * 0.3, -85.0, 85.0);
        last_mouse_pos_ = event->pos();
        update();
    }
}

void PreviewWidget::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    zoom_ = std::clamp(zoom_ * std::pow(1.12, steps), 0.2, 8.0);
    update();
}

PreviewWidget::ProjectedPoint PreviewWidget::project(const PreviewVec3& point, const Bounds& bounds) const {
    const PreviewVec3 center = (bounds.lower + bounds.upper) * 0.5;
    PreviewVec3 view = point - center;
    view = rotateEulerDegrees(view, {elevation_degrees_, 0.0, azimuth_degrees_});
    const double span = std::max({spanAxis(bounds.lower.x, bounds.upper.x),
                                  spanAxis(bounds.lower.y, bounds.upper.y),
                                  spanAxis(bounds.lower.z, bounds.upper.z)});
    const double scale = std::min(width(), height()) * 0.74 * zoom_ / span;
    return {
        QPointF(width() * 0.5 + view.x * scale,
                height() * 0.56 - view.y * scale),
        view.z,
    };
}

PreviewWidget::Bounds PreviewWidget::sceneBounds() const {
    Bounds bounds;
    for (const auto& marker : frame_.markers) {
        if (marker.valid) {
            includePoint(bounds, marker.position);
        }
    }
    for (const auto& segment : frame_.segments) {
        if (segment.valid) {
            includePoint(bounds, segment.position);
        }
    }
    for (const auto& ray : frame_.gaze_rays) {
        if (ray.valid) {
            includePoint(bounds, ray.origin);
        }
    }
    for (const auto& triangle : stair_triangles_) {
        includePoint(bounds, triangle.a);
        includePoint(bounds, triangle.b);
        includePoint(bounds, triangle.c);
    }

    if (!bounds.valid) {
        return bounds;
    }
    const PreviewVec3 span = bounds.upper - bounds.lower;
    const PreviewVec3 pad{
        std::max(span.x * 0.08, 0.25),
        std::max(span.y * 0.08, 0.25),
        std::max(span.z * 0.08, 0.25),
    };
    bounds.lower = bounds.lower - pad;
    bounds.upper = bounds.upper + pad;
    return bounds;
}

void PreviewWidget::includePoint(Bounds& bounds, const PreviewVec3& point) const {
    if (!isFinite(point)) {
        return;
    }
    if (!bounds.valid) {
        bounds.lower = point;
        bounds.upper = point;
        bounds.valid = true;
        return;
    }
    bounds.lower.x = std::min(bounds.lower.x, point.x);
    bounds.lower.y = std::min(bounds.lower.y, point.y);
    bounds.lower.z = std::min(bounds.lower.z, point.z);
    bounds.upper.x = std::max(bounds.upper.x, point.x);
    bounds.upper.y = std::max(bounds.upper.y, point.y);
    bounds.upper.z = std::max(bounds.upper.z, point.z);
}

std::optional<PreviewVec3> PreviewWidget::gazeEndpoint(const PreviewGazeRay& ray,
                                                       const Bounds& bounds) const {
    return raySceneEndpoint(ray.origin, ray.direction, bounds.lower, bounds.upper, stair_triangles_);
}

} // namespace vicon_lsl
