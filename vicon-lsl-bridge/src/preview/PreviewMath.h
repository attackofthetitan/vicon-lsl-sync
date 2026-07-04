#pragma once

#include "preview/PreviewTypes.h"

#include <array>
#include <optional>
#include <vector>

namespace vicon_lsl {

PreviewVec3 operator+(const PreviewVec3& left, const PreviewVec3& right);
PreviewVec3 operator-(const PreviewVec3& left, const PreviewVec3& right);
PreviewVec3 operator*(const PreviewVec3& value, double scale);
PreviewVec3 operator/(const PreviewVec3& value, double scale);

double dot(const PreviewVec3& left, const PreviewVec3& right);
PreviewVec3 cross(const PreviewVec3& left, const PreviewVec3& right);
double length(const PreviewVec3& value);
PreviewVec3 normalize(const PreviewVec3& value);
bool isFinite(const PreviewVec3& value);

PreviewVec3 rotateEulerDegrees(const PreviewVec3& value, const PreviewVec3& degrees);
PreviewVec3 applyTransformPoint(const PreviewTransformProfile& transform, const PreviewVec3& point);
PreviewVec3 applyTransformDirection(const PreviewTransformProfile& transform, const PreviewVec3& direction);

std::array<PreviewVec3, 3> segmentAxes(const PreviewQuaternion& quaternion);

std::vector<PreviewTriangle> triangulateMesh(const PreviewMesh& mesh,
                                             const PreviewTransformProfile& transform);

std::optional<double> rayBoxDistance(const PreviewVec3& origin,
                                     const PreviewVec3& direction,
                                     const PreviewVec3& lower,
                                     const PreviewVec3& upper);
std::optional<double> rayTriangleDistance(const PreviewVec3& origin,
                                          const PreviewVec3& direction,
                                          const std::vector<PreviewTriangle>& triangles,
                                          double max_distance);
std::optional<PreviewVec3> raySceneEndpoint(const PreviewVec3& origin,
                                            const PreviewVec3& direction,
                                            const PreviewVec3& lower,
                                            const PreviewVec3& upper,
                                            const std::vector<PreviewTriangle>& triangles);

bool timestampWithinTolerance(double reference_timestamp,
                              double candidate_timestamp,
                              double tolerance_seconds);

} // namespace vicon_lsl
