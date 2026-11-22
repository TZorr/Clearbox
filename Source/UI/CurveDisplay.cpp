//
//  CurveDisplay.cpp
//  Clearbox
//

#include "CurveDisplay.h"

#include "Palette.h"

CurveDisplay::CurveDisplay (ClearboxProcessor& p) : processor (p)
{
    for (int i = 0; i < gridPoints; ++i)
        gridHz[(size_t) i] = 20.0f * std::pow (1000.0f, (float) i / (float) (gridPoints - 1));

    setOpaque (true);
    refresh();
}

void CurveDisplay::refresh()
{
    const auto& engine = processor.getEngine();
    const auto newBands = engine.getNumBands();

    // Time: each value glides towards the engine's, whatever the timer rate.
    // The first refresh (and a new band layout) snaps, so the panel opens on the
    // curve as it is, not on a curve rising from zero.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto snap = lastRefreshMs <= 0.0 || newBands != bands;
    const auto dt = snap ? 0.0 : juce::jlimit (0.0, 0.25, 0.001 * (now - lastRefreshMs));
    lastRefreshMs = now;

    const auto follow = [dt, snap] (float& shown, float target, float tauSeconds)
    {
        shown = snap ? target : target + (shown - target) * (float) std::exp (-dt / tauSeconds);
    };

    bands = newBands;
    auto moved = snap;

    for (int b = 0; b < bands; ++b)
    {
        hz[(size_t) b] = engine.getBandFrequency (b);
        const auto before = gain[(size_t) b];
        follow (gain[(size_t) b], engine.getDisplayGainDb (b), gainGlideSeconds);
        follow (level[(size_t) b], engine.getDisplayLevelDb (b), levelGlideSeconds);
        follow (deviation[(size_t) b], engine.getDisplayDeviationDb (b), levelGlideSeconds);
        follow (confidence[(size_t) b], engine.getDisplayConfidence (b), levelGlideSeconds);
        moved = moved || std::abs (gain[(size_t) b] - before) > 0.005f;
    }

    const auto newBypassed = processor.parameters.getRawParameterValue (ClearParams::bypass)->load() >= 0.5f;
    const auto newHolding = engine.isHolding();
    const auto newAnalyzing = processor.isAnalyzing();
    moved = moved || newBypassed != bypassed || newHolding != holding || newAnalyzing != analyzing;
    analyzing = newAnalyzing;
    holding = newHolding;
    bypassed = newBypassed;

    // The sum on the grid. The EQ shows at once: a knob moves, the bell moves.
    processor.getEqMagnitudeDb (gridHz.data(), eqDb.data(), gridPoints);
    for (int i = 0; i < gridPoints; ++i)
    {
        const auto total = (bands >= 2 ? dynamicDbAt ((float) i / (float) (gridPoints - 1)) : 0.0f) + eqDb[(size_t) i]
                         + resonanceDbAt (gridHz[(size_t) i]);
        moved = moved || std::abs (total - totalDb[(size_t) i]) > 0.005f;
        totalDb[(size_t) i] = total;
    }

    // The analyzer's octave values: the mean deviation of the bands within half an
    // octave, weighted by confidence; a band the gate counts as missing shows a dash.
    // Rounded to 0.5 dB so the numbers do not flicker.
    for (int i = 0; i < numOctaves; ++i)
    {
        float sum = 0.0f, weight = 0.0f;
        int count = 0;
        for (int b = 0; b < bands; ++b)
            if (std::abs (std::log2 (hz[(size_t) b] / octaveHz[(size_t) i])) <= 0.5f)
            {
                sum += confidence[(size_t) b] * deviation[(size_t) b];
                weight += confidence[(size_t) b];
                ++count;
            }

        octaveValid[(size_t) i] = count > 0 && weight / (float) count >= 0.5f;
        octaveDb[(size_t) i] = octaveValid[(size_t) i] ? std::round (2.0f * sum / weight) / 2.0f : 0.0f;
    }

    if (analyzing)
        moved = true;   // the deviation is the whole picture now

    // The faint input trace moves all the time; the curve only repaints the
    // panel when something visible changed, or every few frames for the trace.
    if (moved || ++framesSinceRepaint >= 4)
    {
        framesSinceRepaint = 0;
        repaint();
    }
}

