// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "AutoCue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr int rate = 48000;
int failures = 0;
void expect(bool ok, const char* name) {
    std::printf("AUTO_CUE %s %s\n", ok ? "ok" : "FAIL", name);
    if (!ok) ++failures;
}
std::vector<float> make(double seconds = 8.0) {
    return std::vector<float>((std::size_t)(seconds * rate) * 2u, 0.0f);
}
void segment(std::vector<float>& a, double from, double to, float amplitude) {
    for (int i = (int)(from * rate); i < (int)(to * rate); ++i) {
        const std::size_t k = (std::size_t)i * 2u;
        a[k] = a[k + 1u] = amplitude;
    }
}
void run() {
    using lean::AutoCueMode;
    using lean::detectAutoCue;
    auto silence = make();
    expect(!detectAutoCue(silence.data(), (int)silence.size() / 2, rate,
                          AutoCueMode::SmartOnset).detected, "silence rejected");
    auto click = make();
    segment(click, 0.25, 0.255, 0.9f);
    segment(click, 2.0, 2.3, 0.35f);
    const auto result = detectAutoCue(click.data(), (int)click.size() / 2, rate,
                                       AutoCueMode::FirstAudio);
    expect(result.detected && result.seconds > 1.9 && result.seconds < 2.05,
           "ignore isolated click, find sustained first audio");
    expect(!detectAutoCue(click.data(), (int)click.size() / 2, rate,
                          AutoCueMode::Off).detected, "off mode");
    auto rising = make();
    for (int i = rate; i < rate * 3; ++i) {
        const float v = (float)(i - rate) / (float)(rate * 2) * 0.012f;
        const auto p = (std::size_t)i * 2u;
        rising[p] = rising[p + 1u] = v;
    }
    segment(rising, 3.0, 3.4, 0.4f);
    const auto quiet = detectAutoCue(rising.data(), (int)rising.size() / 2, rate,
                                      AutoCueMode::FirstAudio);
    const auto attack = detectAutoCue(rising.data(), (int)rising.size() / 2, rate,
                                       AutoCueMode::SmartOnset);
    expect(quiet.detected && quiet.seconds < 2.2, "first-audio fade-in");
    expect(attack.detected && attack.seconds > 2.8 && attack.seconds < 3.05,
           "smart onset chooses substantial attack");
    auto low = make();
    segment(low, 1.5, 3.5, 0.03f);
    expect(detectAutoCue(low.data(), (int)low.size() / 2, rate,
                         AutoCueMode::FirstAudio).detected, "quiet sustained intro");
    expect(!detectAutoCue(nullptr, 100, rate, AutoCueMode::SmartOnset).detected,
           "null guard");
}
} // namespace
int main() {
    run();
    return failures == 0 ? 0 : 1;
}
