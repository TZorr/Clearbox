//
//  PluginEditor.cpp
//  Clearbox
//

#include "PluginEditor.h"

#include "TextUtf8.h"
#include "UI/Palette.h"

namespace
{
    /** The panel's theme and the window's size are the user's, not the
        session's: they live in ~/Library/Application Support/Clearbox/
        Clearbox.settings, shared by every instance and every project, and
        never travel with a session. */
    std::unique_ptr<juce::PropertiesFile> openSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = JucePlugin_Name;
        options.filenameSuffix      = ".settings";
        options.folderName          = JucePlugin_Name;
        options.osxLibrarySubFolder = "Application Support";
        options.getDefaultFile().getParentDirectory().createDirectory();
        return std::make_unique<juce::PropertiesFile> (options);
    }

    constexpr const char* darkModeKey = "darkMode";
    constexpr const char* scaleKey    = "panelScale";

    // The mockup's measures.
    constexpr int margin = 12;
    constexpr int headerHeight = 65;      // 64 and its hairline
    constexpr int footerHeight = 37;      // 36 and its hairline
    constexpr int displayHeight = 244;
    constexpr int columnWidth = 130;
    constexpr int rackHeight = 1 + 112 + 1;
    constexpr int titleColumn = 6 + 22;   // a section's upright title and the room before it
    constexpr int headRow = 22;           // a section's hint, or the EQ's band names

    const juce::String moonIcon = "M13.2 9.6A5.6 5.6 0 1 1 6.4 2.8a4.5 4.5 0 0 0 6.8 6.8z";
    const juce::String sunIcon  = "M11 8a3 3 0 1 1-6 0a3 3 0 1 1 6 0M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6"
                                  "M3.4 3.4l1.1 1.1M11.5 11.5l1.1 1.1M3.4 12.6l1.1-1.1M11.5 4.5l1.1-1.1";

    /** The meters' scale: -48 dB at the bottom, +3 at the top. */
    float meterFraction (float gain)
    {
        const auto db = juce::Decibels::gainToDecibels (gain, -100.0f);
        return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -48.0f, 3.0f, 0.0f, 1.0f));
    }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }

    /** "Low · Q 0.71", or "Low · off" while the band is flat. */
    juce::String bandHead (const char* name, float gainDb)
    {
        return juce::String (name) + utf8 (" \xc2\xb7 ")
             + (std::abs (gainDb) < 0.05f ? juce::String ("off") : "Q " + juce::String (SoftEq::qForGain (gainDb), 2));
    }
}

