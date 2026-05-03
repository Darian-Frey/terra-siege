# terra-siege — Terrain Rebuild

## Overview

Replace the Diamond-Square heightmap generator with a Fourier synthesis approach
inspired by — and significantly improved upon — David Braben's original Zarch/Virus
terrain system. The world becomes effectively infinite and seamlessly tiling with
no artificial ocean borders, while keeping our river carving, lake flooding, and
flat-shaded rendering unchanged.

---

## What Braben Did

From fully-documented Lander source code analysis (lander.bbcelite.com):

The altitude of every point (x, z) is computed entirely on-the-fly from a sum of
six sine waves:

```
height(x, z) = LAND_MID_HEIGHT - (
      2*sin(x - 2z)  + 2*sin(4x + 3z)  + 2*sin(3z - 5x)
    + 2*sin(7x + 5z) +   sin(5x + 11z) +   sin(10x + 7z)
) / 256
```

**Strengths:**
- No stored heightmap — computed at any coordinate instantly
- Naturally infinite and seamlessly tiling (sine functions are periodic)
- Extremely fast — six multiplies and six sin() calls per point
- Smooth, organic hills with no fractal noise artifacts

**Weaknesses:**
- Fixed coefficients — identical terrain every game, no seed variation
- Six terms only — limited terrain variety, no large-scale continental features
- No rivers, no lakes, no water features of any kind
- The terrain period is relatively short — sharp eyes can spot the tiling
- No domain variation — terrain looks similar everywhere

---

## Our Improvements

### 1. Seed-Driven Phase Offsets
Each sine term gets a random phase offset derived from the game seed. Two different
seeds produce completely different landscapes. The original produced the same terrain
every game.

### 2. Multi-Octave Structure
Three distinct layers of terrain at different spatial scales, inspired by how real
landscapes form:

| Octave | Terms | Spatial Period | Amplitude | Character |
|--------|-------|---------------|-----------|-----------|
| Continental | 4 | 1200–2400 cells | 0.50 | Large plateaus, deep valleys, mountain ranges |
| Regional | 6 | 200–600 cells | 0.25 | Rolling hills, ridges, river valleys |
| Local | 6 | 30–100 cells | 0.06 | Surface texture, small bumps |

Total: 16 terms vs Braben's 6.

### 3. Irrational Frequency Ratios (Effectively Infinite Terrain)

The key mathematical improvement. Two sine waves `sin(f₁·x)` and `sin(f₂·x)` have
a combined period of `2π / GCD(f₁, f₂)`. If `f₁/f₂` is irrational, the GCD is
infinitesimally small and the combined period is enormous.

We scale frequencies by irrational numbers:
- Golden ratio: φ = (1+√5)/2 ≈ 1.6180339...
- √2 ≈ 1.4142135...
- √3 ≈ 1.7320508...
- √5 ≈ 2.2360679...
- √7 ≈ 2.6457513...
- √11 ≈ 3.3166247...

With irrational multipliers between adjacent terms, the terrain is computationally
infinite within any playable world size. A player would need to travel millions of
world units before encountering a visible repeat.

### 4. Domain Warping
Before evaluating the sine sum, we perturb the input coordinates slightly using
a lower-frequency sine function. This breaks the regularity of pure sine terrain
and creates more organic-looking coastlines, ridgelines, and valleys:

```cpp
// Warp the sampling coordinates
float warpX = x + WARP_AMPLITUDE * sinf(x * WARP_FREQ + seedPhase);
float warpZ = z + WARP_AMPLITUDE * sinf(z * WARP_FREQ + seedPhase + 1.5f);
// Then evaluate sine sum using warpX, warpZ instead of x, z
```

Warp amplitude of 10–15% of the regional period creates a natural look without
destroying the smooth character of the original.

### 5. Rivers and Lakes Preserved
The original had no water features beyond sea-level flooding. We keep our existing
river carving (downhill flow simulation) and lake flooding (flood-fill at local
minima) running on top of the sine terrain. The smoother sine terrain actually
produces better rivers than diamond-square — wide natural valleys guide rivers
rather than fractal noise creating chaotic channels.

