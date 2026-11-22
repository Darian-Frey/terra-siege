#include "InfectionField.hpp"

#include "core/Config.hpp"

#include <cmath>

void InfectionField::init() {
  unload();
  m_n = Config::HEIGHTMAP_SIZE - 1;
  m_cell = Config::WORLD_SIZE / m_n;
  m_value.assign(static_cast<size_t>(m_n) * m_n, 0.0f);
  m_bytes.assign(m_value.size(), 0);
  Image img{m_bytes.data(), m_n, m_n, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
  m_tex = LoadTextureFromImage(img);
  SetTextureWrap(m_tex, TEXTURE_WRAP_REPEAT);
  SetTextureFilter(m_tex, TEXTURE_FILTER_BILINEAR);
  m_dirty = false;
}

void InfectionField::unload() {
  if (m_tex.id != 0) UnloadTexture(m_tex);
  m_tex = {};
}

void InfectionField::clear() {
  std::fill(m_value.begin(), m_value.end(), 0.0f);
  std::fill(m_bytes.begin(), m_bytes.end(), 0);
  m_dirty = true;
}

void InfectionField::deposit(float worldX, float worldZ, float radius, float amount) {
  if (m_n == 0) return;
  const int r = static_cast<int>(ceilf(radius / m_cell));
  const int cx = static_cast<int>(floorf(worldX / m_cell)), cz = static_cast<int>(floorf(worldZ / m_cell));
  for (int dz = -r; dz <= r; ++dz)
    for (int dx = -r; dx <= r; ++dx) {
      const float px = (cx + dx + 0.5f) * m_cell - worldX, pz = (cz + dz + 0.5f) * m_cell - worldZ;
      const float d = sqrtf(px * px + pz * pz) / radius;
      if (d >= 1.0f) continue;
      const int x = ((cx + dx) % m_n + m_n) % m_n, z = ((cz + dz) % m_n + m_n) % m_n;
      const size_t i = static_cast<size_t>(z) * m_n + x;
      float v = m_value[i] + amount * (1.0f - d * d);
      v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
      m_value[i] = v;
      const uint8_t b = static_cast<uint8_t>(v * 255.0f + 0.5f);
      if (b != m_bytes[i]) {
        m_bytes[i] = b;
        m_dirty = true;
      }
    }
}

float InfectionField::at(float worldX, float worldZ) const {
  if (m_n == 0) return 0.0f;
  const int x = (static_cast<int>(floorf(worldX / m_cell)) % m_n + m_n) % m_n;
  const int z = (static_cast<int>(floorf(worldZ / m_cell)) % m_n + m_n) % m_n;
  return m_value[static_cast<size_t>(z) * m_n + x];
}

float InfectionField::average(float worldX, float worldZ, float radius, float step) const {
  float sum = 0.0f;
  int n = 0;
  for (float dz = -radius; dz <= radius; dz += step)
    for (float dx = -radius; dx <= radius; dx += step) {
      if (dx * dx + dz * dz > radius * radius) continue;
      sum += at(worldX + dx, worldZ + dz);
      ++n;
    }
  return n ? sum / n : 0.0f;
}

void InfectionField::upload() {
  if (!m_dirty || m_tex.id == 0) return;
  UpdateTexture(m_tex, m_bytes.data());
  m_dirty = false;
}