ClearboxEditor::ClearboxEditor (ClearboxProcessor& p)
    : AudioProcessorEditor (&p), clearbox (p), display (p), presetButton (p), settings (openSettings())
{
    addAndMakeVisible (canvas);

    lift.attach (p.parameters, ClearParams::lift);
    trim.attach (p.parameters, ClearParams::trim);
    bias.attach (p.parameters, ClearParams::bias);
    brighten.attach (p.parameters, ClearParams::brighten);
    resonance.attach (p.parameters, ClearParams::resonance);
    sensitivity.attach (p.parameters, ClearParams::resSens);
    lowFreq.attach (p.parameters, ClearParams::eq1Freq);
    lowGain.attach (p.parameters, ClearParams::eq1Gain);
    midFreq.attach (p.parameters, ClearParams::eq3Freq);
    midGain.attach (p.parameters, ClearParams::eq3Gain);
    highFreq.attach (p.parameters, ClearParams::eq2Freq);
    highGain.attach (p.parameters, ClearParams::eq2Gain);
    boost.attach (p.parameters, ClearParams::boost);
    gain.attach (p.parameters, ClearParams::gain);

    lift.setTooltip ("Raises what lies below the 4.5 dB/oct target curve - 100 % all the way up to it");
    trim.setTooltip ("Takes off what lies above the curve - 100 % all the way down to it");
    bias.setTooltip ("Above 0: a dead zone - only larger departures from the curve are treated");
    brighten.setTooltip ("Tilts the target curve, darker or brighter, by up to 1.5 dB/oct");
    resonance.setTooltip ("Tames ringing peaks: narrow resonances standing out of their neighbourhood are cut quickly - "
                          "100 % brings them down to the threshold");
    sensitivity.setTooltip ("How far a peak must stand out to count: 0 % = 12 dB, 100 % = 3 dB");
    lowFreq.setTooltip ("Low bell: frequency");
    midFreq.setTooltip ("Mid bell: frequency");
    highFreq.setTooltip ("High bell: frequency");
    for (auto* k : { &lowGain, &midGain, &highGain })
        k->setTooltip ("A bell after the dynamic EQ: the more gain, the narrower");
    boost.setTooltip ("The balance as heard this many dB quieter (+) or louder (-), at the same loudness");
    gain.setTooltip ("Output level");

    using Z = Palette::ZoneId;
    sections[0] = { "Shape", "Pulls towards the curve over ~2 s", Z::shape, { &lift, &trim, &bias, &brighten }, {} };
    sections[1] = { "Resonance", "Tames ringing peaks", Z::resonance, { &resonance, &sensitivity }, {} };
    sections[2] = { "EQ", {}, Z::eq, { &lowFreq, &lowGain, &midFreq, &midGain, &highFreq, &highGain }, {} };
    sections[3] = { "Output", utf8 ("Loudness contour \xc2\xb7 level"), Z::neutral, { &boost, &gain }, {} };

    for (auto& section : sections)
        for (auto* knob : section.knobs)
        {
            knob->setZone (section.zone);
            canvas.addAndMakeVisible (knob);
        }

    // Lift and Trim in the curve's own colours: what they make, they wear.
    lift.setZone (Z::lift);
    trim.setZone (Z::cut);

    // Reset is a push button: it never stays lit. Analyze outlines while on;
    // Bypass, which changes what is heard, fills.
    reset.setTooltip ("Every knob back to its starting position (Bypass stays as it is)");
    reset.onClick = [&p] { p.resetKnobs(); };
    analyze.setClickingTogglesState (true);
    bypass.setClickingTogglesState (true);
    bypass.fillWhenOn = true;
    analyze.setTooltip ("Analyzer: the knobs do nothing and the sound passes untouched; the display shows how far "
                        "the input is from the 4.5 dB/oct curve, per octave - to match it with an external EQ");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, ClearParams::bypass, bypass);
    analyzeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, ClearParams::analyze, analyze);

    for (auto* button : { &analyze, &reset, &bypass })
        canvas.addAndMakeVisible (button);

    // The saved theme first, so the first paint is already the right one.
    // Dark is the mockup's, and so the default.
    Palette::setDark (settings->getBoolValue (darkModeKey, true));
    shownDark = Palette::dark;
    themeButton.onClick = [this] { setDarkMode (! Palette::dark); };
    refreshThemeIcon();
    canvas.addAndMakeVisible (themeButton);

    prevPresetButton.setTooltip ("Previous preset");
    nextPresetButton.setTooltip ("Next preset");
    loadPresetButton.setTooltip ("Load a preset file");
    savePresetButton.setTooltip ("Save the setting as a preset");
    prevPresetButton.onClick = [this] { presetButton.step (-1); };
    nextPresetButton.onClick = [this] { presetButton.step (+1); };
    loadPresetButton.onClick = [this] { presetButton.choose (false); };
    savePresetButton.onClick = [this] { presetButton.choose (true); };

    for (auto* button : { (juce::Button*) &prevPresetButton, (juce::Button*) &nextPresetButton, (juce::Button*) &presetButton,
                          (juce::Button*) &loadPresetButton, (juce::Button*) &savePresetButton })
        canvas.addAndMakeVisible (button);

    canvas.addAndMakeVisible (display);
    showAnalyzing (p.isAnalyzing());

    canvas.setSize (panelWidth, panelHeight);
    layoutCanvas();

    // Any size at the panel's proportions; 85 % the first time.
    const auto scale = juce::jlimit (minScale, maxScale, settings->getDoubleValue (scaleKey, defaultScale));
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (panelWidth * minScale), juce::roundToInt (panelHeight * minScale),
                     juce::roundToInt (panelWidth * maxScale), juce::roundToInt (panelHeight * maxScale));
    getConstrainer()->setFixedAspectRatio ((double) panelWidth / (double) panelHeight);
    setSize (juce::roundToInt (panelWidth * scale), juce::roundToInt (panelHeight * scale));

    startTimerHz (60);
}

