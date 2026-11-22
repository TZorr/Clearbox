//
//  EditorShot.cpp
//  Clearbox
//
//  Builds the plugin headlessly, checks what needs the whole processor, and
//  paints the panel into PNGs.
//
//  - the state round trip: every parameter through getStateInformation into a
//    fresh processor, with the knobs moved immediately before saving;
//  - presets: the plugin's own .clearbox file saves, loads, and refuses another
//    plugin's preset and an .aupreset;
//  - the latency the host is told is the delay the audio has, at 48 and 96 kHz;
//  - Bypass is the input delayed by that latency, bit for bit;
//  - Gain is a gain;
//  - a mono buffer runs;
//  - the panel shows the curve the engine is applying.
//
//  Usage:  EditorShot <output-directory>
//

#include <juce_gui_basics/juce_gui_basics.h>

#include "Engine/PresetFile.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "UI/Palette.h"

#include <cstdio>
#include <random>

namespace
{
    int failures = 0, checks = 0;

    void check (bool condition, const juce::String& what, const juce::String& detail = {})
    {
        ++checks;
        std::printf ("  %s  %-60s %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8(), detail.toRawUTF8());
        if (! condition)
            ++failures;
    }

    void setParam (ClearboxProcessor& processor, const juce::String& id, float value)
    {
        auto* parameter = processor.parameters.getParameter (id);
        jassert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    float getParam (ClearboxProcessor& processor, const juce::String& id)
    {
        return processor.parameters.getRawParameterValue (id)->load();
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    bool writePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();   // createOutputStream appends
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        juce::PNGImageFormat png;
        return stream != nullptr && png.writeImageToStream (image, *stream);
    }

    struct Run { std::vector<float> inL, inR, outL, outR; };

    /** Blocks of noise (or a demo signal) through processBlock. */
    Run render (ClearboxProcessor& processor, int blocks, int blockSize, unsigned seed, bool demo = false)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-0.3f, 0.3f);
        Run run;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                auto l = dist (rng), r = dist (rng);

                if (demo)   // quiet noise under a loud low tone: Trim and Lift both have work
                {
                    const auto tone = 0.35f * (float) std::sin (phase);
                    phase += 2.0 * M_PI * 180.0 / 48000.0;
                    l = 0.03f * l + tone;
                    r = 0.03f * r + tone;
                }

                buffer.setSample (0, i, l);
                buffer.setSample (1, i, r);
                run.inL.push_back (l);
                run.inR.push_back (r);
            }