/** A smooth path through the band points: a light 1-2-1 smoothing across
    neighbouring bands, then Catmull-Rom between them, sampled every 2 pixels.
    The ERB bands are evenly spaced on the display's log axis to within a few
    per cent, so the spline needs no parameter correction. */
juce::Path CurveDisplay::smoothPath (const float* values, bool smoothAcross, float extendLeft, float extendRight,
                                     std::optional<float> leftEndY) const
{
    std::array<float, ClearEngine::maxBands> v {}, x {};

    for (int b = 0; b < bands; ++b)
    {
        const auto prev = values[juce::jmax (0, b - 1)], next = values[juce::jmin (bands - 1, b + 1)];
        v[(size_t) b] = smoothAcross ? 0.25f * prev + 0.5f * values[b] + 0.25f * next : values[b];
        x[(size_t) b] = xFor (hz[(size_t) b]);
    }

    juce::Path path;
    path.startNewSubPath (extendLeft, leftEndY.value_or (v[0]));
    path.lineTo (x[0], v[0]);

    for (int b = 0; b + 1 < bands; ++b)
    {
        const auto p0 = v[(size_t) juce::jmax (0, b - 1)], p1 = v[(size_t) b], p2 = v[(size_t) (b + 1)],
                   p3 = v[(size_t) juce::jmin (bands - 1, b + 2)];
        const auto x1 = x[(size_t) b], x2 = x[(size_t) (b + 1)];
        const auto steps = juce::jmax (2, juce::roundToInt ((x2 - x1) / 2.0f));

        for (int s = 1; s <= steps; ++s)
        {
            const auto t = (float) s / (float) steps, t2 = t * t, t3 = t2 * t;
            const auto y = 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                                   + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
            path.lineTo (x1 + (x2 - x1) * t, y);
        }
    }

    path.lineTo (extendRight, v[(size_t) (bands - 1)]);
    return path;
}

float CurveDisplay::maxLiftDb() const noexcept
{
    float m = 0.0f;
    for (auto v : totalDb)
        m = juce::jmax (m, v);
    return m;
}

float CurveDisplay::maxCutDb() const noexcept
{
    float m = 0.0f;
    for (auto v : totalDb)
        m = juce::jmin (m, v);
    return m;
}

/** The dynamic curve at t (0 = 20 Hz, 1 = 20 kHz), as smoothPath draws it: the
    1-2-1 smoothed band values joined by Catmull-Rom; below the lowest band the
    engine's own continuation; above the top band flat. */
/** The resonance stage's cut at hz, from its own log grid. */
float CurveDisplay::resonanceDbAt (float frequency) const noexcept
{
    const auto& r = processor.getEngine().getResonance();
    const auto pos = juce::jlimit (0.0f, (float) (ResonanceStage::gridPoints - 1),
                                   std::log (frequency / 20.0f) / std::log (1000.0f) * (float) (ResonanceStage::gridPoints - 1));
    const auto i = juce::jmin ((int) pos, ResonanceStage::gridPoints - 2);
    const auto f = pos - (float) i;
    return r.getDisplayDb (i) + f * (r.getDisplayDb (i + 1) - r.getDisplayDb (i));
}

float CurveDisplay::dynamicDbAt (float t) const noexcept
{
    return splineAt (gain.data(), t, true);
}

/** Band values at t as the panel draws them: 1-2-1 smoothed, joined by Catmull-Rom.
    Below the lowest band either the engine's own continuation (`carryOnBelow`) or
    the lowest band's value; above the top band flat. */
