// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cmath>

namespace lean {

// The trim control allows 0..1.25 linear gain, with unity (1.0, 0 dB)
// in the physical centre of the dial. Keeping the engine's gain range
// intact also preserves MIDI behavior and headroom.
inline double trimDialPosition(double gain, double maximum = 1.25) noexcept {
    if (!std::isfinite(gain) || !std::isfinite(maximum) || maximum <= 1.0)
        return 0.5;
    if (gain <= 1.0)
        return 0.5 * std::clamp(gain, 0.0, 1.0);
    return 0.5 + 0.5 * std::clamp((gain - 1.0) / (maximum - 1.0), 0.0, 1.0);
}

} // namespace lean
