#pragma once

#include "Config.hpp"

// Fixed-timestep accumulator.
// Call accumulate() each frame with the raw frame delta,
// then loop shouldTick()/consume() for each physics step,
// and use alpha() for render interpolation.

class Clock {
public:
  void accumulate(float frameTime) {
    // Guard against spiral of death on very slow frames
    if (frameTime > Config::MAX_FRAME_TIME)
      frameTime = Config::MAX_FRAME_TIME;
    m_accumulator += frameTime;
  }

  bool shouldTick() const { return m_accumulator >= Config::FIXED_DT; }

  void consume() { m_accumulator -= Config::FIXED_DT; }

  // Interpolation factor [0,1) for smooth rendering between ticks
  float alpha() const { return m_accumulator / Config::FIXED_DT; }

private:
  float m_accumulator = 0.0f;
};