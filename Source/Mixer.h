// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "Deck.h"
#include "Smooth.h"

namespace lean {

constexpr int kAuxBuses = 4;

// Host bridge for aux-bus inserts. Implemented by the shell (FxRack); the
// lean core only taps sends and mixes returns. Called on the audio thread.
// Contract: always writes valid output (processed audio, or silence when
// the slot is empty, bypassed, or unprepared).
struct AuxHost {
    virtual ~AuxHost() = default;
    virtual void processAux(int aux, float* left, float* right, int numSamples) = 0;
    // Called once per render block before any processAux call, unconditionally
    // (even when every send is silent). Reserved for block-boundary work that
    // must run every block: plugin pickup and suspend handshakes.
    virtual void blockBoundary() {}
};

// RBJ cookbook biquad, transposed direct form II. Coefficients are set from
// the message thread; render uses them lock-free. Plain C++, no dependencies.
class Biquad {
public:
    Biquad() = default;

    void setLowShelf(double fs, double freq, double gainDb);
    void setPeaking(double fs, double freq, double q, double gainDb);
    void setHighShelf(double fs, double freq, double gainDb);
    void reset() { s1 = s2 = 0.0f; }

    float process(float x) {
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;
};

// Three-band DJ EQ: low shelf 250 Hz, peaking mid 1 kHz (Q 0.9),
// high shelf 4 kHz. Gains in dB, clamped to [-24, +6].
// Control (set/setRate) is message-thread only and publishes through
// atomics; coefficients are rebuilt on the audio thread in updateIfNeeded,
// so the render path never observes torn coefficients.
class Eq3 {
public:
    void set(double fs, double lowDb, double midDb, double highDb);
    void setRate(double fs);
    // Message thread: request a state clear; the audio thread performs it at
    // the start of the next render (bands are audio-thread-only).
    void reset();
    double gainDb(int band) const {
        return gains[band < 0 ? 0 : (band > 2 ? 2 : band)].load(
            std::memory_order_seq_cst);
    }
    // Audio thread: rebuild coefficients if the message thread changed them.
    void updateIfNeeded();
    float process(int channel, float x);

private:
    Biquad bands[3][2]; // audio thread only
    std::atomic<double> fs{ 44100.0 };
    std::atomic<double> gains[3] = { 0.0, 0.0, 0.0 };
    std::atomic<bool> dirty{ true };
    std::atomic<bool> resetPending{ false }; // consumed on the audio thread
};

// Original bipolar COLOR FILTER: left low-pass, center dry, right high-pass.
// Only the audio thread owns filter state; message/MIDI threads write atomics.
class ColorFilter {
public:
    void set(float value) {
        position.setTarget(std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f);
    }
    float target() const { return position.targetValue(); }
    void setRate(double rate) {
        sampleRate.store(std::isfinite(rate) && rate >= 1000.0 ? rate : 44100.0,
                         std::memory_order_seq_cst);
        resetPending.store(true, std::memory_order_seq_cst);
    }
    void reset() { resetPending.store(true, std::memory_order_seq_cst); }
    void snap() { position.snap(position.targetValue()); reset(); }
    void process(float& left, float& right, float smooth);

private:
    SmoothFloat position{ 0.0f };
    std::atomic<double> sampleRate{ 44100.0 };
    std::atomic<bool> resetPending{ false };
    float ic1[2] = { 0.0f, 0.0f };
    float ic2[2] = { 0.0f, 0.0f };
    float g = 0.0f;
    int remaining = 0; // recalculate coefficients every 16 samples
    int direction = 0;
};

struct ChannelStrip {
    Deck deck;
    Eq3 eq;
    ColorFilter color;
    SmoothFloat trim{ 1.0f };  // 0..2
    SmoothFloat fader{ 1.0f }; // 0..1
    SmoothFloat sends[kAuxBuses]; // post-fader aux taps, 0..1
    std::atomic<bool> cue{ false }; // pre-fader listen to the phones bus
};

// Two-deck mixer: strips -> equal-power crossfader -> master gain.
// Realtime-safe render after setup; control setters are message-thread only.
class Mixer {
public:
    static constexpr int kChannels = 2;

    Mixer() {
        for (auto& ret : auxReturn)
            ret.snap(1.0f); // returns open by default; sends default closed
    }

    ChannelStrip& channel(int i) { return strips[i & 1]; }
    const ChannelStrip& channel(int i) const { return strips[i & 1]; }

