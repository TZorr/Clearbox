//
//  PluginEditor.h
//  Clearbox
//
//  The panel, since 0.9.0 the "Clearbox - Target EQ" mockup (Kitbox 0.10's
//  "Variante B"):
//
//      CLEARBOX  [<][>][ preset  .CLEARBOX  v] LOAD SAVE     In / Out  ||||  ||  [moon]
//      [ APPLIED NOW  | the curve: lift, cut, the EQ's bells       ]  ANALYZE
//      [ +2.8 dB      |                                            ]  RESET
//      [ -5.1 dB      | 20  50  100  200  500  1k  2k  5k  10k 20k ]  BYPASS
//      +----------------------------------------------------------------------+
//      |S| SHAPE: Lift Trim Bias Brighten |R| Amount Sens |E| L M H |O| Boost Gain |
//      +----------------------------------------------------------------------+
//      CLEARBOX 0.9.0 | hints                                         Latency 21 ms
//
//  The rack is one panel, its sections split by uprights, every section
//  named down its left edge and in the colour of what it works on. While
//  Analyze is on the knobs do nothing, and the rack fades back.
//
//  The panel is laid out once at the mockup's 1248 x 488 and scaled as a
//  whole: the window resizes freely at that aspect ratio, opens at 85 % the
//  first time, and remembers the size per user. Laid out from constants, so
//  EditorShot's render is the panel as a host shows it.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/Controls.h"
#include "UI/CurveDisplay.h"
#include "UI/Knob.h"
#include "UI/PresetButton.h"

class ClearboxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClearboxEditor (ClearboxProcessor&);
    ~ClearboxEditor() override;

    /** The panel's own size; the window shows it scaled. */
    static constexpr int panelWidth  = 1248;
    static constexpr int panelHeight = 488;
    static constexpr double defaultScale = 0.85, minScale = 0.6, maxScale = 1.4;

    void paint (juce::Graphics&) override;
    void resized() override;

    CurveDisplay& getDisplay() noexcept { return display; }
    juce::Button& getResetButton() noexcept { return reset; }
    const juce::Component& getLiftKnob() const noexcept { return lift; }

private:
    /** The 1248 x 488 panel: everything on it is its child, and it is drawn
        through the editor's paintCanvas. */
    struct Canvas : public juce::Component
    {
        explicit Canvas (ClearboxEditor& ownerToUse) : owner (ownerToUse) { setOpaque (true); }
        void paint (juce::Graphics& g) override { owner.paintCanvas (g); }
        ClearboxEditor& owner;
    };

    /** A section of the rack: its upright title, a hint (or the EQ's band
        names) over its knobs. */
    struct Section
    {
        juce::String title, hint;
        Palette::ZoneId zone = Palette::ZoneId::none;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> area;
    };

    void timerCallback() override;
    void setDarkMode (bool shouldBeDark);
    void refreshThemeIcon();
    void showAnalyzing (bool on);
    void paintCanvas (juce::Graphics&);
    void layoutCanvas();
    juce::String latencyText() const;

    ClearboxProcessor& clearbox;
    Canvas canvas { *this };
    juce::TooltipWindow tooltipWindow { this, 500 };
    CurveDisplay display;
    Knob lift { "Lift" }, trim { "Trim" }, bias { "Bias", true }, brighten { "Brighten", true },
         resonance { "Amount" }, sensitivity { "Sensitivity" },
         lowFreq { "Freq" }, lowGain { "Gain", true }, midFreq { "Freq" }, midGain { "Gain", true },
         highFreq { "Freq" }, highGain { "Gain", true }, boost { "Boost", true }, gain { "Gain", true };
    PanelButton analyze { "Analyze", {}, PanelButton::Style::column },
                reset { "Reset", {}, PanelButton::Style::column },
                bypass { "Bypass", {}, PanelButton::Style::column };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, analyzeAttachment;
    bool shownAnalyzing = false;

    std::array<Section, 4> sections;

    PresetButton presetButton;
    PanelButton prevPresetButton { {}, "M10 3.5 5.5 8l4.5 4.5" }, nextPresetButton { {}, "M6 3.5 10.5 8 6 12.5" };
    PanelButton loadPresetButton { "Load", "M2 4.5A1.5 1.5 0 0 1 3.5 3H6l1.5 1.5h5A1.5 1.5 0 0 1 14 6v5.5a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 2 11.5z" };
    PanelButton savePresetButton { "Save", "M8 2.5v7.5M4.8 7 8 10.2 11.2 7M3 13.5h10" };
    PanelButton themeButton { {} };

    std::unique_ptr<juce::PropertiesFile> settings;
    bool shownDark = false;
    std::array<float, 4> meterLevels {};   // in L, in R, out L, out R
    juce::String shownLatency, shownEqHeads;

    juce::Rectangle<int> headerArea, wordmarkArea, ioArea, metersArea, rackArea, footerArea;
    std::array<juce::Rectangle<int>, 2> headerDividers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearboxEditor)
};
