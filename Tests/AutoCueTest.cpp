// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include "AutoCue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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

    // Full-volume tracks starting at sample zero were previously missed
    // because the percentile noise estimate was multiplied above the signal.
    auto immediate = make();
    segment(immediate, 0.0, 4.0, 0.35f);
    const auto now = detectAutoCue(immediate.data(), (int)immediate.size() / 2,
                                  rate, AutoCueMode::FirstAudio);
    expect(now.detected && now.seconds < 0.02, "immediate full-level start");

    auto whisper = make();
    segment(whisper, 1.25, 2.6, 0.0005f);
    auto quietOnset = detectAutoCue(whisper.data(), (int)whisper.size() / 2,
                                   rate, AutoCueMode::FirstAudio);
    expect(quietOnset.detected && std::abs(quietOnset.seconds - 1.24) < 0.06,
           "very quiet but sustained audio");

    auto hiss = make();
    segment(hiss, 0.0, 5.0, 0.001f);
    segment(hiss, 2.75, 4.5, 0.3f);
    const auto hissOnset = detectAutoCue(hiss.data(), (int)hiss.size() / 2, rate,
                                         AutoCueMode::FirstAudio);
    expect(hissOnset.detected && hissOnset.seconds > 2.65
               && hissOnset.seconds < 2.8, "ignore constant noise intro");

    auto rightOnly = make();
    for (int i = rate; i < 2 * rate; ++i)
        rightOnly[(std::size_t)i * 2u + 1u] = 0.03f;
    const auto rightCue = detectAutoCue(rightOnly.data(),
        (int)rightOnly.size() / 2, rate, AutoCueMode::FirstAudio);
    expect(rightCue.detected && rightCue.seconds > 0.95
               && rightCue.seconds < 1.05, "single-channel opening");

    auto nonFinite = make();
    segment(nonFinite, 1.5, 2.5, 0.07f);
    nonFinite[(std::size_t)(rate * 2) * 2u] = std::numeric_limits<float>::quiet_NaN();
    expect(detectAutoCue(nonFinite.data(), (int)nonFinite.size() / 2, rate,
                         AutoCueMode::SmartOnset).detected,
           "isolated nonfinite sample cannot poison detection");
    expect(!detectAutoCue(silence.data(), (int)silence.size() / 2,
                          std::numeric_limits<double>::infinity(),
                          AutoCueMode::SmartOnset).detected, "invalid rate guard");
}
} // namespace
int main() {
    run();
    return failures == 0 ? 0 : 1;
}