    void setOutputRate(double rate);
    void setCrossfader(float v) { xfade.setTarget(std::clamp(v, 0.0f, 1.0f)); }
    void setMaster(float v) { master.setTarget(v); }
    void setPhonesLevel(float v) { phonesLevel.setTarget(v); }
    void setCue(int ch, bool on) { strips[ch & 1].cue.store(on); }
    void setCueBlend(float v) { cueBlend.setTarget(std::clamp(v, 0.0f, 1.0f)); }
    void setEq(int ch, double lowDb, double midDb, double highDb);
    void setColorFilter(int ch, float position) { strips[ch & 1].color.set(position); }
    float colorFilterLevel(int ch) const { return strips[ch & 1].color.target(); }
    void setTrim(int ch, float v) { strips[ch & 1].trim.setTarget(v); }
    void setFader(int ch, float v) { strips[ch & 1].fader.setTarget(v); }
    void setSend(int ch, int aux, float v) {
        if (aux >= 0 && aux < kAuxBuses)
            strips[ch & 1].sends[aux].setTarget(v);
    }
    void setAuxReturn(int aux, float v) {
        if (aux >= 0 && aux < kAuxBuses)
            auxReturn[aux].setTarget(std::clamp(v, 0.0f, 1.25f));
    }
    // FX return channels are genuine independent stereo mixer paths.
    // PFL taps the processed return before its return fader.
    void setAuxCue(int aux, bool on) {
        if (aux >= 0 && aux < kAuxBuses)
            auxCue[aux].store(on, std::memory_order_seq_cst);
    }
    bool auxCueEnabled(int aux) const {
        return aux >= 0 && aux < kAuxBuses
            && auxCue[aux].load(std::memory_order_seq_cst);
    }
    float auxChannelPeak(int aux) const {
        return aux >= 0 && aux < kAuxBuses
            ? auxPeak[aux].load(std::memory_order_seq_cst) : 0.0f;
    }
    void setAuxHost(AuxHost* host) { auxHost = host; }
    void reset();
    // Snap every smoothed control to its target (tests, deterministic setup).
    void snap();

    // Mixes into left/right (must hold numSamples). Clears first.
    void render(float* left, float* right, int numSamples) {
        render(left, right, nullptr, nullptr, numSamples);
    }
    // Full version: phonesL/R receive blend * cueMix + (1 - blend) * master.
    // Pass nullptr phones on stereo-only devices to skip the cue mix.
    void render(float* left, float* right, float* phonesL, float* phonesR,
                int numSamples);
    // Post-fader, pre-crossfader peak per channel from the last render call.
    // Audio thread only.
    float channelPeak(int ch) const { return stripPeak[ch & 1]; }
    // Commanded control levels (atomic targets; race-free from any thread).
    float faderLevel(int ch) const { return strips[ch & 1].fader.targetValue(); }
    float trimLevel(int ch) const { return strips[ch & 1].trim.targetValue(); }
    float sendLevel(int ch, int aux) const {
        if (aux < 0 || aux >= kAuxBuses)
            return 0.0f;
        return strips[ch & 1].sends[aux].targetValue();
    }
    float returnLevel(int aux) const {
        if (aux < 0 || aux >= kAuxBuses)
            return 0.0f;
        return auxReturn[aux].targetValue();
    }
    float masterLevel() const { return master.targetValue(); }
    float phonesLevelValue() const { return phonesLevel.targetValue(); }
    float xfadeValue() const { return xfade.targetValue(); }
    float blendValue() const { return cueBlend.targetValue(); }
    void eqGains(int ch, double& lo, double& mi, double& hi) const;

private:
    ChannelStrip strips[kChannels];
    SmoothFloat xfade{ 0.5f };
    SmoothFloat master{ 1.0f };
    SmoothFloat cueBlend{ 1.0f }; // phones: blend * cue + (1 - blend) * master
    SmoothFloat phonesLevel{ 1.0f }; // phones output gain, 0..1.25
    SmoothFloat auxReturn[kAuxBuses];
    std::atomic<bool> auxCue[kAuxBuses]{};
    std::atomic<float> auxPeak[kAuxBuses]{};
    AuxHost* auxHost = nullptr; // set once before audio starts, never changes
    std::atomic<double> outputRate{ 44100.0 };
    float stripPeak[kChannels] = { 0.0f, 0.0f }; // audio thread only
};

} // namespace lean
