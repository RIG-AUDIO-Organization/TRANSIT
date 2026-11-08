// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "ChannelMode.h"

#include <cstdio>

int main() {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* label) {
        std::printf("CHANNEL_MODE %s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failures;
    };
    check(lean::returnChannelCount(lean::ChannelMode::Two) == 0,
          "2CH has 2 decks and no return strips");
    check(lean::returnChannelCount(lean::ChannelMode::Four) == 2,
          "4CH has 2 decks and 2 FX return strips");
    check(lean::returnChannelCount(lean::ChannelMode::Six) == 4,
          "6CH has 2 decks and 4 FX return strips");
    check(lean::returnChannelNumber(0) == 3
              && lean::returnChannelNumber(3) == 6,
          "return indices become mixer channels 3 through 6");
    check(lean::validChannelCount(2) && lean::validChannelCount(4)
              && lean::validChannelCount(6) && !lean::validChannelCount(5),
          "reject unsupported layout counts");
    check(lean::channelModeFromCount(8) == lean::ChannelMode::Two,
          "invalid saved layout falls back to two-channel");
    return failures == 0 ? 0 : 1;
}
