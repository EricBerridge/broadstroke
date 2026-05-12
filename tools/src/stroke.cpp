#include "broadstroke/tools/stroke.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace broadstroke::tools {

std::vector<float> packStroke(const Stroke& s) {
  std::vector<float> out;
  out.reserve(s.points.size() * 3);
  for (const auto& pt : s.points) {
    out.push_back(pt.x);
    out.push_back(pt.y);
    out.push_back(pt.pressure);
  }
  return out;
}

Stroke resampleUniform(const Stroke& source, float spacing) {
  if (source.points.size() < 2 || spacing <= 0.0f) {
    return source;
  }

  // Cumulative arclength.
  std::vector<float> arc(source.points.size(), 0.0f);
  for (std::size_t i = 1; i < source.points.size(); ++i) {
    const float dx = source.points[i].x - source.points[i - 1].x;
    const float dy = source.points[i].y - source.points[i - 1].y;
    arc[i] = arc[i - 1] + std::sqrt(dx * dx + dy * dy);
  }
  const float total = arc.back();
  if (total <= 0.0f) {
    return Stroke{{source.points.front(), source.points.back()}};
  }

  const int steps = std::max(1, static_cast<int>(std::round(total / spacing)));
  Stroke out;
  out.points.reserve(static_cast<std::size_t>(steps) + 1);

  std::size_t seg = 0;
  for (int k = 0; k <= steps; ++k) {
    const float target = (total * static_cast<float>(k)) / static_cast<float>(steps);
    while (seg + 1 < arc.size() && arc[seg + 1] < target) {
      ++seg;
    }
    const float a = arc[seg];
    const float b = arc[seg + 1];
    const float t = (b > a) ? (target - a) / (b - a) : 0.0f;
    const auto& p0 = source.points[seg];
    const auto& p1 = source.points[seg + 1];
    StrokePoint sp;
    sp.x        = p0.x        + t * (p1.x        - p0.x);
    sp.y        = p0.y        + t * (p1.y        - p0.y);
    sp.pressure = p0.pressure + t * (p1.pressure - p0.pressure);
    out.points.push_back(sp);
  }
  return out;
}

}  // namespace broadstroke::tools