            processor.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                run.outL.push_back (buffer.getSample (0, i));
                run.outR.push_back (buffer.getSample (1, i));
            }
        }

        return run;
    }

    void checkStateRoundTrip()
    {
        ClearboxProcessor original;
        const std::pair<const char*, float> values[] {
            { ClearParams::lift, 37.0f }, { ClearParams::trim, 64.0f }, { ClearParams::bias, -23.0f },
            { ClearParams::resonance, 55.0f }, { ClearParams::resSens, 70.0f },
            { ClearParams::brighten, 41.0f }, { ClearParams::boost, -7.5f },
            { ClearParams::gain, 3.5f }, { ClearParams::bypass, 1.0f }, { ClearParams::analyze, 1.0f },
            { ClearParams::eq1Freq, 150.0f }, { ClearParams::eq1Gain, -4.5f }, { ClearParams::eq3Freq, 2200.0f }, { ClearParams::eq3Gain, 2.5f },
            { ClearParams::eq2Freq, 6000.0f }, { ClearParams::eq2Gain, 7.0f } };

        for (const auto& [id, v] : values)
            setParam (original, id, v);

        juce::MemoryBlock state;
        original.getStateInformation (state);

        ClearboxProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());

        for (const auto& [id, v] : values)
            check (std::abs (getParam (restored, id) - v) < 0.05f * std::max (1.0f, std::abs (v)) + 0.01f,
                   juce::String ("state round trip: ") + id, juce::String (getParam (restored, id)));
    }

    void checkPresets()
    {
        const auto folder = juce::File::createTempFile ("presets");
        folder.createDirectory();
        const auto file = folder.getChildFile (juce::String ("Test") + PresetFile::extension);

        ClearboxProcessor original;
        setParam (original, ClearParams::trim, 80.0f);
        check (PresetButton::save (original, file).wasOk(), "a preset saves");

        const auto text = file.loadFileAsString();
        const auto xml = juce::XmlDocument::parse (text);
        check (xml != nullptr && xml->hasTagName ("TZorrPreset") && xml->getStringAttribute ("plugin") == "Clbx"
                   && xml->getNumChildElements() == 1,
               "the preset is readable XML naming Clbx", text.substring (0, 80));

        ClearboxProcessor loaded;
        check (PresetButton::load (loaded, file).wasOk(), "a preset loads");
        check (std::abs (getParam (loaded, ClearParams::trim) - 80.0f) < 0.5f, "a preset brings back the knobs",
               juce::String (getParam (loaded, ClearParams::trim)));

        const auto foreign = folder.getChildFile (juce::String ("Foreign") + PresetFile::extension);
        foreign.replaceWithText (text.replace ("plugin=\"Clbx\"", "plugin=\"Tnbx\""));
        ClearboxProcessor untouched;
        check (PresetButton::load (untouched, foreign).failed() && juce::exactlyEqual (getParam (untouched, ClearParams::trim), 0.0f),
               "another plugin's preset is refused");

        const auto auPreset = folder.getChildFile ("Logic.aupreset");
        auPreset.replaceWithText ("<?xml version=\"1.0\"?><plist version=\"1.0\"><dict/></plist>");
        check (PresetButton::load (untouched, auPreset).failed() && juce::exactlyEqual (getParam (untouched, ClearParams::trim), 0.0f),
               "an .aupreset is refused");

        folder.deleteRecursively();
    }

    void checkLatencyAndBypass()
    {
        for (const auto [rate, expected] : { std::pair { 48000.0, 1024 }, { 96000.0, 2048 } })
        {
            ClearboxProcessor processor;
            processor.setPlayConfigDetails (2, 2, rate, 512);
            processor.prepareToPlay (rate, 512);
            check (processor.getLatencySamples() == expected,
                   "the host is told " + juce::String (expected) + " samples at " + juce::String (rate / 1000.0, 0) + " kHz",
                   juce::String (processor.getLatencySamples()));

            // Neutral: the input, that much later (within the STFT's rounding).
            const auto run = render (processor, 200, 512, 1);
            double e = 0, r = 0;
            for (size_t i = (size_t) expected * 2; i < run.outL.size(); ++i)
            {
                const auto d = run.outL[i] - run.inL[i - (size_t) expected];
                e += (double) d * d;
                r += (double) run.inL[i] * run.inL[i];
            }
            const auto db = 10.0 * std::log10 (e / r + 1e-30);
            check (db < -110.0, "neutral is the input delayed by the reported latency", juce::String (db, 1) + " dB");
        }

        ClearboxProcessor processor;
        processor.setPlayConfigDetails (2, 2, 48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        setParam (processor, ClearParams::trim, 75.0f);
        setParam (processor, ClearParams::lift, 75.0f);
        setParam (processor, ClearParams::bypass, 1.0f);
        setParam (processor, ClearParams::gain, 12.0f);

        const auto run = render (processor, 100, 512, 2);
        bool exact = true;
        for (size_t i = 4096; i < run.outL.size(); ++i)
            exact = exact && juce::exactlyEqual (run.outL[i], run.inL[i - 1024]) && juce::exactlyEqual (run.outR[i], run.inR[i - 1024]);
        check (exact, "Bypass is the input delayed by the latency, bit for bit (Gain too)");

        // Gain: +6 dB with nothing else is twice the delayed input.
        ClearboxProcessor plain;
        plain.setPlayConfigDetails (2, 2, 48000.0, 512);
        plain.prepareToPlay (48000.0, 512);
        setParam (plain, ClearParams::gain, 6.0206f);
        const auto loud = render (plain, 60, 512, 3);
        double worst = 0.0;
        for (size_t i = 8192; i < loud.outL.size(); ++i)
            worst = std::max (worst, (double) std::abs (loud.outL[i] - 2.0f * loud.inL[i - 1024]));
        check (worst < 1.0e-4, "Gain +6 dB doubles the signal", juce::String (worst, 7));
    }

    void checkMono()
    {
        ClearboxProcessor processor;
        processor.setPlayConfigDetails (1, 1, 48000.0, 256);
        processor.prepareToPlay (48000.0, 256);
        setParam (processor, ClearParams::trim, 50.0f);

        juce::AudioBuffer<float> buffer (1, 256);
        juce::MidiBuffer midi;
        std::mt19937 rng (4);
        std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
        bool finite = true;
        double energy = 0.0;

        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < 256; ++i)
                buffer.setSample (0, i, dist (rng));
            processor.processBlock (buffer, midi);
            for (int i = 0; i < 256; ++i)
            {
                finite = finite && std::isfinite (buffer.getSample (0, i));
                energy += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
            }
        }

        check (finite && energy > 1.0, "a mono buffer runs and comes out");
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::File outputDirectory = argc > 1 ? juce::File (juce::String (argv[1]))
                                                : juce::File::getCurrentWorkingDirectory();

    if (! outputDirectory.createDirectory())
    {
        std::printf ("FAIL: cannot create %s\n", outputDirectory.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("\nEditorShot\n\nState...\n");
    checkStateRoundTrip();
    std::printf ("Presets...\n");
    checkPresets();
    std::printf ("Latency, bypass, gain...\n");
    checkLatencyAndBypass();
    std::printf ("Mono...\n");
    checkMono();

    // The soft EQ through the processor: a bell at +6 dB measures +6 dB at its centre.
    std::printf ("EQ...\n");
    {
        const auto measure = [] (const char* freqId, const char* gainId, float hz)
        {
            ClearboxProcessor processor;
            processor.setPlayConfigDetails (2, 2, 48000.0, 512);
            processor.prepareToPlay (48000.0, 512);
            setParam (processor, freqId, hz);
            setParam (processor, gainId, 6.0f);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            double in = 0, out = 0, phase = 0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const auto s = 0.1f * (float) std::sin (phase);
                    phase += 2.0 * M_PI * hz / 48000.0;
                    buffer.setSample (0, i, s);
                    buffer.setSample (1, i, s);
                    if (b >= 100) in += (double) s * s;
                }
                processor.processBlock (buffer, midi);
                if (b >= 100)
                    for (int i = 0; i < 512; ++i) out += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
            }
            return 10.0 * std::log10 (out / in);
        };

        const auto low = measure (ClearParams::eq1Freq, ClearParams::eq1Gain, 200.0f);
        const auto mid = measure (ClearParams::eq3Freq, ClearParams::eq3Gain, 1000.0f);
        const auto high = measure (ClearParams::eq2Freq, ClearParams::eq2Gain, 4000.0f);
        check (std::abs (low - 6.0) < 0.2 && std::abs (mid - 6.0) < 0.2 && std::abs (high - 6.0) < 0.2,
               "Low, Mid and High at +6 dB measure +6 dB at their centres",
               juce::String (low, 2) + " / " + juce::String (mid, 2) + " / " + juce::String (high, 2) + " dB");
    }

    // Reset: every knob back to its default, Bypass left alone.
    std::printf ("Reset...\n");
    {
        ClearboxProcessor processor;
        for (const auto* id : ClearParams::knobs)
        {
            auto* p = processor.parameters.getParameter (id);
            p->setValueNotifyingHost (p->getDefaultValue() < 0.5f ? 0.9f : 0.1f);
        }
        setParam (processor, ClearParams::bypass, 1.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* clear = dynamic_cast<ClearboxEditor*> (editor.get());
        clear->getResetButton().triggerClick();
        pump (200);

        bool allDefault = true;
        juce::String off;
        for (const auto* id : ClearParams::knobs)
        {
            auto* p = processor.parameters.getParameter (id);
            if (std::abs (p->getValue() - p->getDefaultValue()) > 1.0e-6f) { allDefault = false; off << id << " "; }
        }
        check (allDefault, "Reset puts every knob back to its default", off);
        check (getParam (processor, ClearParams::bypass) >= 0.5f, "and leaves Bypass as it was");
        check (getParam (processor, ClearParams::analyze) < 0.5f, "and Analyze too");
        check (! clear->getResetButton().getToggleState(), "the Reset button does not stay lit");
        editor.reset();
    }

    // The analyzer: every knob turned up does nothing, and the display reads the deviation.
    std::printf ("Analyzer...\n");
    {
        ClearboxProcessor processor;
        processor.setPlayConfigDetails (2, 2, 48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        setParam (processor, ClearParams::lift, 100.0f);
        setParam (processor, ClearParams::trim, 100.0f);
        setParam (processor, ClearParams::boost, 20.0f);
        setParam (processor, ClearParams::eq3Gain, 9.0f);
        setParam (processor, ClearParams::gain, 6.0f);
        setParam (processor, ClearParams::analyze, 1.0f);

        // Pink-ish noise, so the analyzer has a known slope to read.
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        std::mt19937 rng (12);
        std::normal_distribution<float> d (0.0f, 1.0f);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        std::vector<float> in, out;
        for (int block = 0; block < 600; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double w = d (rng);
                b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
                b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
                const auto v = (float) (0.02 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362));
                b6 = w * 0.115926;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
                in.push_back (v);
            }
            processor.processBlock (buffer, midi);
            for (int i = 0; i < 512; ++i) out.push_back (buffer.getSample (0, i));
        }

        bool exact = true;
        for (size_t i = 4096; i < out.size(); ++i)
            exact = exact && juce::exactlyEqual (out[i], in[i - 1024]);
        check (exact, "analyzing, every knob turned up does nothing: the output is the input, 1024 samples later, bit for bit");

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* clear = dynamic_cast<ClearboxEditor*> (editor.get());
        pump (150);

        auto& display = clear->getDisplay();
        bool allThere = true;
        for (int i = 0; i < CurveDisplay::numOctaves; ++i)
            allThere = allThere && display.octaveHasSignal (i);
        // 1.5 dB/oct over 5 octaves, plus the target's own roll-off at 8 kHz
        const auto slope = display.octaveDeviationDb (7) - display.octaveDeviationDb (2);   // 8 kHz - 250 Hz
        const auto expected = 7.5f + ClearEngine::targetShapeDb (250.0f) - ClearEngine::targetShapeDb (8000.0f);
        check (allThere && std::abs (slope - expected) < 1.5f, "pink noise: every octave 63 Hz - 16 kHz reads, 8 k - 250 Hz as expected",
               juce::String (slope, 1) + " dB, expected " + juce::String (expected, 1));
        check (! clear->getLiftKnob().isEnabled(), "the knobs are greyed out while analyzing");

        Palette::setDark (true);
        const auto file = outputDirectory.getChildFile ("clearbox-analyze.png");
        check (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), file),
               "the analyzer panel renders to " + file.getFullPathName());
        Palette::setDark (false);
        editor.reset();
    }

    // The panel: a loud low tone over quiet noise, pulled to the curve, with two bells.
    std::printf ("Panel...\n");
    {
        ClearboxProcessor processor;
        processor.setPlayConfigDetails (2, 2, 48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        setParam (processor, ClearParams::trim, 80.0f);
        setParam (processor, ClearParams::lift, 60.0f);
        setParam (processor, ClearParams::resonance, 100.0f);
        setParam (processor, ClearParams::eq3Freq, 800.0f);
        setParam (processor, ClearParams::eq3Gain, -3.0f);
        setParam (processor, ClearParams::eq2Freq, 5000.0f);
        setParam (processor, ClearParams::eq2Gain, 4.0f);
        render (processor, 400, 512, 9, true);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* clear = dynamic_cast<ClearboxEditor*> (editor.get());
        pump (100);

        check (clear->getDisplay().maxCutDb() < -3.0f, "the display shows the cut on the loud tone",
               juce::String (clear->getDisplay().maxCutDb(), 1) + " dB");
        check (processor.getEqMagnitudeDb (5000.0f) > 3.5f && processor.getEqMagnitudeDb (800.0f) < -2.5f,   // the bells overlap a little
               "the Mid and High bells are part of the shown curve",
               juce::String (processor.getEqMagnitudeDb (800.0f), 2) + " dB at 800 Hz, "
                   + juce::String (processor.getEqMagnitudeDb (5000.0f), 2) + " dB at 5 kHz");
        check (clear->getDisplay().maxLiftDb() > 1.0f, "and the lift elsewhere",
               juce::String (clear->getDisplay().maxLiftDb(), 1) + " dB");

        Palette::setDark (false);
        editor->repaint();
        const auto file = outputDirectory.getChildFile ("clearbox.png");
        check (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), file),
               "the panel renders to " + file.getFullPathName());

        Palette::setDark (true);
        const auto darkFile = outputDirectory.getChildFile ("clearbox-dark.png");
        check (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), darkFile),
               "the dark panel renders to " + darkFile.getFullPathName());
        Palette::setDark (false);

        editor.reset();
    }

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