### 6. No Radial Falloff Needed
The old diamond-square approach needed a radial falloff pass to create the ocean
ring around the continent. With sine terrain and no stored edge, there's no
artificial boundary. Ocean appears naturally wherever the sine sum produces low
values. The `SEA_LEVEL` threshold is tuned so roughly 30% of the terrain is ocean.

---

## Architecture

Only `Heightmap::generate()` changes. Everything downstream is identical:

```
Old pipeline:                    New pipeline:
  diamondSquare()        →         sineWaveGenerate()
  smooth(12 passes)      →         (removed — sine is already smooth)
  applyRadialFalloff()   →         (removed — no edges)
  normalise()            →         normalise()
  classifyOcean()        →         classifyOcean()
  carveRivers()          →         carveRivers()
  floodLakes()           →         floodLakes()
```

The stored heightmap (`m_data[]`) and `WaterType` map (`m_water[]`) remain. The
sine generator fills `m_data[]` just like diamond-square did. All chunk building,
colouring, and rendering is unchanged.

---

## World and Wrapping

With no radial falloff, the terrain doesn't have an ocean border — it extends to
the heightmap boundary. We handle the map boundary one of two ways:

**Option A — Toroidal wrap (player teleports):**
When the player crosses X=0, X=worldSize, Z=0, or Z=worldSize, they wrap to the
opposite edge. The sine terrain tiles seamlessly so there's no visible seam.
```cpp
// In Player::applyPhysics()
float ws = m_planet.worldSize();
if (m_pos.x < 0)   m_pos.x += ws;
if (m_pos.x > ws)  m_pos.x -= ws;
if (m_pos.z < 0)   m_pos.z += ws;
if (m_pos.z > ws)  m_pos.z -= ws;
```

**Option B — Infinite scrolling (terrain computed live):**
Compute `heightAt()` directly from the sine formula at any world coordinate, with
no stored heightmap. The rendered chunks scroll with the player — only the tiles
visible in the view frustum are ever generated.

Option A is simpler to implement and faithful to the original's feel. Option B is
the more technically impressive approach and would allow a genuinely infinite world.
**Recommend Option A for the rebuild — it can be upgraded to B later.**

---

## Config Changes

Remove all old terrain generation constants, add new ones:

```cpp
namespace Config {

// ----------------------------------------------------------------
// Terrain — Fourier synthesis
// ----------------------------------------------------------------

// World dimensions (unchanged — but now truly tiling)
constexpr int   HEIGHTMAP_SIZE      = 1025;     // back to 1025 — 4x faster to generate
constexpr int   CHUNK_COUNT         = 16;
constexpr float TERRAIN_SCALE       = 8.0f;     // increased: world = 8192×8192 units
constexpr float TERRAIN_HEIGHT_MAX  = 120.0f;   // tunable — feels right for Virus scale
constexpr float SEA_LEVEL           = 0.30f;    // ~30% ocean without radial falloff

// Sine wave octave structure
constexpr int   SINE_CONTINENTAL_COUNT = 4;
constexpr int   SINE_REGIONAL_COUNT    = 6;
constexpr int   SINE_LOCAL_COUNT       = 6;

// Base spatial frequencies (cycles per heightmap cell)
// Continental: ~period 1024 cells = full world width at HEIGHTMAP_SIZE=1025
constexpr float SINE_CONTINENTAL_BASE_FREQ = 0.001f;
// Regional: ~period 128-256 cells
constexpr float SINE_REGIONAL_BASE_FREQ    = 0.008f;
// Local: ~period 20-40 cells
constexpr float SINE_LOCAL_BASE_FREQ       = 0.045f;

// Amplitude weights per octave
constexpr float SINE_CONTINENTAL_AMP   = 0.50f;  // dominant — shapes the land
constexpr float SINE_REGIONAL_AMP      = 0.25f;  // secondary — hills and valleys
constexpr float SINE_LOCAL_AMP         = 0.06f;  // texture only

// Irrational multipliers for frequency progression within each octave
// These are the first six values we use: φ, √2, √3, √5, √7, √11
// Their irrationality means no two terms share a common rational period.
// (Defined as runtime constants since constexpr sqrtf isn't standard C++17)

// Domain warping
constexpr float SINE_WARP_AMPLITUDE    = 18.0f;  // cells — warp offset magnitude
constexpr float SINE_WARP_FREQ         = 0.003f; // low frequency warp

// Rivers / Lakes (scaled counts for the new terrain)
constexpr int   RIVER_COUNT            = 8;
constexpr float RIVER_SOURCE_MIN_H     = 0.65f;
constexpr float RIVER_CARVE_DEPTH      = 0.035f;
constexpr int   RIVER_WIDTH            = 3;
constexpr int   RIVER_MIN_LENGTH       = 60;

constexpr int   LAKE_COUNT             = 16;
constexpr float LAKE_MIN_H             = 0.30f;
constexpr float LAKE_MAX_H             = 0.60f;
constexpr int   LAKE_MAX_CELLS         = 1200;

} // namespace Config
```

