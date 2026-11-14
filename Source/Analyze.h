// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace lean {

struct BeatGrid {
    double bpm = 0.0;
    double originSeconds = 0.0; // beat anchor modulo one period
    double confidence = 0.0;    // normalized peak correlation
    bool phaseReliable = false;
};

// Offline worker analysis; the playback audio thread never calls this.
// Autocorrelation is evaluated at the real downsampled envelope rate, then
// the discrete peak is interpolated for sub-lag precision. Phase is estimated
// separately from positive onsets folded across successive beat periods.
// Unreliable phase estimates must NEVER be used for automatic phase chase.
inline BeatGrid analyzeBeatGrid(const float* stereo, int frames, double sampleRate) {
    BeatGrid result;
    if (!stereo || frames <= 0 || !std::isfinite(sampleRate)
        || sampleRate < 1000.0 || sampleRate > 768000.0)
        return result;
    constexpr double targetEnvelopeRate = 200.0;
    const int hop = std::max(1, (int)(sampleRate / targetEnvelopeRate));
    const double envelopeRate = sampleRate / (double)hop;
    const int limitedFrames = (int)std::min(
        (double)frames, sampleRate * 120.0);
    const std::size_t n = (std::size_t)limitedFrames / (std::size_t)hop;
    if (n < (std::size_t)(envelopeRate * 4.0))
        return result;

    std::vector<double> envelope(n, 0.0);
    const double alpha = 1.0 -
        std::exp(-2.0 * 3.14159265358979323846 * 8.0 / envelopeRate);
    double follower = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t start = i * (std::size_t)hop;
        double peak = 0.0;
        for (int j = 0; j < hop; ++j) {
            const std::size_t at = (start + (std::size_t)j) * 2u;
            const double l = std::isfinite(stereo[at])
                ? std::abs((double)stereo[at]) : 0.0;
            const double r = std::isfinite(stereo[at + 1u])
                ? std::abs((double)stereo[at + 1u]) : 0.0;
            peak = std::max(peak, std::max(l, r));
        }
        follower += alpha * (peak - follower);
        envelope[i] = follower;
    }

    double mean = 0.0;
    for (double value : envelope)
        mean += value;
    mean /= (double)n;
    double power = 0.0;
    for (double& value : envelope) {
        value -= mean;
        power += value * value;
    }
    if (!(power > 1.0e-12))
        return result;

    const int lagMin = std::max(2, (int)std::floor(60.0 * envelopeRate / 200.0));
    const int lagMax = (int)std::ceil(60.0 * envelopeRate / 60.0);
    if ((std::size_t)(lagMax + 2) >= n)
        return result;
    auto correlation = [&](int lag) {
        double sum = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const std::size_t len = n - (std::size_t)lag;
        for (std::size_t i = 0; i < len; ++i) {
            const double a = envelope[i];
            const double b = envelope[i + (std::size_t)lag];
            sum += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        return sum / std::sqrt(energyA * energyB + 1.0e-30);
    };
    int bestLag = lagMin;
    double best = -1.0;
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        const double score = correlation(lag);
        if (score > best) {
            best = score;
            bestLag = lag;
        }
    }
    if (!(best > 0.08))
        return result;

    // Musical half/double time selection. This changes the chosen beat
    // period, not the resampling/playback clock.
    double candidateBpm = 60.0 * envelopeRate / (double)bestLag;
    if (candidateBpm < 100.0) {
        const int shorter = bestLag / 2;
        if (shorter >= lagMin && correlation(shorter) > 0.6 * best)
            bestLag = shorter;
    } else if (candidateBpm > 170.0) {
        const int longer = bestLag * 2;
        if (longer <= lagMax && correlation(longer) > 0.8 * best)
            bestLag = longer;
    }

    // Refine the autocorrelation maximum with a bounded parabola.
    double offset = 0.0;
    if (bestLag > 1 && bestLag + 1 < (int)n) {
        const double a = correlation(bestLag - 1);
        const double b = correlation(bestLag);
        const double c = correlation(bestLag + 1);
        const double curvature = a - 2.0 * b + c;
        if (curvature < -1.0e-10)
            offset = std::clamp(0.5 * (a - c) / curvature, -0.5, 0.5);
    }
    const double refinedLag = (double)bestLag + offset;
    const double bpm = 60.0 * envelopeRate / refinedLag;
    if (!std::isfinite(bpm) || bpm < 59.0 || bpm > 201.0)
        return result;
    result.bpm = bpm;
    result.confidence = std::clamp(best, 0.0, 1.0);

    // Fold transient onset strengths into one beat period. Never assume that
    // the strongest detected event is necessarily the first beat of a bar.
    const int bins = std::max(2, (int)std::lround(refinedLag));
    std::vector<double> phase((std::size_t)bins, 0.0);
    const std::size_t analyzed = std::min(n, (std::size_t)(envelopeRate * 30.0));
    double total = 0.0;
    for (std::size_t i = 1; i < analyzed; ++i) {
        const double onset = std::max(0.0, envelope[i] - envelope[i - 1]);
        const double loc = std::fmod((double)i, refinedLag)
            * (double)bins / refinedLag;
        int cell = (int)loc;
        if (cell >= bins) cell = bins - 1;
        const double frac = loc - (double)cell;
        phase[(std::size_t)cell] += onset * (1.0 - frac);
        phase[(std::size_t)((cell + 1) % bins)] += onset * frac;
        total += onset;
    }
    if (!(total > 0.0))
        return result;
    const auto it = std::max_element(phase.begin(), phase.end());
    const double prominence = (*it) * (double)bins / total;
    if (result.confidence >= 0.35 && prominence >= 3.0) {
        const int peakBin = (int)std::distance(phase.begin(), it);
        result.originSeconds = (double)peakBin * refinedLag
            / ((double)bins * envelopeRate);
        result.phaseReliable = true;
    }
    return result;
}

inline double detectBpm(const float* interleavedStereo, int numFrames,
                        double sampleRate) {
    return analyzeBeatGrid(interleavedStereo, numFrames, sampleRate).bpm;
}

// Nearest-beat phase error, follower relative to leader, [-0.5, 0.5] beats.
// A positive error means the follower is ahead and must briefly slow down.
inline double phaseErrorBeats(double followerPosition, double followerOrigin,
                              double followerBpm, double leaderPosition,
                              double leaderOrigin, double leaderBpm) {
    if (!std::isfinite(followerPosition) || !std::isfinite(leaderPosition)
        || !std::isfinite(followerOrigin) || !std::isfinite(leaderOrigin)
        || !(followerBpm > 0.0) || !(leaderBpm > 0.0))
        return 0.0;
    const double followerBeats = (followerPosition - followerOrigin)
        * followerBpm / 60.0;
    const double leaderBeats = (leaderPosition - leaderOrigin)
        * leaderBpm / 60.0;
    return std::remainder(followerBeats - leaderBeats, 1.0);
}

inline double phaseCorrectedTempo(double baseTempo, double errorBeats) {
    if (!std::isfinite(baseTempo) || !(baseTempo > 0.0)
        || !std::isfinite(errorBeats))
        return baseTempo;
    // Maximum +/-2% temporary tempo correction; converges without ever
    // seeking a playing deck or introducing a step in beat phase.
    const double correction = std::clamp(-0.08 * errorBeats, -0.02, 0.02);
    return baseTempo * (1.0 + correction);
}

} // namespace lean
