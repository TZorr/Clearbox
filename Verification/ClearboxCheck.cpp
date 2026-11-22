//
//  ClearboxCheck.cpp
//  Clearbox
//
//  The engine, measured against signals whose answer is known. It links the
//  same ClearEngine.cpp the plugin runs.
//
//  - Neutral is a wire: with nothing turned up, the output is the input
//    delayed by the reported latency, at every sample rate, and comes back to
//    exactly that after the knobs return to 0.
//  - Silence stays silence, whatever the knobs; nothing blows up at full scale.
//  - The target: white and brown noise are pulled to the 4.5 dB/oct curve,
//    slowly, without overshoot, at the same loudness; 50 % goes half the way.
//  - The ends, linked stereo, mono, and the three soft bells.
//
//  Usage:  ClearboxCheck
//

#include <juce_core/juce_core.h>

#include "Engine/ClearEngine.h"
#include "Engine/SoftEq.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
    int failures = 0, checks = 0;

    void check (bool condition, const juce::String& what, const juce::String& detail = {})
    {
        ++checks;
        std::printf ("  %s  %-62s %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8(), detail.toRawUTF8());
        if (! condition)
            ++failures;
    }

    std::vector<float> pink (size_t n, unsigned seed, double rmsDb)
    {
        std::mt19937 g (seed);
        std::normal_distribution<float> d (0, 1);
        std::vector<float> o (n);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;   // Paul Kellet's filter

        for (auto& v : o)
        {
            const double w = d (g);
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            v = (float) (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
            b6 = w * 0.115926;
        }

        double s = 0;
        for (auto v : o) s += (double) v * v;
        const auto k = std::pow (10.0, rmsDb / 20.0) / std::sqrt (s / (double) n);
        for (auto& v : o) v = (float) (v * k);
        return o;
    }

    double rmsDb (const std::vector<float>& x, size_t from)
    {
        double s = 0;
        for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / (double) (x.size() - from) + 1e-30);
    }

    /** Energy in a band (three RBJ band-passes, Q 2, in series - steep enough
        that a loud tone two octaves away does not leak in), from `from` on, in dB. */
    double bandDb (const std::vector<float>& x, double hz, double rate, size_t from)
    {
        const double w = 2 * M_PI * hz / rate, al = std::sin (w) / 4.0, a0 = 1 + al;
        const double b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos (w) / a0, a2 = (1 - al) / a0;
        double z[3][2] {}, s = 0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            double v = x[i];
            for (auto& st : z)
            {
                const double y = b0 * v + st[0];
                st[0] = -a1 * y + st[1];
                st[1] = b2 * v - a2 * y;
                v = y;
            }
            if (i >= from) s += v * v;
        }

        return 10.0 * std::log10 (s + 1e-30);
    }

    /** Like bandDb, between two sample positions. */
    double bandDbBetween (const std::vector<float>& x, double hz, double rate, size_t from, size_t to)
    {
        const std::vector<float> piece (x.begin(), x.begin() + (long) to);
        return bandDb (piece, hz, rate, from);
    }

    std::vector<float> white (size_t n, unsigned seed, double rmsDb)
    {
        std::mt19937 g (seed);
        std::normal_distribution<float> d (0, 1);
        std::vector<float> o (n);
        for (auto& v : o) v = d (g);
        double s = 0;
        for (auto v : o) s += (double) v * v;
        const auto k = std::pow (10.0, rmsDb / 20.0) / std::sqrt (s / (double) n);
        for (auto& v : o) v = (float) (v * k);
        return o;
    }

    /** -6 dB/oct above ~40 Hz: leaky integrated white noise. */
    std::vector<float> brown (size_t n, unsigned seed, double rmsDb)
    {
        auto o = white (n, seed, 0.0);
        double y = 0;
        for (auto& v : o) { y = 0.995 * y + v; v = (float) y; }
        double s = 0;
        for (auto v : o) s += (double) v * v;
        const auto k = std::pow (10.0, rmsDb / 20.0) / std::sqrt (s / (double) n);
        for (auto& v : o) v = (float) (v * k);
        return o;
    }

    /** How far a signal's spectrum is from the TARGET curve: octave bands 125 Hz ... 8 kHz,
        level per Hz in the 4.5 dB/oct view minus the target, mean removed, rms in dB. */
    double targetDeviation (const std::vector<float>& x, double rate, size_t from, double lowest = 125.0)
    {
        std::vector<double> d;
        for (double f = lowest; f <= 8000.0; f *= 2.0)
            d.push_back (bandDb (x, f, rate, from) - 10.0 * std::log10 (f) + ClearEngine::targetSlopeDb * std::log2 (f / 1000.0)
                         - ClearEngine::targetShapeDb ((float) f));
        double mean = 0, s = 0;
        for (auto v : d) mean += v / (double) d.size();
        for (auto v : d) s += (v - mean) * (v - mean);
        return std::sqrt (s / (double) d.size());
    }

    /** A steep high-pass (three RBJ sections, 36 dB/oct): a passage without bass. */
    std::vector<float> highPass (std::vector<float> x, double hz, double rate)
    {
        const double w = 2 * M_PI * hz / rate, al = std::sin (w) / (2 * 0.707), c = std::cos (w), a0 = 1 + al;
        const double b0 = (1 + c) / 2 / a0, b1 = -(1 + c) / a0, b2 = (1 + c) / 2 / a0, a1 = -2 * c / a0, a2 = (1 - al) / a0;
        for (int section = 0; section < 3; ++section)
        {
            double z1 = 0, z2 = 0;
            for (auto& v : x) { const double y = b0 * v + z1; z1 = b1 * v - a1 * y + z2; z2 = b2 * v - a2 * y; v = (float) y; }
        }
        return x;
    }

    /** A narrow RBJ peak (a resonance) added in place. */
    std::vector<float> peaked (std::vector<float> x, double hz, double q, double db, double rate)
    {
        const double w = 2 * M_PI * hz / rate, A = std::pow (10.0, db / 40.0), al = std::sin (w) / (2 * q), a0 = 1 + al / A;
        const double b0 = (1 + al * A) / a0, b1 = -2 * std::cos (w) / a0, b2 = (1 - al * A) / a0, a1 = b1, a2 = (1 - al / A) / a0;
        double z1 = 0, z2 = 0;
        for (auto& v : x) { const double y = b0 * v + z1; z1 = b1 * v - a1 * y + z2; z2 = b2 * v - a2 * y; v = (float) y; }
        return x;
    }

    /** Energy within +-1.5 % of hz (Welch, 16384-point Hann), in dB: narrow enough to see one resonance. */
    double narrowDb (const std::vector<float>& x, double hz, double rate, size_t from)
    {
        constexpr int N = 16384, L = 14;
        static FFTSetup setup = vDSP_create_fftsetup (L, kFFTRadix2);
        std::vector<float> w (N), b (N), re (N / 2), im (N / 2);
        vDSP_hann_window (w.data(), N, vDSP_HANN_NORM);
        double sum = 0;
        for (size_t s = from; s + N <= x.size(); s += N / 2)
        {
            vDSP_vmul (x.data() + s, 1, w.data(), 1, b.data(), 1, N);
            DSPSplitComplex z { re.data(), im.data() };
            vDSP_ctoz ((const DSPComplex*) b.data(), 2, &z, 1, N / 2);
            vDSP_fft_zrip (setup, &z, 1, L, FFT_FORWARD);
            for (int i = (int) std::floor (hz * 0.985 * N / rate); i <= (int) std::ceil (hz * 1.015 * N / rate); ++i)
                sum += (double) re[(size_t) i] * re[(size_t) i] + (double) im[(size_t) i] * im[(size_t) i];
        }
        return 10.0 * std::log10 (sum + 1e-30);
    }

    struct Stereo { std::vector<float> l, r; };

    /** Through the engine in host-sized pieces; the output is shifted back by the latency. */
    Stereo run (ClearEngine& engine, Stereo in, const ClearEngine::Settings& s, int block = 480)
    {
        const auto latency = (size_t) engine.getLatencySamples();
        const auto n = in.l.size();
        in.l.resize (n + latency, 0.0f);
        in.r.resize (n + latency, 0.0f);

        for (size_t i = 0; i < in.l.size(); i += (size_t) block)
        {
            const auto m = (int) std::min ((size_t) block, in.l.size() - i);
            engine.process (in.l.data() + i, in.r.data() + i, m, s);
        }

        in.l.erase (in.l.begin(), in.l.begin() + (long) latency);
        in.r.erase (in.r.begin(), in.r.begin() + (long) latency);
        return in;
    }

    double errorDb (const std::vector<float>& a, const std::vector<float>& b, size_t from)
    {
        double e = 0, r = 0;
        for (size_t i = from; i < a.size(); ++i) { e += (double) (a[i] - b[i]) * (a[i] - b[i]); r += (double) b[i] * b[i]; }
        return 10.0 * std::log10 (e / (r + 1e-30) + 1e-30);
    }
}

