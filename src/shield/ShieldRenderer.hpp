#pragma once

#include "core/Config.hpp"
#include "raylib.h"

// ====================================================================
// ShieldRenderer — shared low-poly spherical-cap "shield impact" FX.
//
// Pattern (informed by Halo / Star Citizen / Unity shader-graph hit
// tutorials, adapted to terra-siege's flat-shaded aesthetic):
//
//   1. Damage path stores hit DIRECTION in entity-local space — a unit
//      vector from the centre to the impact point, with entity yaw
//      already rolled out. Storing locally means the cap moves with
//      the ship as it turns rather than hanging in world space.
//
//   2. Each shielded entity owns a small ring buffer (4 slots). New
//      impacts evict the oldest active slot once full so sustained
//      fire reads as a burst of overlapping flashes.
//
//   3. Render path emits a low-poly hemispherical cap per active slot,
//      pole = the stored direction (rotated back to world by yaw),
//      alpha eased as (1-t)^2 — sharp peak + smooth tail.
//
// No textures, no fragment shader — just untextured translucent triangles
// matching the rest of the 1988 look. ~32 tris per cap, up to 4 caps per
// shielded entity × ~10 visible shielded entities = ~1280 tris/frame in
// the worst case. Trivial.
// ====================================================================
namespace shieldfx {

// Push a fresh impact into a 4-slot ring buffer. `hitDirLocal` is a
// unit vector in the entity's local frame (centre → impact, yaw
// already removed). Evicts the slot with the oldest timer when full.
void pushImpact(Vector3 *dirArr, float *timerArr, int slots,
                Vector3 hitDirLocal);

// Advance every active slot's age by dt. Slots whose age exceeds
// SHIELD_FLASH_DURATION are marked empty (timer = -1).
void tickImpacts(float *timerArr, int slots, float dt);

// Render every active impact as a translucent low-poly spherical cap.
// `centre` is the shield-sphere centre in world space; `radius` is the
// sphere radius (≈ entity.radius × SHIELD_VIS_RADIUS_SCALE); `yaw` is
// the entity's heading so the stored local direction is rotated back
// to world for placement.
//
// Optional `baseColor` lets callers tint per faction (player vs enemy)
// without recompiling — defaults to the dim blue used elsewhere.
void renderImpacts(const Vector3 *dirArr, const float *timerArr, int slots,
                   Vector3 centre, float radius, float yaw,
                   Color baseColor = {60, 140, 220, 255});

} // namespace shieldfx
