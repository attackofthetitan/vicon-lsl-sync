#include "preview/PreviewMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vicon_lsl {
namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(double degrees) {
    return degrees * kPi / 180.0;
}

PreviewVec3 rotateX(const PreviewVec3& value, double degrees) {
    const double angle = radians(degrees);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {value.x, value.y * c - value.z * s, value.y * s + value.z * c};
}

PreviewVec3 rotateY(const PreviewVec3& value, double degrees) {
    const double angle = radians(degrees);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {value.x * c + value.z * s, value.y, -value.x * s + value.z * c};
}

PreviewVec3 rotateZ(const PreviewVec3& value, double degrees) {
    const double angle = radians(degrees);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {value.x * c - value.y * s, value.x * s + value.y * c, value.z};
}

} // namespace

PreviewVec3 operator+(const PreviewVec3& left, const PreviewVec3& right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

PreviewVec3 operator-(const PreviewVec3& left, const PreviewVec3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

PreviewVec3 operator*(const PreviewVec3& value, double scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

PreviewVec3 operator/(const PreviewVec3& value, double scale) {
    return {value.x / scale, value.y / scale, value.z / scale};
}

double dot(const PreviewVec3& left, const PreviewVec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

PreviewVec3 cross(const PreviewVec3& left, const PreviewVec3& right) {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

double length(const PreviewVec3& value) {
    return std::sqrt(dot(value, value));
}

PreviewVec3 normalize(const PreviewVec3& value) {
    const double norm = length(value);
    if (norm <= 1e-12 || !std::isfinite(norm)) {
        return {};
    }
    return value / norm;
}

bool isFinite(const PreviewVec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

PreviewVec3 rotateEulerDegrees(const PreviewVec3& value, const PreviewVec3& degrees) {
    return rotateZ(rotateY(rotateX(value, degrees.x), degrees.y), degrees.z);
}

PreviewQuaternion normalizeQuaternion(const PreviewQuaternion& quaternion) {
    const double norm = std::sqrt(quaternion.x * quaternion.x + quaternion.y * quaternion.y +
                                  quaternion.z * quaternion.z + quaternion.w * quaternion.w);
    if (norm <= 1e-12 || !std::isfinite(norm)) {
        return {};
    }
    return {quaternion.x / norm, quaternion.y / norm, quaternion.z / norm, quaternion.w / norm};
}

PreviewQuaternion multiplyQuaternions(const PreviewQuaternion& left,
                                      const PreviewQuaternion& right) {
    return normalizeQuaternion({
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    });
}

PreviewQuaternion inverseQuaternion(const PreviewQuaternion& quaternion) {
    const PreviewQuaternion normalized = normalizeQuaternion(quaternion);
    return {-normalized.x, -normalized.y, -normalized.z, normalized.w};
}

PreviewVec3 rotateByQuaternion(const PreviewVec3& value, const PreviewQuaternion& quaternion) {
    const PreviewQuaternion q = normalizeQuaternion(quaternion);
    const PreviewVec3 qv{q.x, q.y, q.z};
    const PreviewVec3 uv = cross(qv, value);
    const PreviewVec3 uuv = cross(qv, uv);
    return value + uv * (2.0 * q.w) + uuv * 2.0;
}

PreviewVec3 applyTransformPoint(const PreviewTransformProfile& transform, const PreviewVec3& point) {
    if (!transform.enabled) {
        return point;
    }
    const PreviewVec3 scaled{
        point.x * transform.input_axis_sign.x * transform.scale,
        point.y * transform.input_axis_sign.y * transform.scale,
        point.z * transform.input_axis_sign.z * transform.scale,
    };
    const PreviewVec3 rotated = transform.use_quaternion_rotation
        ? rotateByQuaternion(scaled, transform.rotation)
        : rotateEulerDegrees(scaled, transform.rotation_degrees);
    return rotated + transform.translation;
}

PreviewVec3 applyTransformDirection(const PreviewTransformProfile& transform, const PreviewVec3& direction) {
    if (!transform.enabled) {
        return normalize(direction);
    }
    const PreviewVec3 rebased{
        direction.x * transform.input_axis_sign.x,
        direction.y * transform.input_axis_sign.y,
        direction.z * transform.input_axis_sign.z,
    };
    const PreviewVec3 rotated = transform.use_quaternion_rotation
        ? rotateByQuaternion(rebased, transform.rotation)
        : rotateEulerDegrees(rebased, transform.rotation_degrees);
    return normalize(rotated);
}

std::array<PreviewVec3, 3> segmentAxes(const PreviewQuaternion& quaternion) {
    const PreviewQuaternion q = normalizeQuaternion(quaternion);
    return {
        normalize(rotateByQuaternion({1.0, 0.0, 0.0}, q)),
        normalize(rotateByQuaternion({0.0, 1.0, 0.0}, q)),
        normalize(rotateByQuaternion({0.0, 0.0, 1.0}, q)),
    };
}

std::vector<PreviewTriangle> triangulateMesh(const PreviewMesh& mesh,
                                             const PreviewTransformProfile& transform) {
    std::vector<PreviewTriangle> triangles;
    for (const auto& face : mesh.faces) {
        if (face.size() < 3) {
            continue;
        }
        for (std::size_t index = 1; index + 1 < face.size(); ++index) {
            if (face[0] >= mesh.vertices.size() ||
                face[index] >= mesh.vertices.size() ||
                face[index + 1] >= mesh.vertices.size()) {
                continue;
            }
            triangles.push_back({
                applyTransformPoint(transform, mesh.vertices[face[0]]),
                applyTransformPoint(transform, mesh.vertices[face[index]]),
                applyTransformPoint(transform, mesh.vertices[face[index + 1]]),
            });
        }
    }
    return triangles;
}

std::optional<double> rayBoxDistance(const PreviewVec3& origin,
                                     const PreviewVec3& direction,
                                     const PreviewVec3& lower,
                                     const PreviewVec3& upper) {
    double t_near = -std::numeric_limits<double>::infinity();
    double t_far = std::numeric_limits<double>::infinity();
    const double origin_values[3] = {origin.x, origin.y, origin.z};
    const double direction_values[3] = {direction.x, direction.y, direction.z};
    const double lower_values[3] = {lower.x, lower.y, lower.z};
    const double upper_values[3] = {upper.x, upper.y, upper.z};

    for (int axis = 0; axis < 3; ++axis) {
        const double value = origin_values[axis];
        const double delta = direction_values[axis];
        if (std::abs(delta) < 1e-12) {
            if (value < lower_values[axis] || value > upper_values[axis]) {
                return std::nullopt;
            }
            continue;
        }

        double t1 = (lower_values[axis] - value) / delta;
        double t2 = (upper_values[axis] - value) / delta;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        t_near = std::max(t_near, t1);
        t_far = std::min(t_far, t2);
        if (t_near > t_far) {
            return std::nullopt;
        }
    }

    if (t_far <= 1e-9) {
        return std::nullopt;
    }
    return t_near > 1e-9 ? t_near : t_far;
}

std::optional<double> rayTriangleDistance(const PreviewVec3& origin,
                                          const PreviewVec3& direction,
                                          const std::vector<PreviewTriangle>& triangles,
                                          double max_distance) {
    std::optional<double> best;
    for (const auto& triangle : triangles) {
        const PreviewVec3 edge1 = triangle.b - triangle.a;
        const PreviewVec3 edge2 = triangle.c - triangle.a;
        const PreviewVec3 h = cross(direction, edge2);
        const double det = dot(edge1, h);
        if (std::abs(det) <= 1e-9) {
            continue;
        }
        const double inv_det = 1.0 / det;
        const PreviewVec3 s = origin - triangle.a;
        const double u = inv_det * dot(s, h);
        if (u < 0.0 || u > 1.0) {
            continue;
        }
        const PreviewVec3 q = cross(s, edge1);
        const double v = inv_det * dot(direction, q);
        if (v < 0.0 || u + v > 1.0) {
            continue;
        }
        const double t = inv_det * dot(edge2, q);
        if (t <= 1e-6 || t > max_distance) {
            continue;
        }
        if (!best || t < *best) {
            best = t;
        }
    }
    return best;
}

std::optional<PreviewVec3> raySceneEndpoint(const PreviewVec3& origin,
                                            const PreviewVec3& direction,
                                            const PreviewVec3& lower,
                                            const PreviewVec3& upper,
                                            const std::vector<PreviewTriangle>& triangles) {
    const PreviewVec3 unit_direction = normalize(direction);
    if (!isFinite(origin) || !isFinite(unit_direction) || length(unit_direction) <= 1e-12) {
        return std::nullopt;
    }
    const auto boundary_distance = rayBoxDistance(origin, unit_direction, lower, upper);
    if (!boundary_distance) {
        return std::nullopt;
    }
    const auto triangle_distance =
        rayTriangleDistance(origin, unit_direction, triangles, *boundary_distance);
    const double distance = triangle_distance.value_or(*boundary_distance);
    return origin + unit_direction * distance;
}

bool timestampWithinTolerance(double reference_timestamp,
                              double candidate_timestamp,
                              double tolerance_seconds) {
    return std::isfinite(reference_timestamp) &&
           std::isfinite(candidate_timestamp) &&
           std::abs(reference_timestamp - candidate_timestamp) <= tolerance_seconds;
}

} // namespace vicon_lsl