float CurveDisplay::splineAt (const float* values, float t, bool carryOnBelow) const noexcept
{
    const auto tFor = [] (float f) { return std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f); };
    const auto v = [&] (int b)
    {
        b = juce::jlimit (0, bands - 1, b);
        return 0.25f * values[juce::jmax (0, b - 1)] + 0.5f * values[b] + 0.25f * values[juce::jmin (bands - 1, b + 1)];
    };

    const auto t0 = tFor (hz[0]);
    if (t <= t0)
    {
        const auto z0 = ClearEngine::erbNumber (hz[0]), z1 = ClearEngine::erbNumber (hz[1]);
        const auto bandsBelow = (ClearEngine::erbNumber (20.0f) - z0) / (z1 - z0);
        const auto at20 = ClearEngine::lowEndDb (v (0), v (1), bandsBelow);
        if (! carryOnBelow || t0 <= 0.0f)
            return v (0);
        return at20 + (v (0) - at20) * t / t0;
    }

    for (int b = 0; b + 1 < bands; ++b)
    {
        const auto ta = tFor (hz[(size_t) b]), tb = tFor (hz[(size_t) (b + 1)]);
        if (t <= tb)
        {
            const auto u = (t - ta) / juce::jmax (1.0e-6f, tb - ta), u2 = u * u, u3 = u2 * u;
            const auto p0 = v (b - 1), p1 = v (b), p2 = v (b + 1), p3 = v (b + 2);
            return 0.5f * (2.0f * p1 + (p2 - p0) * u + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2
                           + (3.0f * p1 - p0 - 3.0f * p2 + p3) * u3);
        }
    }

    return v (bands - 1);
}

namespace
{
    constexpr float infoWidth = 170.0f, infoGap = 14.0f;
    constexpr float tickRow = 18.0f, octaveRow = 26.0f;
    constexpr std::array<float, 10> tickHz { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };

    juce::String hzText (float f)
    {
        return f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f));
    }

    /** "+2.8 dB", "-5.1 dB" with a real minus, "0.0 dB". */
    juce::String signedDb (float db)
    {
        const auto rounded = std::round (db * 10.0f) / 10.0f;
        const auto sign = rounded > 0.0f ? juce::String ("+") : rounded < 0.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))
                                                                               : juce::String();
        return sign + juce::String (std::abs (rounded), 1) + " dB";
    }

    float tFor (float f)
    {
        return std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f);
    }
}

juce::Rectangle<float> CurveDisplay::wellArea() const noexcept
{
    return getLocalBounds().toFloat().reduced (16.0f, 14.0f).withTrimmedLeft (infoWidth + infoGap);
}

juce::Rectangle<float> CurveDisplay::plotArea() const noexcept
{
    // The well, less its frequency row, and in Analyze the octave values.
    return wellArea().reduced (1.0f).withTrimmedBottom (tickRow + (analyzing ? octaveRow : 0.0f));
}

float CurveDisplay::xFor (float f) const noexcept
{
    const auto area = plotArea();
    return area.getX() + tFor (f) * area.getWidth();
}

float CurveDisplay::yFor (float db) const noexcept
{
    // As the mockup: +-12 dB fill 88 % of the height.
    const auto area = plotArea();
    return area.getCentreY() - juce::jlimit (-rangeDb, rangeDb, db) / rangeDb * area.getHeight() * 0.44f;
}

juce::Path CurveDisplay::curvePath (const float* dbOnGrid) const
{
    const auto area = plotArea();
    juce::Path path;

    for (int i = 0; i < gridPoints; ++i)
    {
        const auto x = area.getX() + area.getWidth() * (float) i / (float) (gridPoints - 1);
        const auto y = yFor (dbOnGrid[i]);
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }

    return path;
}

std::array<float, CurveDisplay::gridPoints> CurveDisplay::deviationOnGrid() const
{
    // Below the lowest band (~47 Hz) there is no measurement of its own: the curve is
    // carried on to 20 Hz with the slope of the two lowest bands (at most 12 dB).
    std::array<float, gridPoints> out {};
    const auto t0 = tFor (hz[0]);
    const auto smoothed = [&] (int b) { return 0.25f * deviation[(size_t) juce::jmax (0, b - 1)] + 0.5f * deviation[(size_t) b]
                                               + 0.25f * deviation[(size_t) juce::jmin (bands - 1, b + 1)]; };
    const auto z0 = ClearEngine::erbNumber (hz[0]), z1 = ClearEngine::erbNumber (hz[1]);
    const auto bandsBelow = (ClearEngine::erbNumber (20.0f) - z0) / (z1 - z0);
    const auto at20Hz = smoothed (0) + juce::jlimit (-12.0f, 12.0f, bandsBelow * (smoothed (1) - smoothed (0)));

    for (int i = 0; i < gridPoints; ++i)
    {
        const auto t = (float) i / (float) (gridPoints - 1);
        out[(size_t) i] = t < t0 && t0 > 0.0f ? at20Hz + (smoothed (0) - at20Hz) * t / t0
                                              : splineAt (deviation.data(), t, false);
    }

    return out;
}

