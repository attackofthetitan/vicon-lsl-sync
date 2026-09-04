#pragma once

#include "preview/PreviewTypes.h"

#include <QWidget>
#include <QPoint>

#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace vicon_lsl {

class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setStairMesh(const PreviewMesh& mesh, const PreviewTransformProfile& transform);
    void setTrailPointLimit(int points);
    void resetForNewSource();
    void requestViewRefit();

public slots:
    void setFrame(vicon_lsl::PreviewFrame frame);
    void fitView();
    void resetCamera();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct ProjectedPoint {
        QPointF point;
    };

    struct Bounds {
        PreviewVec3 lower;
        PreviewVec3 upper;
        bool valid = false;
    };

    // The ground the walking runs happen on: a rectangle at one height rather
    // than a face of the view box, so it can meet the foot of the stairs and
    // reach past them along the walkway.
    struct FloorPlane {
        double lower_x = 0.0;
        double upper_x = 0.0;
        double lower_y = 0.0;
        double upper_y = 0.0;
        double z = 0.0;
        bool valid = false;
    };

    struct ViewBasis {
        PreviewVec3 right;
        PreviewVec3 up;
    };

    ViewBasis viewBasis() const;
    double viewScale(const Bounds& bounds, const ViewBasis& basis) const;
    double usableWidth() const;
    double usableHeight() const;
    ProjectedPoint project(const PreviewVec3& point, const Bounds& bounds) const;
    Bounds sceneContentBounds() const;
    FloorPlane floorPlane() const;
    Bounds currentSceneBounds() const;
    void resetViewFit();
    void lockViewToCurrentScene();
    void expandViewToInclude(const Bounds& bounds);
    void includePoint(Bounds& bounds, const PreviewVec3& point) const;
    std::optional<PreviewVec3> gazeEndpoint(const PreviewGazeRay& ray, const Bounds& bounds) const;

    PreviewFrame frame_;
    std::vector<PreviewTriangle> stair_triangles_;
    Bounds stair_bounds_;
    Bounds view_bounds_;
    std::map<std::string, std::deque<PreviewVec3>> marker_trails_;
    int trail_point_limit_ = 24;
    double azimuth_degrees_ = -64.0;
    double elevation_degrees_ = 24.0;
    double zoom_ = 1.0;
    bool have_previous_frame_timestamp_ = false;
    bool have_seen_valid_gaze_ = false;
    double previous_frame_timestamp_ = 0.0;
    bool refit_on_next_frame_ = true;
    QPoint last_mouse_pos_;
};

} // namespace vicon_lsl
