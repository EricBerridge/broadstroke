#include "media/watercolor.hpp"

namespace bs {
namespace {
const float kWetEps = 1e-5f;
const int kDx[4] = {1, -1, 0, 0};
const int kDy[4] = {0, 0, 1, -1};
const int kOpp[4] = {1, 0, 3, 2};
}  // namespace

WatercolorSim::WatercolorSim(Canvas& canvas)
    : water(canvas.w, canvas.h, 0.f), sat(canvas.w, canvas.h, 0.f), susp(canvas.w, canvas.h, PigVec{}),
      cv_(canvas), tw_((canvas.w + kTile - 1) / kTile), th_((canvas.h + kTile - 1) / kTile),
      active_(static_cast<size_t>(tw_) * th_, 0), out_(canvas.w, canvas.h, std::array<float, 4>{}),
      water_n_(canvas.w, canvas.h, 0.f), susp_n_(canvas.w, canvas.h, PigVec{}), sat_n_(canvas.w, canvas.h, 0.f) {}

void WatercolorSim::touch(const Rect& r_in) {
  Rect r = r_in.clipped(cv_.w, cv_.h);
  if (r.empty()) return;
  for (int ty = r.y0 / kTile; ty <= (r.y1 - 1) / kTile; ++ty)
    for (int tx = r.x0 / kTile; tx <= (r.x1 - 1) / kTile; ++tx) active_[static_cast<size_t>(ty) * tw_ + tx] = 2;
}

bool WatercolorSim::any_wet() const {
  for (uint8_t a : active_)
    if (a == 2) return true;
  return false;
}

double WatercolorSim::total_water() const {
  double t = 0.0;
  for (size_t i = 0; i < water.v.size(); ++i) t += water.v[i] + sat.v[i] * params.capacity_mm;
  return t;
}

PigVec WatercolorSim::total_pigment() const {
  std::array<double, kMaxPig> acc{};
  for (size_t i = 0; i < susp.v.size(); ++i)
    for (int k = 0; k < kMaxPig; ++k) acc[k] += susp.v[i][k] + cv_.pig.v[i][k];
  PigVec r;
  for (int k = 0; k < kMaxPig; ++k) r[k] = static_cast<float>(acc[k]);
  return r;
}

void WatercolorSim::advance(float seconds) {
  int n = std::max(1, static_cast<int>(std::ceil(seconds / params.dt)));
  for (int i = 0; i < n; ++i) {
    if (!any_wet()) {
      // Nothing is flowing: the only thing time does is dry the damp paper,
      // which needs no stepping.
      float remaining = (n - i) * params.dt;
      for (int ty = 0; ty < th_; ++ty)
        for (int tx = 0; tx < tw_; ++tx) {
          uint8_t& a = active_[static_cast<size_t>(ty) * tw_ + tx];
          if (!a) continue;
          bool damp = false;
          for (int y = ty * kTile; y < std::min((ty + 1) * kTile, cv_.h); ++y)
            for (int x = tx * kTile; x < std::min((tx + 1) * kTile, cv_.w); ++x) {
              float& s = sat(x, y);
              s = std::max(s - 0.012f * remaining, 0.f);
              damp = damp || s > 0.02f;
            }
          a = damp ? 1 : 0;
        }
      return;
    }
    step();
  }
}

void WatercolorSim::dry(float max_seconds) {
  int n = static_cast<int>(max_seconds / params.dt);
  for (int i = 0; i < n && any_wet(); ++i) step();
  // Whatever is still wet evaporates where it stands.
  for (int ty = 0; ty < th_; ++ty)
    for (int tx = 0; tx < tw_; ++tx) {
      uint8_t& a = active_[static_cast<size_t>(ty) * tw_ + tx];
      if (!a) continue;
      for (int y = ty * kTile; y < std::min((ty + 1) * kTile, cv_.h); ++y)
        for (int x = tx * kTile; x < std::min((tx + 1) * kTile, cv_.w); ++x) {
          PigVec& s = susp(x, y);
          PigVec& d = cv_.pig(x, y);
          for (int k = 0; k < kMaxPig; ++k) { d[k] += s[k]; s[k] = 0.f; }
          water(x, y) = 0.f;
          sat(x, y) = 0.f;
        }
      cv_.mark_dirty({tx * kTile, ty * kTile, (tx + 1) * kTile, (ty + 1) * kTile});
      a = 0;
    }
}

void WatercolorSim::step() {
  // Tiles to process: every flagged tile and its neighbors. Free water (and
  // the pigment suspended in it) exists only in tiles flagged 2, so a tile
  // with such a tile in its 3x3 neighborhood may hold water or receive it
  // and gets the full update. The remaining tiles hold no water and none can
  // reach them in this step; only their dampness changes.
  std::vector<int> tiles;  // full update first, then (from n_full on) dampness only
  std::vector<int> damp_only;
  for (int ty = 0; ty < th_; ++ty)
    for (int tx = 0; tx < tw_; ++tx) {
      bool on = false, wet = false;
      for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
          int nx = tx + i, ny = ty + j;
          if (nx < 0 || ny < 0 || nx >= tw_ || ny >= th_) continue;
          uint8_t a = active_[static_cast<size_t>(ny) * tw_ + nx];
          on = on || a != 0;
          wet = wet || a == 2;
        }
      if (wet) tiles.push_back(ty * tw_ + tx);
      else if (on) damp_only.push_back(ty * tw_ + tx);
    }
  const int n_full = static_cast<int>(tiles.size());
  tiles.insert(tiles.end(), damp_only.begin(), damp_only.end());
  if (tiles.empty()) return;
  const WaterParams& P = params;
  const int W = cv_.w, H = cv_.h;
  const float relief = std::max(cv_.surface.relief, 0.004f);
  const float absorb = P.absorb * cv_.surface.absorbency * P.dt;
  float settle[kMaxPig], gran[kMaxPig], lift[kMaxPig], stain[kMaxPig];
  for (int k = 0; k < kMaxPig; ++k) {
    const Pigment& pg = cv_.palette.pig[k];
    bool used = k < cv_.palette.n;
    settle[k] = used ? P.settle * pg.density * P.dt * P.depth_ref_mm : 0.f;
    gran[k] = used ? 0.8f * pg.granulation : 0.f;
    lift[k] = used ? P.lift * (1.f - pg.staining) * P.dt : 0.f;
    stain[k] = used ? P.stain * pg.staining * P.dt * P.depth_ref_mm : 0.f;
  }
  auto tile_rect = [&](int t) {
    int tx = t % tw_, ty = t / tw_;
    return Rect{tx * kTile, ty * kTile, std::min((tx + 1) * kTile, W), std::min((ty + 1) * kTile, H)};
  };
  const int nt = static_cast<int>(tiles.size());
  // A few tiles are not worth waking the thread pool for.
  auto run = [](int count, const std::function<void(int, int)>& fn) {
    if (count < 12) fn(0, count);
    else parallel_for(0, count, fn, 1);
  };

