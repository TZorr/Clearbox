//
//  SoftEq.h
//  Clearbox
//
//  Three peak bands after the dynamic EQ, with no Q knob: Q follows the gain.
//  A little boost or cut is a broad, gentle bell; the more gain, the
//  narrower it gets - "proportional Q", as on many analogue consoles.
//
//      Q = 0.5 * 4^(|gain| / 12 dB)
//      1 dB -> 0.56 (about 2.3 octaves),  6 dB -> 1.0,  12 dB -> 2.0 (about 0.7 octaves)
//
//  Peaking biquads from the RBJ cookbook, transposed direct form II, stereo.
//  Frequency and gain glide over 20 ms (frequency on a log axis) and the
//  coefficients follow every 32 samples while anything moves, so turning a
//  knob does not zip. A band at 0 dB that has settled is skipped: the plugin
//  stays a wire with the EQ flat.
//
//  Plain C++, no JUCE, so the checks link it as is.
//

#pragma once

#include <array>

class SoftEq
{
public:
    static constexpr int numBands = 3;
    static constexpr float maxGainDb = 12.0f;

    struct Settings
    {
        std::array<float, numBands> hz { 200.0f, 1000.0f, 4000.0f };   // Low, Mid, High
        std::array<float, numBands> gainDb { 0.0f, 0.0f, 0.0f };
    };

    /** The rule this EQ is built on. */
    static float qForGain (float gainDb) noexcept;

    void prepare (double sampleRate);
    void reset();

    /** In place; `right` may be null (mono). */
    void process (float* left, float* right, int numSamples, const Settings&) noexcept;

    /** The response of both bands at `hz` for these settings, in dB - exactly
        the digital filters' (not an analogue idealisation), for the panel. */
    float magnitudeDb (float hz, const Settings&) const noexcept;

    /** The same at many frequencies at once (the filters designed once). */
    void magnitudeDb (const float* hz, float* outDb, int count, const Settings&) const noexcept;

private:
    struct Coefficients { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    static Coefficients design (double sampleRate, float hz, float gainDb) noexcept;
    static double responseDb (const Coefficients&, double sampleRate, double hz) noexcept;

    double sampleRate = 48000.0;
    std::array<Coefficients, numBands> coeffs {};
    std::array<float, numBands> logHz {}, gain {};
    std::array<std::array<double, 2>, numBands> stateL {}, stateR {};
    bool primed = false;
    float glide = 0.0f;   // per-sample one-pole coefficient for 20 ms
};
