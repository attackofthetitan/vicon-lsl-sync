#pragma once

#include "preview/PreviewTypes.h"

#include <QOpenGLWidget>
#include <QPoint>

#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace vicon_lsl {

class PreviewWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setStairMesh(const PreviewMesh& mesh, const PreviewTransformProfile& transform);
    void setTrailPointLimit(int points);

public slots:
    void setFrame(vicon_lsl::PreviewFrame frame);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct ProjectedPoint {
        QPointF point;
        double depth = 0.0;
    };

    struct Bounds {
        PreviewVec3 lower;
        PreviewVec3 upper;
        bool valid = false;
    };

    ProjectedPoint project(const PreviewVec3& point, const Bounds& bounds) const;
    Bounds sceneBounds() const;
    void includePoint(Bounds& bounds, const PreviewVec3& point) const;
    std::optional<PreviewVec3> gazeEndpoint(const PreviewGazeRay& ray, const Bounds& bounds) const;

    PreviewFrame frame_;
    PreviewMesh stair_mesh_;
    PreviewTransformProfile stair_transform_;
    std::vector<PreviewTriangle> stair_triangles_;
    std::map<std::string, std::deque<PreviewVec3>> marker_trails_;
    int trail_point_limit_ = 24;
    double azimuth_degrees_ = -64.0;
    double elevation_degrees_ = 24.0;
    double zoom_ = 1.0;
    QPoint last_mouse_pos_;
};

} // namespace vicon_lsl