float CurveDisplay::totalAt (float frequency) const noexcept
{
    const auto pos = tFor (frequency) * (float) (gridPoints - 1);
    const auto i = juce::jlimit (0, gridPoints - 2, (int) pos);
    const auto f = pos - (float) i;
    return totalDb[(size_t) i] + f * (totalDb[(size_t) (i + 1)] - totalDb[(size_t) i]);
}

void CurveDisplay::paintScale (juce::Graphics& g, juce::Rectangle<float> well)
{
    const auto area = plotArea();

    // The grid: the frequencies the row names, and every 6 dB.
    g.setColour (Palette::plotGrid);
    for (const auto f : tickHz)
        g.fillRect (juce::Rectangle<float> (xFor (f), area.getY(), 1.0f, area.getHeight()));

    for (const auto db : { 12.0f, 6.0f, -6.0f, -12.0f })
        g.fillRect (juce::Rectangle<float> (area.getX(), yFor (db), area.getWidth(), 1.0f));

    g.setColour (Palette::plotZero);
    g.fillRect (juce::Rectangle<float> (area.getX(), yFor (0.0f), area.getWidth(), 1.0f));

    g.setFont (Palette::font (10.0f));
    g.setColour (Palette::plotText);
    for (const auto db : { 6, 0, -6 })
        g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                    juce::Rectangle<float> (area.getX() + 6.0f, yFor ((float) db) - 7.0f, 24.0f, 14.0f), juce::Justification::centredLeft);

    // The frequency row, under a hairline.
    auto row = well.reduced (1.0f).removeFromBottom (tickRow);
    g.setColour (Palette::waveLine);
    g.fillRect (row.removeFromTop (1.0f));

    g.setColour (Palette::plotText);
    for (size_t i = 0; i < tickHz.size(); ++i)
    {
        const auto x = xFor (tickHz[i]);
        const auto text = hzText (tickHz[i]);
        const auto box = i == 0 ? juce::Rectangle<float> (x + 4.0f, row.getY(), 40.0f, row.getHeight())
                       : i + 1 == tickHz.size() ? juce::Rectangle<float> (x - 44.0f, row.getY(), 40.0f, row.getHeight())
                                                : juce::Rectangle<float> (x - 20.0f, row.getY(), 40.0f, row.getHeight());
        g.drawText (text, box, i == 0 ? juce::Justification::centredLeft
                             : i + 1 == tickHz.size() ? juce::Justification::centredRight : juce::Justification::centred);
    }
}

