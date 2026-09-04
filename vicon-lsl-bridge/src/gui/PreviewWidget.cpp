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

constexpr double kPi = 3.14159265358979323846;

// How far the drawn ground reaches past the stair model. Taken from the
// recorded walking runs in this study (sub-04 and sub-05, 33 runs): valid
// markers reach 4.10 m past the far end of the stairs along the walking axis,
// stop just short of the near end, and stay inside the stair width. The
// margins leave a little ground past the widest run rather than ending the
// floor exactly where a heel last landed.
constexpr double kFloorBeyondStairM = 4.6;
constexpr double kFloorBehindStairM = 0.6;
constexpr double kFloorBesideStairM = 0.6;

// Rows the fit keeps clear so the scene never grows under the status line or
// the legend, which are drawn over the same surface.
constexpr double kStatusBandPx = 30.0;
constexpr double kLegendBandPx = 34.0;
constexpr double kSideMarginPx = 12.0;

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

double radians(double degrees) {
    return degrees * kPi / 180.0;
}

} // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    // A floor this widget cannot go below is a floor the panel's controls get
    // laid over when the window is short, so keep it to a usable scrap of
    // drawing area and let the splitter hand out the real size.
    setMinimumSize(240, 160);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Motion and gaze preview");
}

void PreviewWidget::setStairMesh(const PreviewMesh& mesh, const PreviewTransformProfile& transform) {
    stair_triangles_ = triangulateMesh(mesh, transform);
    stair_bounds_ = {};
    for (const auto& triangle : stair_triangles_) {
        includePoint(stair_bounds_, triangle.a);
        includePoint(stair_bounds_, triangle.b);
        includePoint(stair_bounds_, triangle.c);
    }
    resetViewFit();
    refit_on_next_frame_ = true;
    update();
}

void PreviewWidget::setTrailPointLimit(int points) {
    trail_point_limit_ = std::max(2, points);
}

void PreviewWidget::resetForNewSource() {
    frame_ = {};
    marker_trails_.clear();
    have_previous_frame_timestamp_ = false;
    have_seen_valid_gaze_ = false;
    previous_frame_timestamp_ = 0.0;
    resetViewFit();
    refit_on_next_frame_ = true;
    update();
}

void PreviewWidget::requestViewRefit() {
    refit_on_next_frame_ = true;
}

void PreviewWidget::fitView() {
    resetViewFit();
    lockViewToCurrentScene();
    update();
}

void PreviewWidget::resetCamera() {
    azimuth_degrees_ = -64.0;
    elevation_degrees_ = 24.0;
    zoom_ = 1.0;
    fitView();
}

