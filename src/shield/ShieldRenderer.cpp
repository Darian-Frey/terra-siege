#include "ShieldRenderer.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <cmath>

namespace shieldfx {

namespace {

// Build an orthonormal basis (right, up, forward) where `up` = the
// supplied unit `pole`. Right/forward are arbitrary axes in the plane
// perpendicular to the pole — only their orthogonality matters for
// drawing the cap (we sweep azimuth over a full 2π so the choice of
// the "zero azimuth" axis only rotates the cap mesh in-place).
//
// Picks the world axis least aligned with the pole as the seed for the
// cross product to avoid the degeneracy at pole ≈ ±X.
void capBasis(Vector3 pole, Vector3 &right, Vector3 &forward) {
  Vector3 seed = (fabsf(pole.x) < 0.9f) ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
  right = Vector3Normalize(Vector3CrossProduct(seed, pole));
  forward = Vector3CrossProduct(pole, right); // already unit since pole ⟂ right
}

// Eased flash envelope: alpha = (1 - t)² × peak, t = age / duration.
// Sharp peak then smooth tail — matches the storyprogramming.com
// shader pattern's intensity multiplier (linear decay × ring fade).
float capAlphaFraction(float ageSec) {
  float t = ageSec / Config::SHIELD_FLASH_DURATION;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float k = 1.0f - t;
  return k * k * Config::SHIELD_FLASH_ALPHA_PEAK;
}

} // anonymous namespace

void pushImpact(Vector3 *dirArr, float *timerArr, int slots,
                Vector3 hitDirLocal) {
  if (slots <= 0) return;
  // Prefer the first empty slot; if all full, evict the oldest.
  int target = 0;
  float oldestAge = -1.0f;
  for (int i = 0; i < slots; ++i) {
    if (timerArr[i] < 0.0f) { target = i; oldestAge = -2.0f; break; }
    if (timerArr[i] > oldestAge) { oldestAge = timerArr[i]; target = i; }
  }
  dirArr[target] = hitDirLocal;
  timerArr[target] = 0.0f;
}

void tickImpacts(float *timerArr, int slots, float dt) {
  for (int i = 0; i < slots; ++i) {
    if (timerArr[i] < 0.0f) continue;
    timerArr[i] += dt;
    if (timerArr[i] >= Config::SHIELD_FLASH_DURATION)
      timerArr[i] = -1.0f; // mark empty
  }
}

void renderImpacts(const Vector3 *dirArr, const float *timerArr, int slots,
                   Vector3 centre, float radius, float yaw,
                   Color baseColor) {
  if (radius <= 0.0f) return;

  float cy = cosf(yaw), sy = sinf(yaw);

  const float halfAng = Config::SHIELD_CAP_HALF_ANGLE_RAD;
  const int rings = Config::SHIELD_CAP_RINGS;
  const int slc = Config::SHIELD_CAP_SLICES;

  for (int s = 0; s < slots; ++s) {
    if (timerArr[s] < 0.0f) continue;
    float aFrac = capAlphaFraction(timerArr[s]);
    if (aFrac <= 0.001f) continue;

    // Local → world: rotation about Y by `yaw`. Standard 2D rotation
    // on the XZ plane; Y untouched (sectors are yaw-only by design).
    Vector3 local = dirArr[s];
    Vector3 pole = {
        local.x * cy + local.z * sy,
        local.y,
        -local.x * sy + local.z * cy,
    };
    pole = Vector3Normalize(pole);

    Vector3 right, fwd;
    capBasis(pole, right, fwd);

    Color col = baseColor;
    col.a = static_cast<unsigned char>(aFrac * 255.0f);

    // Triangle-strip emission for the cap. Latitudes are evenly spaced
    // from 0 (pole) to halfAng; azimuths sweep 0..2π. Two triangles per
    // (ring, slice) quad; the innermost ring degenerates to a fan at
    // the pole because the r=0 ring has zero radius (sin(0) = 0).
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);

    for (int r = 0; r < rings; ++r) {
      float th0 = halfAng * (static_cast<float>(r)     / rings);
      float th1 = halfAng * (static_cast<float>(r + 1) / rings);
      float c0 = cosf(th0), s0 = sinf(th0);
      float c1 = cosf(th1), s1 = sinf(th1);

      for (int j = 0; j < slc; ++j) {
        float ph0 = (2.0f * PI) * (static_cast<float>(j)     / slc);
        float ph1 = (2.0f * PI) * (static_cast<float>(j + 1) / slc);
        float cp0 = cosf(ph0), sp0 = sinf(ph0);
        float cp1 = cosf(ph1), sp1 = sinf(ph1);

        // Cap-local point at (theta, phi):
        //   (s * cos(phi)) * right + c * pole + (s * sin(phi)) * fwd
        // multiplied by radius and offset to centre.
        auto P = [&](float c, float s_, float cp, float sp) -> Vector3 {
          return Vector3{
              centre.x + radius * (s_ * cp * right.x + c * pole.x + s_ * sp * fwd.x),
              centre.y + radius * (s_ * cp * right.y + c * pole.y + s_ * sp * fwd.y),
              centre.z + radius * (s_ * cp * right.z + c * pole.z + s_ * sp * fwd.z),
          };
        };
        Vector3 a = P(c0, s0, cp0, sp0); // inner-near
        Vector3 b = P(c0, s0, cp1, sp1); // inner-far
        Vector3 c_ = P(c1, s1, cp0, sp0); // outer-near
        Vector3 d = P(c1, s1, cp1, sp1); // outer-far

        // Two triangles, outward-facing winding.
        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(c_.x, c_.y, c_.z);
        rlVertex3f(b.x, b.y, b.z);

        rlVertex3f(b.x, b.y, b.z);
        rlVertex3f(c_.x, c_.y, c_.z);
        rlVertex3f(d.x, d.y, d.z);
      }
    }
    rlEnd();
  }
}

} // namespace shieldfx
