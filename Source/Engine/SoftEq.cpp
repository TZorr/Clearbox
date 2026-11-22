//
//  SoftEq.cpp
//  Clearbox
//

#include "SoftEq.h"

#include <algorithm>
#include <cmath>
#include <complex>

float SoftEq::qForGain (float gainDb) noexcept
{
    return 0.5f * std::pow (4.0f, std::min (std::abs (gainDb), maxGainDb) / 12.0f);
}

SoftEq::Coefficients SoftEq::design (double rate, float hz, float gainDb) noexcept
{
    const auto f = std::clamp ((double) hz, 10.0, 0.49 * rate);
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto w = 2.0 * M_PI * f / rate;
    const auto alpha = std::sin (w) / (2.0 * qForGain (gainDb));
    const auto c = std::cos (w);
    const auto a0 = 1.0 + alpha / A;

    Coefficients k;
    k.b0 = (1.0 + alpha * A) / a0;
    k.b1 = -2.0 * c / a0;
    k.b2 = (1.0 - alpha * A) / a0;
    k.a1 = -2.0 * c / a0;
    k.a2 = (1.0 - alpha / A) / a0;
    return k;
}

double SoftEq::responseDb (const Coefficients& k, double rate, double hz) noexcept
{
    const auto z = std::polar (1.0, -2.0 * M_PI * hz / rate);
    const auto h = (k.b0 + k.b1 * z + k.b2 * z * z) / (1.0 + k.a1 * z + k.a2 * z * z);
    return 20.0 * std::log10 (std::abs (h));
}

void SoftEq::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    glide = (float) std::exp (-1.0 / (0.02 * sampleRate));
    reset();
}

void SoftEq::reset()
{
    for (auto& s : stateL) s = { 0.0, 0.0 };
    for (auto& s : stateR) s = { 0.0, 0.0 };
    primed = false;
}

void SoftEq::process (float* left, float* right, int numSamples, const Settings& settings) noexcept
{
    // The first block after a reset starts where the knobs are: no glide in from nowhere.
    if (! primed)
    {
        for (int b = 0; b < numBands; ++b)
        {
            logHz[(size_t) b] = std::log (settings.hz[(size_t) b]);
            gain[(size_t) b] = settings.gainDb[(size_t) b];
            coeffs[(size_t) b] = design (sampleRate, settings.hz[(size_t) b], gain[(size_t) b]);
        }
        primed = true;
    }

    constexpr int step = 32;

    for (int start = 0; start < numSamples; start += step)
    {
        const auto n = std::min (step, numSamples - start);
        const auto g32 = std::pow (glide, (float) n);

        for (int b = 0; b < numBands; ++b)
        {
            auto& lh = logHz[(size_t) b];
            auto& gd = gain[(size_t) b];
            const auto targetLog = std::log (std::clamp (settings.hz[(size_t) b], 10.0f, 0.49f * (float) sampleRate));
            const auto targetGain = std::clamp (settings.gainDb[(size_t) b], -maxGainDb, maxGainDb);

            const auto moving = std::abs (lh - targetLog) > 1.0e-5f || std::abs (gd - targetGain) > 1.0e-4f;
            if (moving)
            {
                lh = targetLog + (lh - targetLog) * g32;
                gd = targetGain + (gd - targetGain) * g32;
                if (std::abs (lh - targetLog) <= 1.0e-5f) lh = targetLog;
                if (std::abs (gd - targetGain) <= 1.0e-4f) gd = targetGain;
                coeffs[(size_t) b] = design (sampleRate, std::exp (lh), gd);
            }

            // Flat and settled: a wire, and the filter's memory let go so it starts clean.
            if (std::abs (gd) < 1.0e-6f && ! moving)
            {
                stateL[(size_t) b] = { 0.0, 0.0 };
                stateR[(size_t) b] = { 0.0, 0.0 };
                continue;
            }

            const auto& k = coeffs[(size_t) b];
            const auto run = [&] (float* x, std::array<double, 2>& s)
            {
                for (int i = start; i < start + n; ++i)
                {
                    const double in = x[i];
                    const double out = k.b0 * in + s[0];
                    s[0] = k.b1 * in - k.a1 * out + s[1];
                    s[1] = k.b2 * in - k.a2 * out;
                    x[i] = (float) out;
                }
            };

            run (left, stateL[(size_t) b]);
            if (right != nullptr && right != left)
                run (right, stateR[(size_t) b]);
        }
    }
}

float SoftEq::magnitudeDb (float hz, const Settings& settings) const noexcept
{
    double sum = 0.0;
    for (int b = 0; b < numBands; ++b)
        if (std::abs (settings.gainDb[(size_t) b]) > 1.0e-6f)
            sum += responseDb (design (sampleRate, settings.hz[(size_t) b], settings.gainDb[(size_t) b]), sampleRate, hz);
    return (float) sum;
}

void SoftEq::magnitudeDb (const float* hz, float* outDb, int count, const Settings& settings) const noexcept
{
    std::fill (outDb, outDb + count, 0.0f);

    for (int b = 0; b < numBands; ++b)
    {
        if (std::abs (settings.gainDb[(size_t) b]) <= 1.0e-6f)
            continue;

        const auto k = design (sampleRate, settings.hz[(size_t) b], settings.gainDb[(size_t) b]);
        for (int i = 0; i < count; ++i)
            outDb[i] += (float) responseDb (k, sampleRate, hz[i]);
    }
}