void PreviewWidget::setFrame(PreviewFrame frame) {
    const bool rewound = have_previous_frame_timestamp_
        && std::isfinite(frame.timestamp)
        && frame.timestamp + 1e-6 < previous_frame_timestamp_;
    const bool first_valid_gaze = !have_seen_valid_gaze_ && std::any_of(
        frame.gaze_rays.begin(), frame.gaze_rays.end(), [](const PreviewGazeRay& ray) {
            return ray.valid && isFinite(ray.origin) && isFinite(ray.direction);
        });

    frame_ = std::move(frame);
    if (frame_.marker_stream_present) {
        std::map<std::string, bool> current_names;
        for (const auto& marker : frame_.markers) current_names[marker.name] = true;
        for (auto it = marker_trails_.begin(); it != marker_trails_.end();) {
            if (current_names.find(it->first) == current_names.end()) {
                it = marker_trails_.erase(it);
            } else {
                ++it;
            }
        }
    }
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
    if (rewound || refit_on_next_frame_ || first_valid_gaze) {
        resetViewFit();
        refit_on_next_frame_ = false;
    }
    have_seen_valid_gaze_ = have_seen_valid_gaze_ || first_valid_gaze;
    lockViewToCurrentScene();
    if (std::isfinite(frame_.timestamp)) {
        previous_frame_timestamp_ = frame_.timestamp;
        have_previous_frame_timestamp_ = true;
    }
    update();
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor background = palette().color(QPalette::Window);
    const QColor foreground = palette().color(QPalette::WindowText);
    painter.fillRect(rect(), background);

    const Bounds bounds = view_bounds_.valid ? view_bounds_ : currentSceneBounds();
    if (!bounds.valid) {
        painter.setPen(foreground);
        painter.drawText(rect(), Qt::AlignCenter, "Preview waiting for LSL stream samples");
        return;
    }

    const FloorPlane floor = floorPlane();
    if (floor.valid) {
        painter.setPen(QPen(gridColor(), 1.0));
        painter.setBrush(Qt::NoBrush);
        const double x_span = spanAxis(floor.lower_x, floor.upper_x);
        const double y_span = spanAxis(floor.lower_y, floor.upper_y);
        const double grid_step = std::max(0.25, std::pow(10.0, std::floor(std::log10(std::max(x_span, y_span))) - 1.0));
        for (double x = std::ceil(floor.lower_x / grid_step) * grid_step; x <= floor.upper_x; x += grid_step) {
            const auto a = project({x, floor.lower_y, floor.z}, bounds);
            const auto b = project({x, floor.upper_y, floor.z}, bounds);
            painter.drawLine(a.point, b.point);
        }
        for (double y = std::ceil(floor.lower_y / grid_step) * grid_step; y <= floor.upper_y; y += grid_step) {
            const auto a = project({floor.lower_x, y, floor.z}, bounds);
            const auto b = project({floor.upper_x, y, floor.z}, bounds);
            painter.drawLine(a.point, b.point);
        }
        // The grid lines land on whole steps, so the edge of the walkway is
        // only where the floor really ends once it is drawn outright.
        QPolygonF edge;
        edge << project({floor.lower_x, floor.lower_y, floor.z}, bounds).point
             << project({floor.upper_x, floor.lower_y, floor.z}, bounds).point
             << project({floor.upper_x, floor.upper_y, floor.z}, bounds).point
             << project({floor.lower_x, floor.upper_y, floor.z}, bounds).point;
        painter.drawPolygon(edge);
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

    const auto valid_markers = std::count_if(frame_.markers.begin(), frame_.markers.end(),
        [](const PreviewMarker& marker) { return marker.valid && isFinite(marker.position); });
    const auto valid_segments = std::count_if(frame_.segments.begin(), frame_.segments.end(),
        [](const PreviewSegment& segment) { return segment.valid && isFinite(segment.position); });
    const auto valid_gaze = std::count_if(frame_.gaze_rays.begin(), frame_.gaze_rays.end(),
        [](const PreviewGazeRay& ray) { return ray.valid && isFinite(ray.origin) && isFinite(ray.direction); });
    painter.setPen(foreground);
    const QString status = QString("markers %1/%2 | segments %3/%4 | gaze %5/%6 | t %7 s")
        .arg(valid_markers).arg(frame_.markers.size())
        .arg(valid_segments).arg(frame_.segments.size())
        .arg(valid_gaze).arg(frame_.gaze_rays.size())
        .arg(frame_.timestamp, 0, 'f', 3);
    painter.drawText(QRectF(10, 8, width() - 20, 24), Qt::AlignLeft | Qt::AlignVCenter, status);

    const int legend_y = height() - 16;
    painter.setPen(QPen(segmentXColor(), 2.0));
    painter.drawText(10, legend_y, "X red");
    painter.setPen(QPen(segmentYColor(), 2.0));
    painter.drawText(62, legend_y, "Y green");
    painter.setPen(QPen(segmentZColor(), 2.0));
    painter.drawText(126, legend_y, "Z blue");
    painter.setPen(foreground);
    painter.drawText(190, legend_y, "positions: metres | dim/absent = invalid or occluded");
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

PreviewWidget::ViewBasis PreviewWidget::viewBasis() const {
    const double azimuth = radians(azimuth_degrees_);
    const double elevation = radians(elevation_degrees_);
    const PreviewVec3 camera_direction = normalize({
        std::cos(elevation) * std::cos(azimuth),
        std::cos(elevation) * std::sin(azimuth),
        std::sin(elevation),
    });
    const PreviewVec3 forward = camera_direction * -1.0;
    PreviewVec3 right = cross(forward, {0.0, 0.0, 1.0});
    if (length(right) <= 1e-9) {
        right = {1.0, 0.0, 0.0};
    } else {
        right = normalize(right);
    }
    return {right, normalize(cross(right, forward))};
}

double PreviewWidget::viewScale(const Bounds& bounds, const ViewBasis& basis) const {
    // Fitting the scene as it is actually projected, rather than its largest
    // world span against the shorter side of the widget: a walkway several
    // times longer than it is tall otherwise leaves most of a wide panel empty.
    const PreviewVec3 center = (bounds.lower + bounds.upper) * 0.5;
    double half_across = 0.0;
    double half_down = 0.0;
    for (int corner = 0; corner < 8; ++corner) {
        const PreviewVec3 offset = PreviewVec3{
            (corner & 1) ? bounds.upper.x : bounds.lower.x,
            (corner & 2) ? bounds.upper.y : bounds.lower.y,
            (corner & 4) ? bounds.upper.z : bounds.lower.z,
        } - center;
        half_across = std::max(half_across, std::abs(dot(offset, basis.right)));
        half_down = std::max(half_down, std::abs(dot(offset, basis.up)));
    }
    const double across = std::max(2.0 * half_across, 0.05);
    const double down = std::max(2.0 * half_down, 0.05);
    return std::min(usableWidth() / across, usableHeight() / down) * zoom_;
}

double PreviewWidget::usableWidth() const {
    return std::max(40.0, width() - 2.0 * kSideMarginPx);
}

double PreviewWidget::usableHeight() const {
    return std::max(40.0, height() - kStatusBandPx - kLegendBandPx);
}

PreviewWidget::ProjectedPoint PreviewWidget::project(const PreviewVec3& point, const Bounds& bounds) const {
    const ViewBasis basis = viewBasis();
    const PreviewVec3 center = (bounds.lower + bounds.upper) * 0.5;
    const PreviewVec3 view = point - center;
    const double scale = viewScale(bounds, basis);
    return {
        QPointF(width() * 0.5 + dot(view, basis.right) * scale,
                kStatusBandPx + usableHeight() * 0.5 - dot(view, basis.up) * scale),
    };
}

PreviewWidget::Bounds PreviewWidget::sceneContentBounds() const {
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
    if (stair_bounds_.valid) {
        includePoint(bounds, stair_bounds_.lower);
        includePoint(bounds, stair_bounds_.upper);
    }
    return bounds;
}

PreviewWidget::FloorPlane PreviewWidget::floorPlane() const {
    const Bounds content = sceneContentBounds();
    const Bounds& base = stair_bounds_.valid ? stair_bounds_ : content;
    if (!base.valid) {
        return {};
    }

    FloorPlane floor;
    floor.valid = true;
    // The height the stairs stand on, not the padded bottom of the view box:
    // padding there is what left the ground floating below the model.
    floor.z = base.lower.z;
    // Without a model there is no walkway to reach along, so the ground covers
    // what the samples cover and no more.
    const double beyond = stair_bounds_.valid ? kFloorBeyondStairM : 0.0;
    const double behind = stair_bounds_.valid ? kFloorBehindStairM : 0.0;
    const double beside = stair_bounds_.valid ? kFloorBesideStairM : 0.0;
    floor.lower_x = base.lower.x - behind;
    floor.upper_x = base.upper.x + beyond;
    floor.lower_y = base.lower.y - beside;
    floor.upper_y = base.upper.y + beside;
    if (content.valid) {
        floor.lower_x = std::min(floor.lower_x, content.lower.x);
        floor.upper_x = std::max(floor.upper_x, content.upper.x);
        floor.lower_y = std::min(floor.lower_y, content.lower.y);
        floor.upper_y = std::max(floor.upper_y, content.upper.y);
    }
    return floor;
}

PreviewWidget::Bounds PreviewWidget::currentSceneBounds() const {
    Bounds bounds = sceneContentBounds();
    // Fitting to the floor as well is what makes the walkway visible at rest:
    // otherwise the view frames the stairs and the ground runs off the edge.
    const FloorPlane floor = floorPlane();
    if (floor.valid) {
        includePoint(bounds, {floor.lower_x, floor.lower_y, floor.z});
        includePoint(bounds, {floor.upper_x, floor.upper_y, floor.z});
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

void PreviewWidget::resetViewFit() {
    view_bounds_ = {};
}

void PreviewWidget::lockViewToCurrentScene() {
    const Bounds bounds = currentSceneBounds();
    if (!view_bounds_.valid && bounds.valid) {
        view_bounds_ = bounds;
    } else if (bounds.valid) {
        expandViewToInclude(bounds);
    }
}

void PreviewWidget::expandViewToInclude(const Bounds& bounds) {
    if (!bounds.valid) {
        return;
    }
    if (!view_bounds_.valid) {
        view_bounds_ = bounds;
        return;
    }
    includePoint(view_bounds_, bounds.lower);
    includePoint(view_bounds_, bounds.upper);
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