ClearboxEditor::~ClearboxEditor()
{
    stopTimer();
    bypassAttachment.reset();
    analyzeAttachment.reset();
}

void ClearboxEditor::setDarkMode (bool shouldBeDark)
{
    Palette::setDark (shouldBeDark);
    settings->setValue (darkModeKey, shouldBeDark);
    settings->saveIfNeeded();

    shownDark = shouldBeDark;
    refreshThemeIcon();
    canvas.repaint();
}

void ClearboxEditor::refreshThemeIcon()
{
    themeButton.setIcon (Palette::dark ? moonIcon : sunIcon);
    themeButton.setTooltip (Palette::dark ? "Switch to the light panel" : "Switch to the dark panel");
}

void ClearboxEditor::showAnalyzing (bool on)
{
    shownAnalyzing = on;

    // The knobs do nothing while analyzing: the rack fades back, and Reset with it.
    for (auto& section : sections)
        for (auto* knob : section.knobs)
        {
            knob->setEnabled (! on);
            knob->setAlpha (on ? 0.38f : 1.0f);
        }

    reset.setEnabled (! on);
    reset.setAlpha (on ? 0.38f : 1.0f);
    canvas.repaint (rackArea);
}

juce::String ClearboxEditor::latencyText() const
{
    const auto rate = clearbox.getSampleRate() > 0 ? clearbox.getSampleRate() : 48000.0;
    return "Latency " + juce::String (juce::roundToInt (1000.0 * clearbox.getLatencySamples() / rate)) + " ms";
}

void ClearboxEditor::timerCallback()
{
    display.refresh();

    // Followed here rather than on the click, so host automation is followed too.
    if (const auto on = clearbox.isAnalyzing(); on != shownAnalyzing)
        showAnalyzing (on);

    // Another Clearbox window may have switched the theme: it is process-wide.
    if (shownDark != Palette::dark)
    {
        shownDark = Palette::dark;
        refreshThemeIcon();
        canvas.repaint();
    }

    // The meters: peaks held since the last tick, falling back at about 20 dB a second.
    {
        bool moved = false;
        for (int c = 0; c < 4; ++c)
        {
            const auto peak = clearbox.takeMeterPeak (c);
            const auto next = juce::jmax (peak, meterLevels[(size_t) c] * 0.93f);
            const auto shown = next < 1.0e-4f ? 0.0f : next;
            moved = moved || std::abs (meterFraction (shown) - meterFraction (meterLevels[(size_t) c])) > 0.002f;
            meterLevels[(size_t) c] = shown;
        }
        if (moved)
            canvas.repaint (metersArea);
    }

    // The latency, in the header and the status bar; the EQ's band names, with their Q.
    {
        const auto eq = clearbox.getEqSettings();
        const auto heads = bandHead ("Low", eq.gainDb[0]) + "|" + bandHead ("Mid", eq.gainDb[1]) + "|" + bandHead ("High", eq.gainDb[2]);
        const auto latency = latencyText();

        if (heads != shownEqHeads || latency != shownLatency)
        {
            shownEqHeads = heads;
            shownLatency = latency;
            canvas.repaint();
        }
    }
}

void ClearboxEditor::paint (juce::Graphics& g)
{
    // Only where the scaled panel leaves a sliver at the edge.
    g.fillAll (Palette::body);
}

void ClearboxEditor::resized()
{
    const auto scale = (float) getWidth() / (float) panelWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));

    // Remembered once the window is up: the constructor's own setSize is the
    // saved size, not a choice.
    if (isTimerRunning() && std::abs (settings->getDoubleValue (scaleKey, defaultScale) - scale) > 0.001)
        settings->setValue (scaleKey, (double) scale);
}

