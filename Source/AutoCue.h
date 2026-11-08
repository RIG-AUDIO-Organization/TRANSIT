// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace lean {

// Never guess a downbeat without a beat grid. Both modes find an audible
// onset; FirstAudio is conservative about any sustained audio, while
// SmartOnset prefers a salient attack over a very quiet ambient lead-in.
enum class AutoCueMode { Off = 0, FirstAudio = 1, SmartOnset = 2 };

struct AutoCueResult {
    double seconds = 0.0;
    float confidence = 0.0f;
    bool detected = false;
};

// Pure, offline PCM analysis. All allocation and scanning belong to the
// background decoder worker, never the realtime callback.
// Bounded to the first 90 seconds to avoid analyzing an entire DJ set file.
inline AutoCueResult detectAutoCue(const float* stereo, int frames, double rate,
                                   AutoCueMode mode) {
    AutoCueResult out;
    if (mode == AutoCueMode::Off || stereo == nullptr || frames <= 0
        || !std::isfinite(rate) || rate < 1000.0)
        return out;

    const int windowFrames = std::max(1, (int)(rate * 0.01)); // 10 ms
    const int limit = std::min(frames, (int)(rate * 90.0));
    const int count = limit / windowFrames;
    if (count < 3)
        return out;

    // RMS envelope. Mono-invariant measurement uses the louder of L/R.
    std::vector<float> levels;
    levels.reserve((std::size_t)count);
    for (int i = 0; i < count; ++i) {
        double sum = 0.0;
        const std::size_t start = (std::size_t)i * (std::size_t)windowFrames * 2u;
        for (int j = 0; j < windowFrames; ++j) {
            const std::size_t at = start + (std::size_t)j * 2u;
            const float l = stereo[at], r = stereo[at + 1];
            const double v = (std::isfinite(l) && std::isfinite(r))
                ? (double)std::max(std::abs(l), std::abs(r)) : 0.0;
            sum += v * v;
        }
        levels.push_back((float)std::sqrt(sum / (double)windowFrames));
    }

    // Robust low percentile from the beginning; a transient click does
    // not set the noise floor. A fixed -54 dBFS minimum avoids noise hunts.
    const int noiseCount = std::min(count, 500); // first 5 seconds
    std::vector<float> sorted(levels.begin(), levels.begin() + noiseCount);
    const std::size_t percentile = (std::size_t)(noiseCount / 5);
    std::nth_element(sorted.begin(), sorted.begin() + (std::ptrdiff_t)percentile,
                     sorted.end());
    const float floor = sorted[percentile];
    const float threshold = std::max(0.002f, floor * 4.0f);

    const auto sustained = [&](int i, float level) {
        if (i + 2 >= count)
            return false;
        // Reject one-window transients and single isolated clicks.
        return levels[(std::size_t)i] >= level
            && levels[(std::size_t)i + 1u] >= level * 0.7f
            && levels[(std::size_t)i + 2u] >= level * 0.45f;
    };

    int first = -1;
    int transient = -1;
    for (int i = 0; i + 2 < count; ++i) {
        const float rms = levels[(std::size_t)i];
        if (first < 0 && rms >= threshold && sustained(i, threshold))
            first = i;
        if (mode == AutoCueMode::SmartOnset && transient < 0
            && rms >= threshold * 2.0f && sustained(i, threshold * 1.3f)) {
            // Compare against up to 30 ms before the attack, not the
            // immediate preceding frame (which may already be rising).
            const float prior = levels[(std::size_t)(i >= 3 ? i - 3 : 0)];
            if (rms > std::max(threshold * 1.5f, prior * 2.8f))
                transient = i;
        }
    }

    const int selected = mode == AutoCueMode::SmartOnset && transient >= 0
        ? transient : first;
    if (selected < 0)
        return out;

    // Back off one analysis window for reliable cue-preview attack.
    out.seconds = std::max(0.0, (double)(selected - 1) * (double)windowFrames / rate);
    out.detected = true;
    out.confidence = selected == transient && transient >= 0 ? 0.90f : 0.65f;
    return out;
}

} // namespace lean