void CurveDisplay::paintInfo (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto lift = Palette::liftColour(), cut = Palette::cutColour();

    g.setColour (Palette::inkSoft);
    g.setFont (Palette::font (12.0f, true, 0.08f));
    g.drawText (analyzing ? "ANALYZER" : bypassed ? "BYPASSED" : "APPLIED NOW", area.removeFromTop (15.0f), juce::Justification::centredLeft);
    area.removeFromTop (8.0f);

    const auto big = Palette::font (24.0f, true);
    g.setFont (big);

    if (analyzing)
    {
        g.setColour (Palette::ink);
        g.drawText ("Signal", area.removeFromTop (29.0f), juce::Justification::centredLeft);
        g.setColour (Palette::inkFaint);
        g.drawText ("untouched", area.removeFromTop (29.0f), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (bypassed ? Palette::plotText : lift);
        g.drawText (signedDb (juce::jmax (0.0f, maxLiftDb())), area.removeFromTop (29.0f), juce::Justification::centredLeft);
        g.setColour (bypassed ? Palette::plotText : cut);
        g.drawText (signedDb (juce::jmin (0.0f, maxCutDb())), area.removeFromTop (29.0f), juce::Justification::centredLeft);
    }

    area.removeFromTop (6.0f);
    g.setColour (Palette::inkFaint);
    g.setFont (Palette::font (11.0f));
    g.drawFittedText (analyzing ? "Above the line: too loud. Below: too quiet." : "Max lift / max cut",
                      area.removeFromTop (28.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.9f);

    // The legend, at the bottom.
    struct Item { const char* text; juce::Colour colour; float height; float alpha; };
    const std::vector<Item> legend = analyzing
        ? std::vector<Item> { { "DEVIATION", Palette::accent, 2.0f, 1.0f }, { "TARGET", Palette::inkFaint, 1.0f, 1.0f } }
        : std::vector<Item> { { "LIFT", lift, 8.0f, 0.8f }, { "CUT", cut, 8.0f, 0.8f }, { "INPUT", Palette::inkFaint, 1.0f, 1.0f } };

    auto rows = area.removeFromBottom ((float) legend.size() * 14.0f + (float) (legend.size() - 1) * 5.0f);
    g.setFont (Palette::font (10.5f, true, 0.06f));

    for (const auto& item : legend)
    {
        auto row = rows.removeFromTop (14.0f);
        rows.removeFromTop (5.0f);
        g.setColour (item.colour.withAlpha (item.alpha));
        g.fillRect (row.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, item.height));
        row.removeFromLeft (7.0f);
        g.setColour (Palette::inkSoft);
        g.drawText (item.text, row, juce::Justification::centredLeft);
    }
}

void CurveDisplay::paint (juce::Graphics& g)
{
    // The card.
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Palette::body);
    g.fillRect (bounds);
    g.setColour (Palette::card);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    paintInfo (g, bounds.reduced (16.0f, 14.0f).withWidth (infoWidth));

    // The well.
    const auto well = wellArea();
    g.setColour (Palette::wave);
    g.fillRoundedRectangle (well, 4.0f);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (well.reduced (1.0f).toNearestInt());
        paintScale (g, well);

        if (bands >= 2)
        {
            if (analyzing)
                paintAnalyzer (g, well);
            else
            {
                const auto area = plotArea();
                const auto zero = yFor (0.0f);

                // The input's distance from the target, faint, behind.
                const auto input = deviationOnGrid();
                g.setColour (Palette::inkFaint.withAlpha (0.35f));
                g.strokePath (curvePath (input.data()), juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // The curve - dynamic, resonance and EQ - filled to 0 dB: orange above, blue below.
                const auto curve = curvePath (totalDb.data());
                juce::Path filled (curve);
                filled.lineTo (area.getRight(), zero);
                filled.lineTo (area.getX(), zero);
                filled.closeSubPath();

                const auto fill = bypassed ? 0.08f : 0.28f;
                {
                    juce::Graphics::ScopedSaveState s1 (g);
                    g.reduceClipRegion (area.withBottom (zero).toNearestInt());
                    g.setColour (Palette::liftColour().withAlpha (fill));
                    g.fillPath (filled);
                }
                {
                    juce::Graphics::ScopedSaveState s2 (g);
                    g.reduceClipRegion (area.withTop (zero).toNearestInt());
                    g.setColour (Palette::cutColour().withAlpha (fill));
                    g.fillPath (filled);
                }

                g.setColour (bypassed ? Palette::plotText.withAlpha (0.6f) : Palette::ink);
                g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // The EQ's bells, where they sit on the curve: L, M, H.
                const auto eq = processor.getEqSettings();
                const auto colour = Palette::zone (Palette::ZoneId::eq).line;
                const char* letters[] { "L", "M", "H" };

                for (size_t b = 0; b < eq.hz.size(); ++b)
                {
                    const auto centre = juce::Point<float> (xFor (eq.hz[b]), yFor (totalAt (eq.hz[b])));
                    const auto alpha = std::abs (eq.gainDb[b]) < 0.05f ? 0.45f : 1.0f;
                    const auto circle = juce::Rectangle<float> (18.0f, 18.0f).withCentre (centre);
                    g.setColour (Palette::wave.withAlpha (alpha));
                    g.fillEllipse (circle);
                    g.setColour (colour.withAlpha (alpha));
                    g.drawEllipse (circle.reduced (0.75f), 1.5f);
                    g.setFont (Palette::font (9.0f, true));
                    g.drawText (letters[b], circle, juce::Justification::centred);
                }
            }
        }

        // The header: what is shown, and its state.
        const auto header = plotArea().withTrimmedLeft (34.0f).withTrimmedRight (10.0f).withTrimmedTop (6.0f).withHeight (13.0f);
        g.setFont (Palette::font (10.5f, true, 0.08f));
        g.setColour (Palette::inkFaint);
        g.drawText (analyzing ? "INPUT VS. TARGET 4.5 DB/OCT" : "TARGET 4.5 DB/OCT", header, juce::Justification::centredLeft);

        const auto status = analyzing ? (holding ? juce::String ("HOLD") : juce::String ("ANALYZE"))
                          : bypassed  ? juce::String ("BYPASSED")
                          : holding   ? juce::String ("HOLD")
                                      : signedDb (juce::jmax (0.0f, maxLiftDb())).replace (" dB", "") + " / "
                                            + signedDb (juce::jmin (0.0f, maxCutDb()));
        // On its own backing: a large lift or deviation runs right up into it.
        const auto statusFont = Palette::font (10.5f, true, 0.08f);
        const auto statusWidth = juce::GlyphArrangement::getStringWidth (statusFont, status) + 10.0f;
        g.setColour (Palette::wave.withAlpha (0.85f));
        g.fillRoundedRectangle (header.withLeft (header.getRight() - statusWidth).expanded (5.0f, 2.0f).withTrimmedRight (5.0f), 3.0f);

        g.setColour (analyzing || holding ? Palette::accent : bypassed ? Palette::inkFaint : Palette::knobName);
        g.drawText (status, header, juce::Justification::centredRight);
    }

    g.setColour (Palette::waveLine);
    g.drawRoundedRectangle (well.reduced (0.5f), 4.0f, 1.0f);
}