void ClearboxEditor::layoutCanvas()
{
    auto area = juce::Rectangle<int> (0, 0, panelWidth, panelHeight);

    // Header: from the left, the wordmark, the preset, Load / Save; from the
    // right, the theme, the meters and what they show.
    headerArea = area.removeFromTop (headerHeight);
    {
        auto row = headerArea.withTrimmedBottom (1).reduced (margin, 0);
        const auto buttonRow = [] (juce::Rectangle<int> r) { return r.withSizeKeepingCentre (r.getWidth(), 36); };

        wordmarkArea = row.removeFromLeft (textWidth (Palette::font (22.0f, true, 0.16f), "CLEARBOX") + 14
                                           + textWidth (Palette::label (12.0f), "TARGET EQ"));
        row.removeFromLeft (20);
        headerDividers[0] = row.removeFromLeft (1).withSizeKeepingCentre (1, 28);
        row.removeFromLeft (20);

        prevPresetButton.setBounds (buttonRow (row.removeFromLeft (36)));
        row.removeFromLeft (6);
        nextPresetButton.setBounds (buttonRow (row.removeFromLeft (36)));
        row.removeFromLeft (6);
        presetButton.setBounds (buttonRow (row.removeFromLeft (280)));
        row.removeFromLeft (20);

        for (auto* button : { &loadPresetButton, &savePresetButton })
        {
            const auto width = 14 + 16 + 8 + textWidth (Palette::label (12.0f), button->getButtonText().toUpperCase()) + 14;
            button->setBounds (buttonRow (row.removeFromLeft (width)));
            row.removeFromLeft (6);
        }

        themeButton.setBounds (buttonRow (row.removeFromRight (36)));
        row.removeFromRight (20);
        headerDividers[1] = row.removeFromRight (1).withSizeKeepingCentre (1, 28);
        row.removeFromRight (20);

        // In L, In R, a gap, Out L, Out R.
        metersArea = row.removeFromRight (5 + 3 + 5 + 3 + 6 + 3 + 5 + 3 + 5).withSizeKeepingCentre (5 + 3 + 5 + 3 + 6 + 3 + 5 + 3 + 5, 34);
        row.removeFromRight (10);
        ioArea = row.removeFromRight (110).withSizeKeepingCentre (110, 32);
    }

    footerArea = area.removeFromBottom (footerHeight);

    auto body = area.reduced (margin, 0);
    body.removeFromTop (8);

    // The display and the column of mode buttons.
    {
        auto top = body.removeFromTop (displayHeight);
        auto column = top.removeFromRight (columnWidth);
        top.removeFromRight (6);
        display.setBounds (top);

        const auto buttonHeight = (column.getHeight() - 2 * 6) / 3;
        analyze.setBounds (column.removeFromTop (buttonHeight));
        column.removeFromTop (6);
        reset.setBounds (column.removeFromTop (buttonHeight));
        column.removeFromTop (6);
        bypass.setBounds (column);
    }

    body.removeFromTop (6);

    // The rack: one row, each section as wide as its knobs are many, a
    // 1-point upright between them.
    rackArea = body.removeFromTop (rackHeight);
    {
        const auto row = rackArea.reduced (1);
        int columnsTotal = 0;
        for (const auto& section : sections)
            columnsTotal += (int) section.knobs.size();

        const auto count = (int) sections.size();
        const auto available = (float) (row.getWidth() - (count - 1));
        int before = 0;

        for (int i = 0; i < count; ++i)
        {
            auto& section = sections[(size_t) i];
            const auto columns = (int) section.knobs.size();
            const auto after = before + columns;
            const auto left  = row.getX() + i + juce::roundToInt (available * (float) before / (float) columnsTotal);
            const auto right = i + 1 == count ? row.getRight()
                                              : row.getX() + i + juce::roundToInt (available * (float) after / (float) columnsTotal);
            section.area = { left, row.getY(), right - left, row.getHeight() };
            before = after;

            auto inner = section.area.withTrimmedLeft (titleColumn).reduced (6, 0);
            inner = inner.withSizeKeepingCentre (inner.getWidth(), headRow + 4 + Knob::stackedHeight);
            inner.removeFromTop (headRow + 4);

            const auto cell = (float) inner.getWidth() / (float) columns;
            for (int c = 0; c < columns; ++c)
            {
                const auto x = inner.getX() + juce::roundToInt (cell * (float) c);
                const auto w = inner.getX() + juce::roundToInt (cell * (float) (c + 1)) - x;
                section.knobs[(size_t) c]->setBounds (x, inner.getY(), w, Knob::stackedHeight);
            }
        }
    }
}