  // Pass A: how much of each cell's water leaves toward each neighbor. It
  // also finds, per tile, the bounding box of the cells that hold any water
  // or suspended pigment at all.
  std::vector<Rect> wet_box(n_full);
  std::vector<int> slot(static_cast<size_t>(tw_) * th_, -1);  // tile -> index among the full tiles
  for (int ti = 0; ti < n_full; ++ti) slot[tiles[ti]] = ti;
  run(n_full, [&](int lo, int hi) {
    const Vec8 zero = _mm256_setzero_ps();
    for (int ti = lo; ti < hi; ++ti) {
      Rect r = tile_rect(tiles[ti]);
      Rect box;
      for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x) {
          std::array<float, 4>& o = out_(x, y);
          o = {0.f, 0.f, 0.f, 0.f};
          float w = water(x, y);
          if (w > 0.f || _mm256_movemask_ps(_mm256_cmp_ps(load8(susp(x, y)), zero, _CMP_NEQ_UQ)) != 0)
            box.include(x, y);
          if (w <= kWetEps) continue;
          float eta = P.relief_share * cv_.tooth(x, y) + w;
          float mob = std::min(w / P.depth_ref_mm, 1.5f);
          float total = 0.f;
          for (int k = 0; k < 4; ++k) {
            int nx = x + kDx[k], ny = y + kDy[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            float wj = water(nx, ny);
            float d = eta - (P.relief_share * cv_.tooth(nx, ny) + wj);
            if (d <= 0.f) continue;
            if (wj <= kWetEps && sat(nx, ny) < P.damp) {
              // Dry paper ahead: the contact line holds until the head exceeds the pinning.
              d -= P.pin_mm;
              if (d <= 0.f) continue;
            }
            o[k] = P.flow * P.dt * 0.25f * mob * d;
            total += o[k];
          }
          float limit = 0.5f * w;
          float scale = total > limit ? limit / total : 1.f;
          for (int k = 0; k < 4; ++k) o[k] = o[k] * scale / w;  // as a share of the cell's water
        }
      wet_box[ti] = box;
    }
  });