/** The analyzer: the input's deviation from the target curve - above the line too
    loud, below too quiet - and one value per octave to set an external EQ by. */
void CurveDisplay::paintAnalyzer (juce::Graphics& g, juce::Rectangle<float> well)
{
    const auto input = deviationOnGrid();
    g.setColour (Palette::accent);
    g.strokePath (curvePath (input.data()), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // One value per octave, in a row of chips over the frequencies.
    auto row = well.reduced (1.0f).withTrimmedBottom (tickRow).removeFromBottom (octaveRow).reduced (8.0f, 0.0f).withTrimmedBottom (4.0f);
    const auto width = (row.getWidth() - 4.0f * (float) (numOctaves - 1)) / (float) numOctaves;
    const auto font = Palette::font (10.5f, true);
    g.setFont (font);

    for (int i = 0; i < numOctaves; ++i)
    {
        const auto chip = juce::Rectangle<float> (row.getX() + (float) i * (width + 4.0f), row.getY(), width, row.getHeight());
        g.setColour (Palette::chip);
        g.fillRoundedRectangle (chip, 3.0f);
        g.setColour (Palette::chipLine);
        g.drawRoundedRectangle (chip.reduced (0.5f), 3.0f, 1.0f);

        const auto f = octaveHz[(size_t) i];
        const auto name = f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f));
        juce::String value = juce::CharPointer_UTF8 ("\xe2\x80\x94");
        auto colour = Palette::plotText;

        if (octaveValid[(size_t) i])
        {
            const auto v = octaveDb[(size_t) i];
            value = (v > 0.0f ? "+" : v < 0.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String())
                  + juce::String (std::abs (v), 1);
            colour = v > 0.0f ? Palette::liftColour() : v < 0.0f ? Palette::cutColour() : Palette::inkFaint;
        }

        const auto nameWidth = juce::GlyphArrangement::getStringWidth (font, name);
        const auto valueWidth = juce::GlyphArrangement::getStringWidth (font, value);
        auto text = chip.withSizeKeepingCentre (nameWidth + 5.0f + valueWidth + 2.0f, chip.getHeight());
        g.setColour (Palette::plotText);
        g.drawText (name, text.removeFromLeft (nameWidth + 1.0f), juce::Justification::centredLeft);
        text.removeFromLeft (4.0f);
        g.setColour (colour);
        g.drawText (value, text, juce::Justification::centredLeft);
    }
}