int main()
{
    std::printf ("\nClearboxCheck\n\n");

    // ---- neutral is a wire, at every rate ----
    for (const auto [rate, expected] : { std::pair { 44100.0, 1024 }, { 48000.0, 1024 }, { 96000.0, 2048 }, { 192000.0, 4096 } })
    {
        ClearEngine engine;
        engine.prepare (rate, 512);
        const auto n = (size_t) rate * 2;
        const Stereo in { pink (n, 1, -20), pink (n, 2, -20) };
        const auto out = run (engine, in, {});
        check (engine.getLatencySamples() == expected,
               "latency at " + juce::String (rate / 1000.0, 1) + " kHz is " + juce::String (expected), juce::String (engine.getLatencySamples()));
        const auto e = juce::jmax (errorDb (out.l, in.l, (size_t) expected), errorDb (out.r, in.r, (size_t) expected));
        check (e < -110.0, "neutral is the input, delayed", juce::String (e, 1) + " dB");
    }

    ClearEngine engine;
    engine.prepare (48000.0, 512);
    constexpr double rate = 48000.0;

    check (engine.getNumBands() >= 30 && engine.getBandFrequency (0) >= 20.0f
               && engine.getBandFrequency (engine.getNumBands() - 1) <= 20001.0f,
           "one band per ERB, 20 Hz to 20 kHz", juce::String (engine.getNumBands()) + " bands");

    ClearEngine::Settings everything;
    everything.lift = 1.0f; everything.trim = 1.0f; everything.boostDb = 50.0f; everything.brighten = 1.0f;

    // ---- silence, full scale ----
    {
        const Stereo silent { std::vector<float> (96000, 0.0f), std::vector<float> (96000, 0.0f) };
        const auto out = run (engine, silent, everything);
        bool zero = true;
        for (size_t i = 0; i < out.l.size(); ++i) zero = zero && juce::exactlyEqual (out.l[i], 0.0f) && juce::exactlyEqual (out.r[i], 0.0f);
        check (zero, "silence stays silence with every knob up");

        engine.reset();
        std::mt19937 g (5);
        std::uniform_real_distribution<float> d (-1.0f, 1.0f);
        Stereo loud { std::vector<float> (96000), std::vector<float> (96000) };
        for (size_t i = 0; i < 96000; ++i) { loud.l[i] = d (g); loud.r[i] = d (g); }
        const auto hot = run (engine, loud, everything);
        float peak = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < hot.l.size(); ++i) { finite = finite && std::isfinite (hot.l[i]) && std::isfinite (hot.r[i]); peak = std::max (peak, std::abs (hot.l[i])); }
        check (finite && peak < 16.0f, "full-scale noise with every knob up stays finite and bounded", "peak " + juce::String (peak, 2));
    }

    const size_t n = 8 * 48000, settled = 4 * 48000;

    // ---- the ends: the top fades to 0 dB, the bottom follows the curve's slope ----
    {
        engine.reset();
        const Stereo in { pink (n, 81, -20), pink (n, 82, -20) };
        ClearEngine::Settings s;
        s.trim = 1.0f; s.brighten = -1.0f;
        const auto out = run (engine, in, s);
        const auto at12k = bandDb (out.l, 12000, rate, settled) - bandDb (in.l, 12000, rate, settled);
        const auto at19k = bandDb (out.l, 19000, rate, settled) - bandDb (in.l, 19000, rate, settled);
        check (at12k < -3.0 && std::abs (at19k) < 0.7, "above 14.5 kHz the curve fades to 0 dB",
               "12 kHz " + juce::String (at12k, 1) + ", 19 kHz " + juce::String (at19k, 2) + " dB");
    }
    {
        engine.reset();
        const Stereo in { pink (n, 91, -20), pink (n, 92, -20) };
        ClearEngine::Settings s;
        s.boostDb = -30.0f;
        const auto out = run (engine, in, s);
        const auto at22 = bandDb (out.l, 22, rate, settled) - bandDb (in.l, 22, rate, settled);
        const auto at47 = bandDb (out.l, 47, rate, settled) - bandDb (in.l, 47, rate, settled);
        const auto at90 = bandDb (out.l, 90, rate, settled) - bandDb (in.l, 90, rate, settled);
        check (at90 > at47 && at47 > at22, "below the lowest band the curve keeps its slope",
               "90 Hz " + juce::String (at90, 1) + ", 47 Hz " + juce::String (at47, 1) + ", 22 Hz " + juce::String (at22, 1) + " dB");
    }

    // ---- linked stereo; mono is the same as two equal channels ----
    {
        engine.reset();
        Stereo in { pink (n, 51, -20), pink (n, 52, -20) };
        // a resonance on the left only: +15 dB at 1 kHz
        {
            const double w = 2 * M_PI * 1000 / rate, A = std::pow (10.0, 15.0 / 40.0), al = std::sin (w) / 8.0, a0 = 1 + al / A;
            const double b0 = (1 + al * A) / a0, b1 = -2 * std::cos (w) / a0, b2 = (1 - al * A) / a0, a1 = b1, a2 = (1 - al / A) / a0;
            double z1 = 0, z2 = 0;
            for (auto& v : in.l) { const double y = b0 * v + z1; z1 = b1 * v - a1 * y + z2; z2 = b2 * v - a2 * y; v = (float) y; }
        }

        ClearEngine::Settings s;
        s.trim = 0.75f;
        const auto out = run (engine, in, s);
        const auto gl = bandDb (out.l, 1000, rate, settled) - bandDb (in.l, 1000, rate, settled);
        const auto gr = bandDb (out.r, 1000, rate, settled) - bandDb (in.r, 1000, rate, settled);
        check (gl < -1.0 && std::abs (gl - gr) < 1.5, "one curve for both channels (resonance on the left only)",
               "L " + juce::String (gl, 1) + ", R " + juce::String (gr, 1) + " dB");

        engine.reset();
        const auto mono = pink (n / 2, 61, -20);
        auto inMono = mono;
        inMono.resize (mono.size() + (size_t) engine.getLatencySamples(), 0.0f);
        for (size_t i = 0; i < inMono.size(); i += 480)
            engine.process (inMono.data() + i, inMono.data() + i, (int) std::min ((size_t) 480, inMono.size() - i), s);

        engine.reset();
        const auto twin = run (engine, { mono, mono }, s);
        bool same = true;
        for (size_t i = 0; i < twin.l.size(); ++i)
            same = same && juce::exactlyEqual (inMono[i + (size_t) engine.getLatencySamples()], twin.l[i]);
        check (same, "a mono buffer gives exactly what two equal channels give");
    }

    // ---- the target: the spectrum is pulled to the 4.5 dB/oct curve, slowly ----
    {
        ClearEngine::Settings s;
        s.trim = 1.0f; s.lift = 1.0f;

        for (const auto kind : { 0, 1 })
        {
            engine.reset();
            const Stereo in = kind == 0 ? Stereo { white (n, 101, -20), white (n, 102, -20) }
                                        : Stereo { brown (n, 103, -20), brown (n, 104, -20) };
            const auto out = run (engine, in, s);
            // White noise lies more than 20 dB under the curve in the low bass - which the
            // confidence gate rightly treats as missing - so it is judged from 250 Hz up.
            const auto lowest = kind == 0 ? 250.0 : 125.0;
            const auto before = targetDeviation (in.l, rate, settled, lowest), after = targetDeviation (out.l, rate, settled, lowest);
            check (after < 0.4 * before, juce::String ("Lift + Trim 100 % pull ") + (kind == 0 ? "white" : "brown") + " noise to the curve",
                   juce::String (before, 1) + " -> " + juce::String (after, 1) + " dB rms");

            if (kind == 0)
            {
                engine.reset();
                ClearEngine::Settings halfWay;
                halfWay.trim = 0.5f; halfWay.lift = 0.5f;
                const auto mid = targetDeviation (run (engine, in, halfWay).l, rate, settled, lowest);
                check (mid > 0.35 * before && mid < 0.65 * before, "50 % goes about half the way",
                       juce::String (before, 1) + " -> " + juce::String (mid, 1) + " dB rms");
                engine.reset();
            }
            const auto change = rmsDb (out.l, settled) - rmsDb (in.l, settled);
            if (kind == 0)
                check (std::abs (change) < 1.5, "and keeps the level", juce::String (change, 2) + " dB");
        }

        // Slow: white noise for 6 s, then brown. Half a second after the switch the
        // 8 kHz gain is still nearer the white setting than the brown one.
        engine.reset();
        const size_t half = 6 * 48000;
        Stereo in { white (half, 111, -20), white (half, 112, -20) };
        const auto b1 = brown (half, 113, -20), b2 = brown (half, 114, -20);
        in.l.insert (in.l.end(), b1.begin(), b1.end());
        in.r.insert (in.r.end(), b2.begin(), b2.end());
        const auto out = run (engine, in, s);
        const auto gainAt = [&] (size_t a, size_t b) { return bandDbBetween (out.l, 8000, rate, a, b) - bandDbBetween (in.l, 8000, rate, a, b); };
        const auto whiteGain = gainAt (half - 48000, half);
        const auto earlyGain = gainAt (half + 12000, half + 24000);
        const auto brownGain = gainAt (in.l.size() - 48000, in.l.size());
        check (std::abs (earlyGain - whiteGain) < std::abs (earlyGain - brownGain),
               "and slowly: 0.25-0.5 s after a change it has not caught up yet",
               "8 kHz: white " + juce::String (whiteGain, 1) + ", early " + juce::String (earlyGain, 1) + ", brown " + juce::String (brownGain, 1) + " dB");
        check (std::abs (gainAt (in.l.size() - 96000, in.l.size() - 48000) - brownGain) < 1.0,
               "but has after a few seconds");
        check (earlyGain > whiteGain - 1.0, "and does not overshoot on the way (slow loudness offset)",
               juce::String (earlyGain - whiteGain, 1) + " dB beyond");
    }

    // ---- the confidence gate: a passage without bass is left without bass ----
    {
        ClearEngine::Settings s;
        s.lift = 1.0f; s.trim = 1.0f;
        const auto gainAt = [&] (const Stereo& in, const Stereo& out, double hz, size_t a, size_t b)
        { return bandDbBetween (out.l, hz, rate, a, b) - bandDbBetween (in.l, hz, rate, a, b); };

        engine.reset();
        const Stereo full { pink (n, 121, -20), pink (n, 122, -20) };
        const auto fullOut = run (engine, full, s);

        engine.reset();
        const Stereo thin { highPass (full.l, 300.0, rate), highPass (full.r, 300.0, rate) };
        const auto thinOut = run (engine, thin, s);

        // The gain the engine applies below 150 Hz, read directly: a ratio of output to input
        // there would divide one tiny number by another, the high-pass having left ~70 dB less.
        float bass = -100.0f;
        for (int b = 0; b < engine.getNumBands(); ++b)
            if (engine.getBandFrequency (b) < 150.0f)
                bass = std::max (bass, engine.getDisplayGainDb (b));
        check (bass < 3.0f, "without bass, Lift does not fill the bass with noise", "largest gain below 150 Hz " + juce::String (bass, 1) + " dB");

        const auto d2k = gainAt (thin, thinOut, 2000.0, settled, n) - gainAt (full, fullOut, 2000.0, settled, n);
        const auto d8k = gainAt (thin, thinOut, 8000.0, settled, n) - gainAt (full, fullOut, 8000.0, settled, n);
        check (std::abs (d2k) < 1.5 && std::abs (d8k) < 1.5, "and the missing bass does not make the rest look too loud",
               "2 kHz " + juce::String (d2k, 2) + ", 8 kHz " + juce::String (d8k, 2) + " dB against full pink");

        // Bass comes back after 6 s without: it is not met by a boost.
        engine.reset();
        const size_t six = 6 * 48000;
        Stereo seq { highPass (pink (six, 131, -20), 300.0, rate), highPass (pink (six, 132, -20), 300.0, rate) };
        const auto back1 = pink (six, 133, -20), back2 = pink (six, 134, -20);
        seq.l.insert (seq.l.end(), back1.begin(), back1.end());
        seq.r.insert (seq.r.end(), back2.begin(), back2.end());
        const auto seqOut = run (engine, seq, s);
        const auto onReturn = gainAt (seq, seqOut, 80.0, six, six + 24000);
        check (onReturn < 3.0, "when the bass comes back it is not boosted", juce::String (onReturn, 1) + " dB in the first 0.5 s");
    }

    // ---- the global gate: silence and tails teach it nothing ----
    {
        ClearEngine::Settings s;
        s.lift = 1.0f; s.trim = 1.0f;
        const size_t four = 4 * 48000;
        auto music = white (3 * four, 141, -20);
        auto quiet = white (four, 142, -80);
        Stereo in { music, white (3 * four, 143, -20) };
        for (size_t i = four; i < 2 * four; ++i) { in.l[i] = quiet[i - four]; in.r[i] = quiet[i - four]; }

        engine.reset();
        const auto latency = (size_t) engine.getLatencySamples();
        Stereo buf = in;
        buf.l.resize (in.l.size() + latency, 0.0f);
        buf.r.resize (in.r.size() + latency, 0.0f);
        bool heldInPause = true, freeAfter = true;
        for (size_t i = 0; i < buf.l.size(); i += 480)
        {
            const auto m = (int) std::min ((size_t) 480, buf.l.size() - i);
            engine.process (buf.l.data() + i, buf.r.data() + i, m, s);
            const auto t = i + (size_t) m;
            if (t > four + 24000 && t < 2 * four) heldInPause = heldInPause && engine.isHolding();
            if (t > 2 * four + 24000 && t < 3 * four) freeAfter = freeAfter && ! engine.isHolding();
        }
        Stereo out { std::vector<float> (buf.l.begin() + (long) latency, buf.l.end()),
                     std::vector<float> (buf.r.begin() + (long) latency, buf.r.end()) };

        check (heldInPause && freeAfter, "it holds while the signal is near silence, and lets go when music returns");
        const auto before = bandDbBetween (out.l, 8000.0, rate, four - 24000, four) - bandDbBetween (in.l, 8000.0, rate, four - 24000, four);
        const auto after = bandDbBetween (out.l, 8000.0, rate, 2 * four + 2400, 2 * four + 14400) - bandDbBetween (in.l, 8000.0, rate, 2 * four + 2400, 2 * four + 14400);
        check (std::abs (after - before) < 1.0, "after a pause the curve is where it was",
               "8 kHz " + juce::String (before, 1) + " -> " + juce::String (after, 1) + " dB");
    }

    // ---- attack and release: too loud is trimmed sooner than a gap is refilled ----
    {
        ClearEngine::Settings s;
        s.trim = 1.0f; s.lift = 1.0f;
        const size_t len = 16 * 48000, on = 4 * 48000, off = 10 * 48000;
        Stereo in { pink (len, 151, -20), pink (len, 152, -20) };
        // +10 dB around 1 kHz between 4 s and 10 s
        {
            const double w = 2 * M_PI * 1000 / rate, A = std::pow (10.0, 10.0 / 40.0), al = std::sin (w) / 4.0, a0 = 1 + al / A;
            const double b0 = (1 + al * A) / a0, b1 = -2 * std::cos (w) / a0, b2 = (1 - al * A) / a0, a1 = b1, a2 = (1 - al / A) / a0;
            for (auto* ch : { &in.l, &in.r })
            {
                double z1 = 0, z2 = 0;
                for (size_t i = 0; i < len; ++i)
                {
                    const double v = (*ch)[i], y = b0 * v + z1;
                    z1 = b1 * v - a1 * y + z2; z2 = b2 * v - a2 * y;
                    if (i >= on && i < off) (*ch)[i] = (float) y;
                }
            }
        }

        engine.reset();
        const auto out = run (engine, in, s);
        const auto g = [&] (size_t a) { return bandDbBetween (out.l, 1000.0, rate, a, a + 4800) - bandDbBetween (in.l, 1000.0, rate, a, a + 4800); };
        const auto base = g (on - 9600), cut = g (off - 9600);
        const auto halfTime = [&] (size_t from, double start, double end)
        {
            for (size_t a = from; a + 4800 < len; a += 2400)
                if (std::abs (g (a) - start) >= 0.5 * std::abs (end - start))
                    return (double) (a - from) / rate;
            return 99.0;
        };
        const auto attack = halfTime (on, base, cut), release = halfTime (off, cut, base);
        check (cut < base - 3.0 && attack < release, "too loud is trimmed sooner than the trim lets go",
               "half way in " + juce::String (attack, 2) + " s, out in " + juce::String (release, 2) + " s");
    }

    // ---- the analyzer's numbers: deviation from the pure target, and confidence ----
    {
        // Pink noise falls 3 dB/oct, the target 4.5: in the target's view pink rises
        // 1.5 dB/oct, so from 250 Hz to 8 kHz (5 octaves) 7.5 dB - plus what the target's
        // own roll-off takes away at 8 kHz and not at 250 Hz.
        engine.reset();
        const Stereo in { pink (n, 161, -20), pink (n, 162, -20) };
        run (engine, in, {});
        const auto deviationAt = [&] (float hz)
        {
            int best = 0;
            for (int b = 0; b < engine.getNumBands(); ++b)
                if (std::abs (std::log2 (engine.getBandFrequency (b) / hz)) < std::abs (std::log2 (engine.getBandFrequency (best) / hz)))
                    best = b;
            return engine.getDisplayDeviationDb (best);
        };
        const auto slope = deviationAt (8000.0f) - deviationAt (250.0f);
        const auto expected = 7.5f + ClearEngine::targetShapeDb (250.0f) - ClearEngine::targetShapeDb (8000.0f);
        check (std::abs (slope - expected) < 1.0f, "pink noise reads as it should against the curve",
               "8 kHz - 250 Hz: " + juce::String (slope, 2) + " dB, expected " + juce::String (expected, 2));

        // Brighten tilts what Lift and Trim aim for, not what the analyzer measures against.
        engine.reset();
        ClearEngine::Settings tilted;
        tilted.brighten = 1.0f;
        run (engine, in, tilted);
        const auto slopeTilted = deviationAt (8000.0f) - deviationAt (250.0f);
        check (std::abs (slopeTilted - slope) < 0.3f, "the deviation ignores Brighten", juce::String (slopeTilted, 2) + " dB");

        engine.reset();
        const Stereo thin { highPass (in.l, 300.0, rate), highPass (in.r, 300.0, rate) };
        run (engine, thin, {});
        float lowSure = 0.0f;
        for (int b = 0; b < engine.getNumBands(); ++b)
            if (engine.getBandFrequency (b) < 150.0f)
                lowSure = std::max (lowSure, engine.getDisplayConfidence (b));
        check (lowSure < 0.5f, "a missing bass is reported as missing, not as a deviation", "confidence below 150 Hz " + juce::String (lowSure, 2));
    }

    // ---- the resonance stage: ringing peaks are cut, narrowly, and nothing else ----
    {
        ClearEngine::Settings s;
        s.resonance = 1.0f;
        const auto ring = [&] (unsigned seed, double level)
        {
            auto x = pink (n, seed, level);
            // Q 8 at 120 Hz (15 Hz wide, a room mode or a boomy note), Q 20 at 2.5 kHz:
            // narrower bass peaks than one 11.7 Hz analysis bin are found only at higher Sensitivity.
            return peaked (peaked (x, 120.0, 8.0, 15.0, rate), 2500.0, 20.0, 15.0, rate);
        };

        engine.reset();
        const Stereo in { ring (171, -20), ring (172, -20) };
        const auto out = run (engine, in, s);
        const auto at120 = narrowDb (out.l, 120.0, rate, settled) - narrowDb (in.l, 120.0, rate, settled);
        const auto at2500 = narrowDb (out.l, 2500.0, rate, settled) - narrowDb (in.l, 2500.0, rate, settled);
        check (at120 < -6.0 && at2500 < -6.0 && at2500 > -16.0, "Resonance 100 % flattens +15 dB peaks at 120 Hz and 2.5 kHz",
               "120 Hz " + juce::String (at120, 1) + ", 2.5 kHz " + juce::String (at2500, 1) + " dB");

        double worst = 0;
        for (const auto f : { 60.0, 240.0, 1250.0, 5000.0 })
            worst = std::max (worst, std::abs (bandDb (out.l, f, rate, settled) - bandDb (in.l, f, rate, settled)));
        check (worst < 1.0, "an octave away nothing changes", "largest " + juce::String (worst, 2) + " dB");

        engine.reset();
        const Stereo quietIn { ring (171, -40), ring (172, -40) }, loudIn { ring (171, -10), ring (172, -10) };
        const auto quietOut = run (engine, quietIn, s);
        engine.reset();
        const auto loudOut = run (engine, loudIn, s);
        const auto q = narrowDb (quietOut.l, 2500.0, rate, settled) - narrowDb (quietIn.l, 2500.0, rate, settled);
        const auto l = narrowDb (loudOut.l, 2500.0, rate, settled) - narrowDb (loudIn.l, 2500.0, rate, settled);
        check (std::abs (q - l) < 1.5, "and the same at -40 and -10 dBFS", juce::String (q, 1) + " / " + juce::String (l, 1) + " dB");

        engine.reset();
        const Stereo plain { pink (n, 181, -20), pink (n, 182, -20) };
        run (engine, plain, s);
        float deepest = 0.0f;
        for (int i = 0; i < ResonanceStage::gridPoints; ++i)
            deepest = std::min (deepest, engine.getResonance().getDisplayDb (i));
        check (deepest > -1.0f, "plain pink noise rings nowhere", "deepest cut " + juce::String (deepest, 2) + " dB");

        // Timing: a peak at 2.5 kHz from 4 s to 6 s; its cut followed block by block.
        engine.reset();
        auto base = pink (8 * 48000, 191, -20);
        const auto withPeak = peaked (base, 2500.0, 20.0, 15.0, rate);
        for (size_t i = 4 * 48000; i < 6 * 48000; ++i) base[i] = withPeak[i];
        auto copyL = base, copyR = base;
        int gi = 0;
        for (int i = 0; i < ResonanceStage::gridPoints; ++i)
            if (std::abs (ResonanceStage::gridHz (i) - 2500.0f) < std::abs (ResonanceStage::gridHz (gi) - 2500.0f)) gi = i;
        std::vector<float> trace;
        for (size_t i = 0; i < base.size(); i += 240)
        {
            engine.process (copyL.data() + i, copyR.data() + i, 240, s);
            trace.push_back (engine.getResonance().getDisplayDb (gi));
        }
        const auto atTime = [&] (double t) { return trace[(size_t) (t * 200.0)]; };   // 240 samples = 5 ms
        const auto full = atTime (5.9);
        double attack = 99, release = 99;
        for (double t = 4.0; t < 6.0; t += 0.005) if (atTime (t) <= 0.5f * full) { attack = t - 4.0; break; }
        for (double t = 6.0; t < 7.9; t += 0.005) if (atTime (t) >= 0.5f * full) { release = t - 6.0; break; }
        check (full < -6.0f && attack <= 0.1 && release >= 0.1 && release <= 0.4,
               "a peak is caught within 100 ms and let go after 0.1-0.4 s",
               "cut " + juce::String (full, 1) + " dB, in " + juce::String (attack * 1000.0, 0) + " ms, out " + juce::String (release * 1000.0, 0) + " ms");
    }

    // ---- back to neutral is a wire again ----
    {
        engine.reset();
        const Stereo in { pink (n, 71, -20), pink (n, 72, -20) };
        ClearEngine::Settings s;
        s.trim = 1.0f; s.lift = 1.0f;
        run (engine, in, s);
        const auto out = run (engine, in, {});   // no reset: the curve has to relax by itself
        const auto e = errorDb (out.l, in.l, settled);
        check (e < -110.0, "after the knobs return to 0 the curve relaxes to a wire", juce::String (e, 1) + " dB");
    }

    // ---- the soft EQ: Q follows the gain ----
    {
        SoftEq eq;
        eq.prepare (rate);

        // The response of a band, measured: a sine through it, after it has settled.
        const auto measure = [&] (const SoftEq::Settings& s, double hz)
        {
            eq.reset();
            std::vector<float> x (24000);
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) std::sin (2.0 * M_PI * hz * (double) i / rate);
            auto y = x;
            eq.process (y.data(), nullptr, (int) y.size(), s);
            double a = 0, b = 0;
            for (size_t i = 12000; i < x.size(); ++i) { a += (double) x[i] * x[i]; b += (double) y[i] * y[i]; }
            return 10.0 * std::log10 (b / a);
        };

        bool centred = true;
        juce::String detail;
        for (const auto g : { -12.0f, -6.0f, -3.0f, 3.0f, 6.0f, 12.0f })
        {
            SoftEq::Settings s;
            s.gainDb = { g, 0.0f, 0.0f };
            s.hz = { 300.0f, 1000.0f, 4000.0f };
            const auto at = measure (s, 300.0);
            centred = centred && std::abs (at - g) < 0.1;
            detail << juce::String (at, 2) << " ";
        }
        check (centred, "the bell reaches its gain at its centre (-12 ... +12 dB)", detail);

        // Width at half the gain in dB, in octaves, from the filter's own response.
        const auto width = [&] (float g)
        {
            SoftEq::Settings s;
            s.gainDb = { g, 0.0f, 0.0f };
            s.hz = { 1000.0f, 1000.0f, 4000.0f };
            float lo = 1000.0f, hi = 1000.0f;
            while (std::abs (eq.magnitudeDb (lo, s)) > std::abs (g) / 2) lo /= 1.005f;
            while (std::abs (eq.magnitudeDb (hi, s)) > std::abs (g) / 2) hi *= 1.005f;
            return std::log2 (hi / lo);
        };
        const auto w3 = width (3.0f), w12 = width (12.0f), c12 = width (-12.0f);
        check (w12 < 0.6 * w3, "the more gain, the narrower the bell",
               "3 dB: " + juce::String (w3, 2) + " oct, 12 dB: " + juce::String (w12, 2) + " oct");
        check (std::abs (c12 - w12) < 0.05, "a cut is as narrow as the same boost", juce::String (c12, 2) + " oct");

        SoftEq::Settings mixed;
        mixed.gainDb = { 6.0f, 3.0f, -4.0f };
        mixed.hz = { 120.0f, 800.0f, 3000.0f };
        double worst = 0;
        for (const auto f : { 60.0, 120.0, 500.0, 800.0, 3000.0, 9000.0 })
            worst = std::max (worst, std::abs (measure (mixed, f) - eq.magnitudeDb ((float) f, mixed)));
        check (worst < 0.2, "what the panel draws is what the filters do", juce::String (worst, 3) + " dB");

        // Flat is a wire.
        eq.reset();
        auto x = pink (48000, 201, -20);
        auto y = x;
        eq.process (y.data(), nullptr, (int) y.size(), {});
        bool wire = true;
        for (size_t i = 0; i < x.size(); ++i) wire = wire && juce::exactlyEqual (x[i], y[i]);
        check (wire, "both bands at 0 dB are a wire, bit for bit");

        // A fast sweep of both knobs: no clicks. A sine at 1 kHz through it should
        // never step by more than the sine itself does sample to sample, plus the
        // gain's own slope.
        eq.reset();
        std::vector<float> sine (96000);
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.5f * (float) std::sin (2.0 * M_PI * 1000.0 * (double) i / rate);
        float maxStep = 0.0f;
        bool finite = true;
        float previous = 0.0f;
        for (int block = 0; block < 96000 / 64; ++block)
        {
            SoftEq::Settings s;
            const auto phase = (float) block / 40.0f;
            s.gainDb = { 12.0f * std::sin (phase), 12.0f * std::sin (phase * 0.9f), -12.0f * std::sin (phase * 1.3f) };
            s.hz = { 600.0f + 500.0f * std::sin (phase * 0.7f), 1000.0f + 400.0f * std::sin (phase * 1.1f), 1500.0f + 1000.0f * std::sin (phase) };
            eq.process (sine.data() + block * 64, nullptr, 64, s);
            for (int i = 0; i < 64; ++i)
            {
                const auto v = sine[(size_t) (block * 64 + i)];
                finite = finite && std::isfinite (v);
                if (block > 0 || i > 0) maxStep = std::max (maxStep, std::abs (v - previous));
                previous = v;
            }
        }
        // a 1 kHz sine of peak P moves at most 2*pi*1000/48000*P per sample; with two bells
        // reaching 1 kHz P is at most 0.5 * 16 (+24 dB)
        check (finite && maxStep < (float) (2.0 * M_PI * 1000.0 / rate * 0.5 * 16.0 * 1.1),
               "sweeping both bands fast does not click", "largest step " + juce::String (maxStep, 4));
    }

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