  // Pass B: move water and pigment, then absorb, evaporate, settle.
  std::vector<uint8_t> state(tiles.size(), 0);
  std::vector<Rect> full_box(n_full);  // the part of each full tile that got the full update
  const float wick = P.wick * P.dt * 0.25f;
  // Cells with no water in or next to them: dampness wicks between neighbors
  // and dries off. Returns whether any cell in the span is still damp.
  auto damp_span = [&](int y, int xa, int xb) {
    bool damp = false;
    for (int x = xa; x < xb; ++x) {
      float sa0 = sat(x, y), sa = sa0;
      for (int k = 0; k < 4; ++k) {
        int nx = x + kDx[k], ny = y + kDy[k];
        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
        sa += wick * (sat(nx, ny) - sa0);
      }
      sa = std::max(sa - 0.012f * P.dt, 0.f);
      if (sa > 0.02f) damp = true;
      sat_n_(x, y) = std::min(sa, 1.f);
    }
    return damp;
  };
  run(nt, [&](int lo, int hi) {
    for (int ti = lo; ti < hi; ++ti) {
      Rect r = tile_rect(tiles[ti]);
      bool any_water = false, any_damp = false;
      if (ti >= n_full) {
        for (int y = r.y0; y < r.y1; ++y) any_damp = damp_span(y, r.x0, r.x1) || any_damp;
        state[ti] = any_damp ? 1 : 0;
        continue;
      }
      // Within the tile, only cells in or next to water need the full update:
      // the box around every wet cell of this tile and its neighbors, grown by
      // one cell (the farthest water can travel in a step).
      Rect full;
      {
        int tx = tiles[ti] % tw_, ty = tiles[ti] / tw_;
        Rect wet;
        for (int j = -1; j <= 1; ++j)
          for (int i = -1; i <= 1; ++i) {
            int nx = tx + i, ny = ty + j;
            if (nx < 0 || ny < 0 || nx >= tw_ || ny >= th_) continue;
            int s = slot[static_cast<size_t>(ny) * tw_ + nx];
            if (s >= 0) wet.include(wet_box[s]);
          }
        if (!wet.empty()) {
          wet = wet.grown(1);
          full = Rect{std::max(wet.x0, r.x0), std::max(wet.y0, r.y0), std::min(wet.x1, r.x1), std::min(wet.y1, r.y1)};
          if (full.empty()) full = Rect{};
        }
      }
      full_box[ti] = full;
      for (int y = r.y0; y < r.y1; ++y) {
        const bool in_rows = y >= full.y0 && y < full.y1;
        const int fx0 = in_rows ? full.x0 : r.x1, fx1 = in_rows ? full.x1 : r.x1;
        any_damp = damp_span(y, r.x0, fx0) || any_damp;
        any_damp = damp_span(y, fx1, r.x1) || any_damp;
        for (int x = fx0; x < fx1; ++x) {
          const float w0 = water(x, y);
          const PigVec& s0 = susp(x, y);
          const std::array<float, 4>& o = out_(x, y);
          float leave = o[0] + o[1] + o[2] + o[3];
          float w = w0 * (1.f - leave);
          const Vec8 s0v = load8(s0);
          Vec8 pv = _mm256_mul_ps(s0v, splat8(1.f - leave));
          float sa0 = sat(x, y), sa = sa0;
          int dry_nb = 0;
          for (int k = 0; k < 4; ++k) {
            int nx = x + kDx[k], ny = y + kDy[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            float wj = water(nx, ny);
            const PigVec& sj = susp(nx, ny);
            float fin = out_(nx, ny)[kOpp[k]];
            if (fin > 0.f) {
              w += wj * fin;
              pv = _mm256_add_ps(pv, _mm256_mul_ps(load8(sj), splat8(fin)));
            }
            if (wj <= kWetEps) ++dry_nb;
            else if (w0 > kWetEps) {
              // Pigment diffuses between wet neighbors, down the concentration gradient.
              float m = P.diffuse * P.dt * 0.25f * std::min(w0, wj);
              float iw0 = 1.f / w0, iwj = 1.f / wj;
              Vec8 gradient = _mm256_sub_ps(_mm256_mul_ps(load8(sj), splat8(iwj)), _mm256_mul_ps(s0v, splat8(iw0)));
              pv = _mm256_add_ps(pv, _mm256_mul_ps(splat8(m), gradient));
            }
            sa += wick * (sat(nx, ny) - sa0);  // dampness wicks through the sheet
          }
          PigVec p;
          store8(p, pv);
          PigVec& dep = cv_.pig(x, y);
          if (w > kWetEps) {
            // The paper drinks from the film.
            float ds = std::min(absorb * (1.f - sa), w / P.capacity_mm);
            if (ds > 0.f) { sa += ds; w -= ds * P.capacity_mm; }
            // Evaporation, strongest at the rim of the wet area: vapor escapes
            // sideways there as well as upward. The effect reaches a couple
            // of cells in from the edge.
            float rim = dry_nb * 0.25f;
            for (int k = 0; k < 4; ++k) {
              int nx = x + 2 * kDx[k], ny = y + 2 * kDy[k];
              if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
              if (water(nx, ny) <= kWetEps) rim += 0.125f;
            }
            w -= P.evaporate * P.dt * (1.f + P.edge_boost * std::min(rim, 1.f));
          } else {
            sa = std::max(sa - 0.012f * P.dt, 0.f);
          }
          if (w > kWetEps) {
            float valley = clamp01(0.5f - 0.5f * cv_.tooth(x, y) / relief);
            float inv_depth = 1.f / std::max(w, 0.02f);
            bool can_lift = w > 0.03f;
            for (int i = 0; i < kMaxPig; ++i) {
              if (p[i] > 0.f) {
                // Both ways of leaving the water act at the paper surface,
                // so the share of the column they take falls with its depth.
                // Heavy particles sink and gather in the valleys; staining
                // ones bind to the fibers evenly, crown or valley.
                float f = 1.f - std::exp(-(settle[i] * (1.f + gran[i] * valley) + stain[i]) * inv_depth);
                float d = p[i] * f;
                p[i] -= d;
                dep[i] += d;
              }
              if (can_lift && dep[i] > 0.f) {
                float l = dep[i] * lift[i];
                dep[i] -= l;
                p[i] += l;
              }
              if (p[i] < 0.f) p[i] = 0.f;
            }
            any_water = true;
          } else {
            // Dried out: whatever was still suspended stays where it is.
            w = 0.f;
            for (int i = 0; i < kMaxPig; ++i) {
              dep[i] += std::max(p[i], 0.f);
              p[i] = 0.f;
            }
          }
          if (sa > 0.02f) any_damp = true;
          water_n_(x, y) = w;
          susp_n_(x, y) = p;
          sat_n_(x, y) = std::min(sa, 1.f);
        }
      }
      state[ti] = any_water ? 2 : (any_damp ? 1 : 0);
    }
  });

  // Commit.
  run(nt, [&](int lo, int hi) {
    for (int ti = lo; ti < hi; ++ti) {
      Rect r = tile_rect(tiles[ti]);
      for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x) sat(x, y) = sat_n_(x, y);
      if (ti >= n_full) continue;
      const Rect& full = full_box[ti];
      for (int y = full.y0; y < full.y1; ++y)
        for (int x = full.x0; x < full.x1; ++x) {
          water(x, y) = water_n_(x, y);
          susp(x, y) = susp_n_(x, y);
        }
    }
  });
  for (int ti = 0; ti < nt; ++ti) {
    active_[tiles[ti]] = state[ti];
    // Pigment may have moved wherever the full update ran, including in a
    // tile whose last water has just gone: the artist must look there again.
    if (ti < n_full) cv_.mark_dirty(full_box[ti]);
  }
}

}  // namespace bs