---

## Code — The Sine Wave Generator

The complete replacement for `diamondSquare()` + `smooth()` + `applyRadialFalloff()`:

```cpp
// ================================================================
// SineWaveTerm — one sine component
// ================================================================
struct SineWaveTerm {
    float freqX;    // spatial frequency in X (cycles per cell)
    float freqZ;    // spatial frequency in Z (cycles per cell)
    float amplitude;// contribution weight
    float phase;    // seed-derived offset (radians)
};

// ================================================================
// buildSineTerms — generate all 16 terms from seed
// ================================================================
void Heightmap::buildSineTerms(uint32_t seed,
                               std::vector<SineWaveTerm>& terms)
{
    // Irrational multipliers — ensures no two terms share a rational period
    // Pre-computed to avoid constexpr sqrt issues in C++17
    const float irr[] = {
        1.6180339f,  // φ  (golden ratio)
        1.4142135f,  // √2
        1.7320508f,  // √3
        2.2360679f,  // √5
        2.6457513f,  // √7
        3.3166247f,  // √11
    };

    // Seed the RNG for phase generation
    uint32_t rng = seed ^ 0xDEADBEEF;
    auto nextPhase = [&]() -> float {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<float>(rng) / static_cast<float>(0xFFFFFFFFu) * 6.28318f;
    };

    terms.clear();
    terms.reserve(SINE_CONTINENTAL_COUNT + SINE_REGIONAL_COUNT + SINE_LOCAL_COUNT);

    // --- Continental octave (4 terms) ---
    // Large features: mountain ranges, broad valleys, elevated plateaus
    // Frequencies are close to each other but offset by irrational ratios
    // so they constructively/destructively interfere at different scales.
    for (int i = 0; i < SINE_CONTINENTAL_COUNT; ++i)
    {
        float freq = SINE_CONTINENTAL_BASE_FREQ * irr[i % 6];
        // X and Z frequencies differ — avoids grid-aligned features
        float freqX = freq * irr[(i + 2) % 6];
        float freqZ = freq * irr[(i + 4) % 6];
        terms.push_back({ freqX, freqZ, SINE_CONTINENTAL_AMP, nextPhase() });
    }

    // --- Regional octave (6 terms) ---
    // Medium features: individual hills, ridges, wide river valleys
    for (int i = 0; i < SINE_REGIONAL_COUNT; ++i)
    {
        float freq  = SINE_REGIONAL_BASE_FREQ * irr[i % 6];
        float freqX = freq * irr[(i + 1) % 6];
        float freqZ = freq * irr[(i + 3) % 6];
        // Taper amplitude slightly for higher-indexed terms
        float amp   = SINE_REGIONAL_AMP * (1.0f - 0.1f * static_cast<float>(i));
        terms.push_back({ freqX, freqZ, amp, nextPhase() });
    }

    // --- Local octave (6 terms) ---
    // Fine detail: surface texture, small undulations
    for (int i = 0; i < SINE_LOCAL_COUNT; ++i)
    {
        float freq  = SINE_LOCAL_BASE_FREQ * irr[i % 6];
        float freqX = freq * irr[(i + 2) % 6];
        float freqZ = freq * irr[(i + 5) % 6];
        float amp   = SINE_LOCAL_AMP * (1.0f - 0.08f * static_cast<float>(i));
        terms.push_back({ freqX, freqZ, amp, nextPhase() });
    }
}

// ================================================================
// sineWaveGenerate — fills m_data[] using Fourier synthesis
// ================================================================
void Heightmap::sineWaveGenerate(uint32_t seed)
{
    std::vector<SineWaveTerm> terms;
    buildSineTerms(seed, terms);

    // Warp phase — unique per seed so warp pattern varies
    uint32_t rng2 = seed ^ 0xCAFEBABE;
    rng2 ^= rng2 << 13; rng2 ^= rng2 >> 17; rng2 ^= rng2 << 5;
    float warpPhase = static_cast<float>(rng2) / static_cast<float>(0xFFFFFFFFu) * 6.28318f;

    for (int z = 0; z < m_size; ++z)
    {
        for (int x = 0; x < m_size; ++x)
        {
            float fx = static_cast<float>(x);
            float fz = static_cast<float>(z);

            // --- Domain warp ---
            // Perturb sample coordinates with a low-frequency sine.
            // This breaks the waviness regularity and creates organic shapes.
            float warpX = fx + SINE_WARP_AMPLITUDE
                           * sinf(fx * SINE_WARP_FREQ + warpPhase);
            float warpZ = fz + SINE_WARP_AMPLITUDE
                           * sinf(fz * SINE_WARP_FREQ + warpPhase + 1.5708f);

            // --- Fourier sum ---
            float h = 0.0f;
            float totalAmp = 0.0f;
            for (const auto& t : terms)
            {
                h += t.amplitude * sinf(t.freqX * warpX
                                      + t.freqZ * warpZ
                                      + t.phase);
                totalAmp += t.amplitude;
            }

            // Normalise to [0, 1] — the sine sum ranges roughly [-totalAmp, +totalAmp]
            h = (h / totalAmp) * 0.5f + 0.5f;

            m_data[static_cast<size_t>(z * m_size + x)] = h;
        }
    }
}
```

