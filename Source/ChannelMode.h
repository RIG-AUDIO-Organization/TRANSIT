// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace lean {

// Two independent deck inputs plus up to four independently controlled
// stereo FX returns. "6 CH" does not claim six track transports.
enum class ChannelMode : int { Two = 2, Four = 4, Six = 6 };

constexpr bool validChannelCount(int count) noexcept {
    return count == 2 || count == 4 || count == 6;
}

constexpr ChannelMode channelModeFromCount(int count) noexcept {
    return count == 4 ? ChannelMode::Four
         : count == 6 ? ChannelMode::Six : ChannelMode::Two;
}

constexpr int returnChannelCount(ChannelMode mode) noexcept {
    return static_cast<int>(mode) - 2;
}

constexpr int returnChannelNumber(int auxIndex) noexcept {
    return auxIndex + 3;
}

} // namespace lean