void ClearboxEditor::paintCanvas (juce::Graphics& g)
{
    g.fillAll (Palette::body);

    // Header.
    g.setColour (Palette::header);
    g.fillRect (headerArea);
    g.setColour (Palette::line);
    g.fillRect (headerArea.withTop (headerArea.getBottom() - 1));

    {
        const auto font = Palette::font (22.0f, true, 0.16f);
        const auto clearWidth = textWidth (font, "CLEAR");
        const auto boxWidth = textWidth (font, "CLEARBOX");
        g.setFont (font);
        g.setColour (Palette::accent);
        g.drawText ("CLEAR", wordmarkArea, juce::Justification::centredLeft);
        g.setColour (Palette::title);
        g.drawText ("BOX", wordmarkArea.withTrimmedLeft (clearWidth), juce::Justification::centredLeft);

        g.setColour (Palette::inkFaint);
        g.setFont (Palette::label (12.0f));
        g.drawText ("TARGET EQ", wordmarkArea.withTrimmedLeft (boxWidth + 14).translated (0, 2), juce::Justification::centredLeft);
    }

    g.setColour (Palette::divider);
    for (const auto& divider : headerDividers)
        g.fillRect (divider);

    // IN / OUT and the latency, right-aligned before the meters.
    {
        auto text = ioArea;
        g.setColour (Palette::inkDim);
        g.setFont (Palette::label (11.0f));
        g.drawText ("IN / OUT", text.removeFromTop (15), juce::Justification::bottomRight);
        text.removeFromTop (2);
        g.setColour (Palette::ink);
        g.setFont (Palette::font (12.0f));
        g.drawText (shownLatency, text, juce::Justification::topRight);
    }

    // The meters: in, then out, each left and right.
    for (int c = 0; c < 4; ++c)
    {
        const auto x = (float) metersArea.getX() + (float) c * 8.0f + (c >= 2 ? 6.0f : 0.0f);
        const auto bar = juce::Rectangle<float> (x, (float) metersArea.getY(), 5.0f, (float) metersArea.getHeight());
        g.setColour (Palette::meterTrack);
        g.fillRoundedRectangle (bar, 2.0f);

        const auto peak = meterLevels[(size_t) c];
        const auto fraction = meterFraction (peak);
        if (fraction > 0.0f)
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (bar, 2.0f);
            g.reduceClipRegion (clip);
            g.setColour (peak >= 1.0f ? Palette::warning : peak >= 0.7f ? Palette::accent : Palette::signal);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * fraction));
        }
    }

    // The rack: one panel with a hairline edge, uprights between the sections.
    {
        const auto outer = rackArea.toFloat();
        g.setColour (Palette::rackEdge);
        g.fillRoundedRectangle (outer, 5.0f);
        g.setColour (Palette::rackPanel);
        g.fillRoundedRectangle (outer.reduced (1.0f), 4.0f);

        const auto heads = juce::StringArray::fromTokens (shownEqHeads, "|", {});

        for (size_t i = 0; i < sections.size(); ++i)
        {
            const auto& section = sections[i];
            const auto zone = Palette::zone (section.zone);

            if (i > 0)
            {
                g.setColour (Palette::rackDivider);
                g.fillRect (section.area.getX() - 1, section.area.getY(), 1, section.area.getHeight());
            }

            // The dot over the title, the title read bottom to top; centred as a
            // pair. Grey while the analyzer has the knobs off.
            const auto column = section.area.withWidth (titleColumn).withTrimmedLeft (6).toFloat();
            const auto font = Palette::font (10.0f, true, 0.12f);
            const auto title = section.title.toUpperCase();
            const auto length = juce::GlyphArrangement::getStringWidth (font, title);
            const auto top = column.getCentreY() - (6.0f + 6.0f + length) * 0.5f;

            g.setColour (shownAnalyzing ? Palette::plotZero : zone.line);
            g.fillEllipse (column.getCentreX() - 3.0f, top, 6.0f, 6.0f);

            {
                const auto centre = juce::Point<float> (column.getCentreX(), top + 12.0f + length * 0.5f);
                juce::Graphics::ScopedSaveState rotated (g);
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, centre.x, centre.y));
                g.setColour (shownAnalyzing ? Palette::plotText : zone.label);
                g.setFont (font);
                g.drawText (title, juce::Rectangle<float> (length + 4.0f, 14.0f).withCentre (centre), juce::Justification::centred, false);
            }

            // Over the knobs: the hint, or each EQ band's name and Q over its two knobs.
            auto head = section.area.withTrimmedLeft (titleColumn).reduced (6, 0);
            head = head.withSizeKeepingCentre (head.getWidth(), headRow + 4 + Knob::stackedHeight).removeFromTop (headRow);
            const auto alpha = shownAnalyzing ? 0.38f : 1.0f;
            g.setFont (Palette::font (10.5f, true, 0.06f));

            if (section.hint.isNotEmpty())
            {
                g.setColour (Palette::inkFaint.withMultipliedAlpha (alpha));
                g.drawFittedText (section.hint.toUpperCase(), head.withTrimmedBottom (2), juce::Justification::centredLeft, 1, 0.8f);
            }
            else
            {
                const auto width = (float) (head.getWidth() - 2 * 6) / 3.0f;

                for (int b = 0; b < 3 && b < heads.size(); ++b)
                {
                    const auto x = (float) head.getX() + (float) b * (width + 6.0f);
                    const auto cell = juce::Rectangle<float> (x, (float) head.getY(), width, (float) head.getHeight());
                    g.setColour (zone.label.withMultipliedAlpha (alpha));
                    g.drawFittedText (heads[b].toUpperCase(), cell.withTrimmedBottom (2.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
                    g.setColour (Palette::buttonLine.withMultipliedAlpha (alpha));
                    g.fillRect (cell.withTop (cell.getBottom() - 1.0f));
                }
            }
        }
    }

    // Status bar.
    {
        auto area = footerArea;
        g.setColour (Palette::footer);
        g.fillRect (area);
        g.setColour (Palette::line.interpolatedWith (Palette::footer, 0.2f));
        g.fillRect (area.removeFromTop (1));
        area = area.reduced (margin, 0);

        const auto font = Palette::font (11.0f, false, 0.06f);
        g.setFont (font);

        const auto version = juce::String ("CLEARBOX " JucePlugin_VersionString);
        g.setColour (Palette::inkSoft);
        g.drawText (version, area.removeFromLeft (textWidth (font, version)), juce::Justification::centredLeft);
        area.removeFromLeft (16);
        g.setColour (Palette::divider);
        g.fillRect (area.removeFromLeft (1).withSizeKeepingCentre (1, 14));
        area.removeFromLeft (16);

        g.setColour (Palette::inkFaint);
        g.drawText (shownLatency, area.removeFromRight (textWidth (font, shownLatency) + 2), juce::Justification::centredRight);
        area.removeFromRight (16);

        const auto hints = juce::StringArray { "Pulls the spectrum towards a rounded 4.5 dB/oct curve over ~2 s",
                                               "Loudness stays where it was", "EQ: Q follows the gain" };
        g.drawText (hints.joinIntoString (utf8 ("   \xc2\xb7   ")), area, juce::Justification::centredLeft, true);
    }
}