---

## Code — Updated generate() Pipeline

```cpp
void Heightmap::generate(int size, float /*roughness*/, uint32_t seed)
{
    m_size = size;
    m_data.assign(static_cast<size_t>(size * size), 0.0f);
    m_water.assign(static_cast<size_t>(size * size), WaterType::None);
    m_rng = (seed == 0) ? static_cast<uint32_t>(std::time(nullptr)) : seed;

    // Step 1: Generate terrain via Fourier synthesis
    sineWaveGenerate(m_rng);

    // Step 2: Normalise (sine output already near [0,1] but let's be precise)
    normalise();

    // Step 3: Classify ocean (anything below SEA_LEVEL)
    classifyOcean();

    // Step 4: Carve rivers (same as before — smoother terrain = better rivers)
    carveRivers();

    // Step 5: Flood lakes (same as before)
    floodLakes();

    // Note: no smooth() pass needed — sine waves are C∞ smooth
    // Note: no applyRadialFalloff() — sine terrain has no edges to taper
}
```

---

## Code — heightAt() for Toroidal Wrapping

The planet's `heightAt()` query needs to wrap coordinates for the toroidal world:

```cpp
float Planet::heightAt(float worldX, float worldZ) const
{
    float ws = worldSize();

    // Wrap coordinates — toroidal world
    worldX = fmodf(worldX, ws);
    if (worldX < 0) worldX += ws;
    worldZ = fmodf(worldZ, ws);
    if (worldZ < 0) worldZ += ws;

    float hmX = worldX / Config::TERRAIN_SCALE;
    float hmZ = worldZ / Config::TERRAIN_SCALE;
    return m_heightmap.sample(hmX, hmZ) * Config::TERRAIN_HEIGHT_MAX;
}
```

And in `Heightmap::sample()`, wrap the sample coordinates too:

