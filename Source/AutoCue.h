// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace lean {

// We find a musical onset, not a beat-grid downbeat. FirstAudio favors the
// earliest continuous signal; SmartOnset favors a later salient attack.
enum class AutoCueMode { Off = 0, FirstAudio = 1, SmartOnset = 2 };

struct AutoCueResult {
    double seconds = 0.0;
    float confidence = 0.0f;
    bool detected = false;
};

// Offline decoder-worker analysis, never the audio callback. The limit and
// sample-window arithmetic are bounded even for malformed file metadata.
inline AutoCueResult detectAutoCue(const float* stereo, int frames, double rate,
                                   AutoCueMode mode) {
    AutoCueResult out;
    if (mode == AutoCueMode::Off || stereo == nullptr || frames <= 0
        || !std::isfinite(rate) || rate < 1000.0 || rate > 768000.0)
        return out;

    const int windowFrames = std::max(1, (int)(rate * 0.01)); // 10 ms
    const int limit = (int)std::min((double)frames, rate * 90.0);
    const int count = limit / windowFrames;
    if (count < 3)
        return out;

    // Channel-independent RMS. Do not reject mono-like L-only or R-only
    // audio, and never propagate NaN/Inf into thresholds.
    std::vector<float> levels;
    levels.reserve((std::size_t)count);
    for (int i = 0; i < count; ++i) {
        double sum = 0.0;
        const std::size_t start = (std::size_t)i * (std::size_t)windowFrames * 2u;
        for (int j = 0; j < windowFrames; ++j) {
            const std::size_t at = start + (std::size_t)j * 2u;
            const double l = std::isfinite(stereo[at]) ? std::abs((double)stereo[at]) : 0.0;
            const double r = std::isfinite(stereo[at + 1u])
                ? std::abs((double)stereo[at + 1u]) : 0.0;
            const double v = std::max(l, r);
            sum += v * v;
        }
        levels.push_back((float)std::sqrt(sum / (double)windowFrames));
    }

    // Approximate 99th-percentile peak: isolated one-frame pops should not
    // become the track's reference level. Use the quiet fifth of the first
    // five seconds to distinguish a noise-only lead-in from musical content.
    std::vector<float> ordered = levels;
    const std::size_t peakIndex = (ordered.size() - 1u) * 99u / 100u;
    std::nth_element(ordered.begin(), ordered.begin() + (std::ptrdiff_t)peakIndex,
                     ordered.end());
    const float peak = ordered[peakIndex];
    if (!std::isfinite(peak) || peak < 0.00005f)
        return out;
    const int noiseCount = std::min(count, 500);
    ordered.assign(levels.begin(), levels.begin() + noiseCount);
    const std::size_t noiseIndex = (ordered.size() - 1u) / 5u;
    std::nth_element(ordered.begin(), ordered.begin() + (std::ptrdiff_t)noiseIndex,
                     ordered.end());
    const float background = ordered[noiseIndex];
    const bool backgroundOnly = background > 0.00005f
        && peak > background * 5.0f;
    // Relative threshold handles quiet masters; a floor cap preserves quiet
    // fades before later loud drops. For a noisy lead-in, require signal
    // distinctly above that noise rather than triggering at sample zero.
    const float threshold = backgroundOnly
        ? std::max(0.00008f, std::max(background * 2.5f,
                                      std::min(0.002f, peak * 0.025f)))
        : std::max(0.00008f, std::min(0.002f, peak * 0.1f));

    const auto sustained = [&](int i, float level) {
        if (i + 2 >= count)
            return false;
        return levels[(std::size_t)i] >= level
            && levels[(std::size_t)i + 1u] >= level * 0.7f
            && levels[(std::size_t)i + 2u] >= level * 0.45f;
    };
    int first = -1;
    int salient = -1;
    for (int i = 0; i + 2 < count; ++i) {
        const float rms = levels[(std::size_t)i];
        if (first < 0 && sustained(i, threshold))
            first = i;
        if (mode != AutoCueMode::SmartOnset || salient >= 0)
            continue;
        // Salience needs a sustained rise over the local 30-ms past AND a
        // meaningful portion of the track's envelope. A single pop never
        // counts, even if extremely loud.
        const float before = levels[(std::size_t)(i >= 3 ? i - 3 : 0)];
        const float strong = std::max(threshold * 2.0f, peak * 0.14f);
        if (rms >= strong && sustained(i, std::max(strong * 0.65f, threshold))
            && rms >= std::max(before * 2.8f, strong))
            salient = i;
    }
    const int selected = mode == AutoCueMode::SmartOnset && salient >= 0
        ? salient : first;
    if (selected < 0)
        return out;

    // One short analysis frame of pre-roll keeps the transient intact.
    out.seconds = std::max(0.0, (double)(selected - 1) * (double)windowFrames / rate);
    out.detected = true;
    out.confidence = salient >= 0 && selected == salient ? 0.9f : 0.65f;
    return out;
}

} // namespace lean
