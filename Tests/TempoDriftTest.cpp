// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// Offline beat/transport stability: two different source rates share a
// single 44.1 kHz output clock, with no independent deck wallclock.
// Runs >60 seconds worth of frames in small 256-sample blocks.
#include "Deck.h"
#include "Analyze.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int errors = 0;

void check(bool ok, const char* label, double got, double expected) {
    std::printf("TEMPO_DRIFT %s %s (got %.9f expected %.9f)\n",
                ok ? "PASS" : "FAIL", label, got, expected);
    if (!ok)
        ++errors;
}

std::vector<float> silence(double rate, double seconds) {
    const int frames = (int)(rate * seconds);
    return std::vector<float>((std::size_t)frames * 2u, 0.0f);
}

void testLongRun(bool keylock) {
    constexpr double outputRate = 44100.0;
    constexpr int block = 256;
    constexpr int seconds = 60;
    lean::Deck a, b;
    a.load(silence(44100.0, 75.0), 44100.0);
    b.load(silence(48000.0, 75.0), 48000.0);
    a.setOutputRate(outputRate);
    b.setOutputRate(outputRate);
    a.setKeylock(keylock);
    b.setKeylock(keylock);
    a.play(true);
    b.play(true);
    float left[block], right[block];
    const int total = (int)outputRate * seconds;
    for (int off = 0; off < total; off += block) {
        const int n = std::min(block, total - off);
        std::fill(left, left + block, 0.0f);
        std::fill(right, right + block, 0.0f);
        a.render(left, right, n);
        std::fill(left, left + block, 0.0f);
        std::fill(right, right + block, 0.0f);
        b.render(left, right, n);
    }
    const double tA = a.getPositionSeconds();
    const double tB = b.getPositionSeconds();
    const double tolerance = keylock ? 0.015 : 0.00005;
    check(std::abs(tA - seconds) < tolerance,
          keylock ? "keylock A 60-second clock" : "resample A 60-second clock",
          tA, seconds);
    check(std::abs(tB - seconds) < tolerance,
          keylock ? "keylock B 60-second clock" : "resample B 60-second clock",
          tB, seconds);
    check(std::abs(tA - tB) < tolerance,
          keylock ? "keylock interdeck offset" : "resample interdeck offset",
          tA - tB, 0.0);
}

// Test the actual analyzer at two sample rates and tempos that cannot be
// represented by the old integer-lag BPM estimator.
std::vector<float> clickTrack(double rate, double bpm, double offsetSeconds) {
    const int frames = (int)(rate * 12.0);
    std::vector<float> pcm((std::size_t)frames * 2u, 0.0f);
    for (double time = offsetSeconds; time < 12.0; time += 60.0 / bpm) {
        const int center = (int)std::lround(time * rate);
        for (int i = 0; i < (int)(rate * 0.012); ++i) {
            if (center + i >= frames) break;
            const float wave = (float)(0.6 *
                std::sin(6.283185307179586 * 1400.0 * (double)i / rate));
            pcm[((std::size_t)center + (std::size_t)i) * 2u] = wave;
            pcm[((std::size_t)center + (std::size_t)i) * 2u + 1u] = wave;
        }
    }
    return pcm;
}

void testAnalyzer() {
    for (double rate : { 44100.0, 48000.0 }) {
        for (double bpm : { 120.0, 128.25, 137.5 }) {
            auto pcm = clickTrack(rate, bpm, 0.18);
            const auto result = lean::analyzeBeatGrid(
                pcm.data(), (int)(pcm.size() / 2u), rate);
            check(std::abs(result.bpm - bpm) < 0.7,
                  "fractional BPM with sample-rate conversion",
                  result.bpm, bpm);
        }
    }
}

void testSyncModel() {
    const double early = lean::phaseErrorBeats(10.1, 0.0, 120.0,
                                               10.0, 0.0, 120.0);
    check(std::abs(early - 0.2) < 1e-10, "phase ahead 0.2 beat", early, 0.2);
    const double late = lean::phaseErrorBeats(9.9, 0.0, 120.0,
                                              10.0, 0.0, 120.0);
    check(std::abs(late + 0.2) < 1e-10, "phase behind 0.2 beat", late, -0.2);
    check(lean::phaseCorrectedTempo(1.0, early) < 1.0,
          "ahead -> temporary slowdown",
          lean::phaseCorrectedTempo(1.0, early), 1.0);
    check(lean::phaseCorrectedTempo(1.0, late) > 1.0,
          "behind -> temporary speedup",
          lean::phaseCorrectedTempo(1.0, late), 1.0);
}

} // namespace

int main() {
    testLongRun(false);
    testLongRun(true);
    testAnalyzer();
    testSyncModel();
    std::printf("TEMPO_DRIFT %s (%d failures)\n", errors == 0 ? "PASS" : "FAIL", errors);
    return errors == 0 ? 0 : 1;
}
