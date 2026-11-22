//
//  PluginProcessor.h
//  Clearbox
//
//  Input -> ClearEngine (the target-curve EQ) -> SoftEq (three bells) -> Gain -> output, with Bypass
//  fading to the input delayed by the engine's latency, so switching it never
//  jumps in time. Analyze takes the same path as Bypass while the engine goes
//  on measuring with every knob at rest: the knobs do nothing, the panel shows
//  how far the input is from the curve.
//
//  Channels: stereo -> stereo, mono -> stereo, mono -> mono. The engine is
//  linked stereo; a mono buffer goes through it as one channel.
//
//  All state is the parameter tree.
//

#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/ClearEngine.h"
#include "Engine/SoftEq.h"
#include "ParameterIds.h"

class ClearboxProcessor : public juce::AudioProcessor
{
public:
    ClearboxProcessor();
    ~ClearboxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

    /** The curve and the band levels, for the panel. */
    const ClearEngine& getEngine() const noexcept { return engine; }

    /** The engine settings the parameters currently ask for. */
    ClearEngine::Settings getSettings() const noexcept;

    /** True while the analyzer is on: the knobs do nothing, the output is the input. */
    bool isAnalyzing() const noexcept { return analyze->load() >= 0.5f; }

    /** Every knob back to its default, as gestures the host can undo. Bypass and Analyze stay. */
    void resetKnobs();

    /** The soft EQ's settings, and its response for the panel. */
    SoftEq::Settings getEqSettings() const noexcept;
    float getEqMagnitudeDb (float hz) const noexcept { return softEq.magnitudeDb (hz, getEqSettings()); }
    void getEqMagnitudeDb (const float* hz, float* outDb, int count) const noexcept { softEq.magnitudeDb (hz, outDb, count, getEqSettings()); }

    /** The peaks since the panel last took them, for the header's meters:
        0 and 1 the input's left and right, 2 and 3 the output's. */
    float takeMeterPeak (int index) noexcept { return meterPeaks[(size_t) (index & 3)].exchange (0.0f); }

    juce::AudioProcessorValueTreeState parameters;

private:
    std::array<std::atomic<float>, 4> meterPeaks {};

    ClearEngine engine;
    SoftEq softEq;

    // The input delayed by the engine's latency, for Bypass.
    juce::AudioBuffer<float> dryDelay;
    int dryWrite = 0, latency = 0;

    std::atomic<float> *lift = nullptr, *trim = nullptr, *bias = nullptr, *brighten = nullptr,
                       *boost = nullptr, *gain = nullptr, *bypass = nullptr, *analyze = nullptr,
                       *eq1Freq = nullptr, *eq1Gain = nullptr, *eq3Freq = nullptr, *eq3Gain = nullptr,
                       *eq2Freq = nullptr, *eq2Gain = nullptr, *resonanceAmount = nullptr, *resonanceSens = nullptr;

    juce::SmoothedValue<float> gainSmoothed { 1.0f }, bypassSmoothed { 0.0f };
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearboxProcessor)
};
