//
//  CurveDisplay.h
//  Clearbox
//
//  The card under the header (since 0.9.0 the "Clearbox - Target EQ"
//  mockup): what the engine is doing to the spectrum right now.
//
//      APPLIED NOW     | TARGET 4.5 DB/OCT              +2.8 / -5.1 dB |
//      +2.8 dB         |        ___         (L)                         |
//      -5.1 dB         |  -----/   \___(M)___      ____                 |
//      Max lift / cut  |                     \(H)_/                     |
//      . LIFT . CUT    | 20   50   100   200   500   1k   2k   5k   20k |
//
//  On the left the largest lift and cut, and a legend. On the right the
//  curve: log frequency across, 20 Hz to 20 kHz; gain up and down, +-12 dB.
//  The curve is the sum of the dynamic curve, the resonance cuts and the
//  soft EQ after it, lift filled orange and cut blue, so Lift and Trim read
//  at a glance; the EQ's three bells sit on it as L, M and H. Behind it,
//  faint, the input's distance from the target curve. While there is too
//  little signal to measure, the header says HOLD: the curve is frozen until
//  the music comes back.
//
//  With Analyze on the display shows the input's deviation from the target
//  curve instead - above the line too loud, below too quiet - with one dB value
//  per octave underneath, to set an external EQ by.
//
//  Read-only: the panel's timer calls refresh() at 60 Hz. The shown values
//  glide towards the engine's (about 60 ms for the curve, longer for the
//  input trace) and are drawn as a spline, so the curve moves smoothly
//  instead of stepping band by band and frame by frame. Display only - the
//  audio is not affected.
//

#pragma once

#include <array>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

class CurveDisplay : public juce::Component
{
public:
    explicit CurveDisplay (ClearboxProcessor&);

    void refresh();
    void paint (juce::Graphics&) override;

    /** The largest lift and cut on the curve now (dynamic + EQ), for the checks. */
    float maxLiftDb() const noexcept;
    float maxCutDb() const noexcept;

    /** The analyzer's octave values, 63 Hz ... 16 kHz: the mean deviation from the
        target in dB, and whether there is enough of the band to say. */
    static constexpr int numOctaves = 9;
    static constexpr std::array<float, numOctaves> octaveHz { 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    float octaveDeviationDb (int i) const noexcept { return octaveDb[(size_t) i]; }
    bool octaveHasSignal (int i) const noexcept    { return octaveValid[(size_t) i]; }

private:
    // The curve as drawn is sampled on a fine grid, even in log frequency, so
    // a narrow bell is drawn as it is.
    static constexpr int gridPoints = 300;

    float xFor (float hz) const noexcept;
    float yFor (float db) const noexcept;
    juce::Rectangle<float> wellArea() const noexcept;
    juce::Rectangle<float> plotArea() const noexcept;
    void paintInfo (juce::Graphics&, juce::Rectangle<float> area);
    void paintScale (juce::Graphics&, juce::Rectangle<float> well);
    juce::Path curvePath (const float* dbOnGrid) const;
    std::array<float, gridPoints> deviationOnGrid() const;
    float totalAt (float frequency) const noexcept;
    juce::Path smoothPath (const float* yValues, bool smoothAcross, float extendLeft, float extendRight,
                           std::optional<float> leftEndY = {}) const;

    ClearboxProcessor& processor;
    int bands = 0;
    std::array<float, ClearEngine::maxBands> hz {}, gain {}, level {};

    // The curve as drawn: the dynamic curve plus the soft EQ, on a fine grid
    // even in log frequency, so a narrow bell is drawn as it is.
    std::array<float, gridPoints> gridHz {}, eqDb {}, totalDb {};
    float dynamicDbAt (float t) const noexcept;   // t: 0 at 20 Hz ... 1 at 20 kHz
    float resonanceDbAt (float frequency) const noexcept;
    float splineAt (const float* values, float t, bool carryOnBelow) const noexcept;
    void paintAnalyzer (juce::Graphics&, juce::Rectangle<float> well);

    // The analyzer.
    std::array<float, ClearEngine::maxBands> deviation {}, confidence {};
    std::array<float, numOctaves> octaveDb {};
    std::array<bool, numOctaves> octaveValid {};
    bool analyzing = false;
    bool bypassed = false, holding = false;
    double lastRefreshMs = 0.0;
    int framesSinceRepaint = 0;

    static constexpr float gainGlideSeconds = 0.06f, levelGlideSeconds = 0.15f;

    static constexpr float rangeDb = 12.0f;
};