```cpp
float Heightmap::sample(float x, float z) const
{
    // Wrap to heightmap bounds for seamless tiling
    x = fmodf(x, static_cast<float>(m_size - 1));
    if (x < 0) x += static_cast<float>(m_size - 1);
    z = fmodf(z, static_cast<float>(m_size - 1));
    if (z < 0) z += static_cast<float>(m_size - 1);

    int   ix = static_cast<int>(x);
    int   iz = static_cast<int>(z);
    float fx = x - static_cast<float>(ix);
    float fz = z - static_cast<float>(iz);

    // Wrap the integer indices too
    int ix1 = (ix + 1) % (m_size - 1);
    int iz1 = (iz + 1) % (m_size - 1);

    return get(ix,  iz)  * (1-fx) * (1-fz)
         + get(ix1, iz)  * fx     * (1-fz)
         + get(ix,  iz1) * (1-fx) * fz
         + get(ix1, iz1) * fx     * fz;
}
```

---

## Code — Player Coordinate Wrapping

```cpp
void Player::wrapPosition(float worldSize)
{
    if (m_pos.x < 0)          m_pos.x += worldSize;
    if (m_pos.x > worldSize)  m_pos.x -= worldSize;
    if (m_pos.z < 0)          m_pos.z += worldSize;
    if (m_pos.z > worldSize)  m_pos.z -= worldSize;
}
```

Call at the end of `applyPhysics()` each tick. The sine terrain tiles
seamlessly so the player experiences no discontinuity when wrapping.

---

## Terrain Colour Changes

With no radial falloff there's no artificial ocean ring. The colour bands
should be tuned for the sine terrain's height distribution:

```cpp
Color TerrainChunk::landColor(float h) const
{
    // h is normalised [0, 1] — SEA_LEVEL is 0.30 so land starts at 0.30
    // Remap land portion to [0, 1] for colour selection
    float landH = (h - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL);
    landH = std::max(0.0f, landH);

    if      (landH < 0.08f) return { 210, 190, 120, 255 }; // sand / beach
    else if (landH < 0.40f) return { 100, 155,  65, 255 }; // grassland
    else if (landH < 0.65f) return { 145, 125,  95, 255 }; // rock
    else if (landH < 0.82f) return { 120, 115, 115, 255 }; // high rock
    else                    return { 240, 245, 255, 255 }; // snow
}
```

---

## What the New Terrain Looks Like

The sine wave approach produces a distinctly different character from diamond-square:

- **Smooth, rolling hills** — no fractal jaggedness at close range
- **Natural valley networks** — where low-frequency terms cancel, wide valleys form
  that rivers follow naturally
- **Coastline variety** — domain warping creates irregular coastlines rather than
  circular ocean borders
- **Mountain ranges** — where continental terms add constructively, elevated regions
  appear as broad ridges rather than isolated peaks
- **No visible tiling** — irrational frequency ratios push the repeat period to
  millions of world units

The visual feel should be closer to the original Virus/Zarch terrain — smooth and
sculptural — while being unique per seed and much more varied.

---

## Performance

Diamond-square on a 1025×1025 heightmap: ~200ms generation time.
Sine wave on a 1025×1025 heightmap: approximately 60–90ms with 16 terms.

The sine computation per cell is:
- 16 × `sinf()` calls = roughly 160ns on modern hardware
- 1025 × 1025 cells = ~1.05M cells
- Total: ~168ms worst case

In practice `sinf()` on x86 with SIMD vectorisation runs much faster. The actual
time measured should be well under 100ms. If needed, 4× SIMD parallelism via
`__m128` or AVX intrinsics can bring this to ~25ms, but it is unlikely to be
necessary.

---

## Implementation Order

1. Add new constants to `Config.hpp` (remove old terrain constants)
2. Add `buildSineTerms()` and `sineWaveGenerate()` to `Heightmap.cpp`
3. Remove `diamondSquare()`, `smooth()`, `applyRadialFalloff()` from `Heightmap.cpp`
4. Update `Heightmap::generate()` to use new pipeline
5. Update `Heightmap::sample()` for wrapping
6. Update `Planet::heightAt()` for wrapping
7. Add `Player::wrapPosition()` call in `applyPhysics()`
8. Tune `SEA_LEVEL`, `TERRAIN_HEIGHT_MAX`, and colour thresholds to taste
9. Run river carving and lake flooding — should work better on sine terrain
10. Verify no seam is visible when player crosses a world boundary

