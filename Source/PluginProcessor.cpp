//
//  PluginProcessor.cpp
//  Clearbox
//

#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
    juce::String hertzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    juce::String signedText (float v, int decimals, const char* unit)
    {
        const auto rounded = decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        return (v > 0.0f && ! rounded.startsWith ("0") ? "+" : "") + rounded + unit;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ClearboxProcessor::makeParameterLayout()
{
    using namespace juce;
    using namespace ClearParams;

    std::vector<std::unique_ptr<RangedAudioParameter>> layout;

    const auto percent = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });

    const auto signedPercent = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return signedText (v, 0, " %"); })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });

    const auto decibels = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return signedText (v, 1, " dB"); })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });

    const auto hertz = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return hertzText (v); })
        .withValueFromStringFunction ([] (const String& t)
        {
            const auto value = t.getFloatValue();
            return t.containsIgnoreCase ("k") ? value * 1000.0f : value;
        });


    const auto add = [&] (const char* id, const char* name, NormalisableRange<float> range, float def, const AudioParameterFloatAttributes& a)
    {
        layout.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, stateVersion }, name, range, def, a));
    };

    add (ClearParams::lift,     "Lift",     { 0.0f, 100.0f }, 0.0f, percent);
    add (ClearParams::trim,     "Trim",     { 0.0f, 100.0f }, 0.0f, percent);
    add (ClearParams::resonance, "Resonance", { 0.0f, 100.0f }, 0.0f, percent);
    add (ClearParams::resSens,   "Sensitivity", { 0.0f, 100.0f }, 50.0f, percent);
    add (ClearParams::bias,     "Bias",     { -100.0f, 100.0f }, 0.0f, signedPercent);
    add (ClearParams::brighten, "Brighten", { -100.0f, 100.0f }, 0.0f, signedPercent);
    add (ClearParams::boost,    "Boost",    { -50.0f, 50.0f }, 0.0f, decibels);
    add (ClearParams::gain,     "Gain",     { -24.0f, 24.0f }, 0.0f, decibels);

    layout.push_back (std::make_unique<AudioParameterBool> (ParameterID { ClearParams::bypass, stateVersion }, "Bypass", false));
    layout.push_back (std::make_unique<AudioParameterBool> (ParameterID { ClearParams::analyze, stateVersion }, "Analyze", false));

    NormalisableRange<float> lowFreq (30.0f, 2000.0f), midFreq (150.0f, 8000.0f), highFreq (500.0f, 16000.0f);
    lowFreq.setSkewForCentre (250.0f);
    midFreq.setSkewForCentre (1000.0f);
    highFreq.setSkewForCentre (2800.0f);
    const NormalisableRange<float> eqGain (-SoftEq::maxGainDb, SoftEq::maxGainDb);

    add (ClearParams::eq1Freq, "Low Freq",  lowFreq, 200.0f, hertz);
    add (ClearParams::eq1Gain, "Low Gain",  eqGain, 0.0f, decibels);
    add (ClearParams::eq3Freq, "Mid Freq",  midFreq, 1000.0f, hertz);
    add (ClearParams::eq3Gain, "Mid Gain",  eqGain, 0.0f, decibels);
    add (ClearParams::eq2Freq, "High Freq", highFreq, 4000.0f, hertz);
    add (ClearParams::eq2Gain, "High Gain", eqGain, 0.0f, decibels);

    return { layout.begin(), layout.end() };
}

//==============================================================================
ClearboxProcessor::ClearboxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, ClearParams::stateTreeType, makeParameterLayout())
{
    lift     = parameters.getRawParameterValue (ClearParams::lift);
    trim     = parameters.getRawParameterValue (ClearParams::trim);
    bias     = parameters.getRawParameterValue (ClearParams::bias);
    resonanceAmount = parameters.getRawParameterValue (ClearParams::resonance);
    resonanceSens   = parameters.getRawParameterValue (ClearParams::resSens);
    brighten = parameters.getRawParameterValue (ClearParams::brighten);
    boost    = parameters.getRawParameterValue (ClearParams::boost);
    gain     = parameters.getRawParameterValue (ClearParams::gain);
    bypass   = parameters.getRawParameterValue (ClearParams::bypass);
    analyze  = parameters.getRawParameterValue (ClearParams::analyze);
    eq1Freq  = parameters.getRawParameterValue (ClearParams::eq1Freq);
    eq1Gain  = parameters.getRawParameterValue (ClearParams::eq1Gain);
    eq3Freq  = parameters.getRawParameterValue (ClearParams::eq3Freq);
    eq3Gain  = parameters.getRawParameterValue (ClearParams::eq3Gain);
    eq2Freq  = parameters.getRawParameterValue (ClearParams::eq2Freq);
    eq2Gain  = parameters.getRawParameterValue (ClearParams::eq2Gain);

    // The latency is known before prepareToPlay for the rates most sessions run at,
    // so a host that asks early is not told 0.
    setLatencySamples (1024);
}

bool ClearboxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();

    return out == juce::AudioChannelSet::mono() && in == juce::AudioChannelSet::mono();
}

void ClearboxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (1, samplesPerBlock);
    engine.prepare (sampleRate, maxBlock);
    softEq.prepare (sampleRate);

    latency = engine.getLatencySamples();
    setLatencySamples (latency);

    dryDelay.setSize (2, latency + 1);
    dryDelay.clear();
    dryWrite = 0;

    gainSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gain->load()));
    bypassSmoothed.reset (sampleRate, 0.02);
    bypassSmoothed.setCurrentAndTargetValue (bypass->load() >= 0.5f ? 1.0f : 0.0f);
}

ClearEngine::Settings ClearboxProcessor::getSettings() const noexcept
{
    ClearEngine::Settings s;
    s.lift     = lift->load() / 100.0f;
    s.trim     = trim->load() / 100.0f;
    s.bias     = bias->load() / 100.0f;
    s.resonance   = resonanceAmount->load() / 100.0f;
    s.sensitivity = resonanceSens->load() / 100.0f;
    s.brighten = brighten->load() / 100.0f;
    s.boostDb  = boost->load();
    return s;
}

SoftEq::Settings ClearboxProcessor::getEqSettings() const noexcept
{
    SoftEq::Settings s;
    s.hz = { eq1Freq->load(), eq3Freq->load(), eq2Freq->load() };          // Low, Mid, High
    s.gainDb = { eq1Gain->load(), eq3Gain->load(), eq2Gain->load() };
    return s;
}

void ClearboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() < 1)
        return;

    const auto stereoBuffer = buffer.getNumChannels() >= 2;

    // A mono input on a stereo output: JUCE leaves the second channel unspecified.
    if (stereoBuffer && getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    // Analyzing: the engine goes on measuring with every knob at rest, the bells are
    // flat, and the output takes Bypass's path - the input, delayed by the latency.
    const auto analyzing = isAnalyzing();
    const auto settings = analyzing ? ClearEngine::Settings {} : getSettings();
    auto eqSettings = getEqSettings();
    if (analyzing)
        eqSettings.gainDb = {};

    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (gain->load()));
    bypassSmoothed.setTargetValue (bypass->load() >= 0.5f || analyzing ? 1.0f : 0.0f);

    auto* left = buffer.getWritePointer (0);
    auto* right = stereoBuffer ? buffer.getWritePointer (1) : nullptr;
    const auto total = buffer.getNumSamples();

    // The header's meters: the peak in and out, per side, held until the panel takes it.
    const auto holdPeaks = [&] (int first)
    {
        for (int side = 0; side < 2; ++side)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (side == 1 && right != nullptr ? right : left, total);
            const auto magnitude = juce::jmax (-range.getStart(), range.getEnd());
            auto& held = meterPeaks[(size_t) (first + side)];
            auto current = held.load (std::memory_order_relaxed);
            while (magnitude > current && ! held.compare_exchange_weak (current, magnitude)) {}
        }
    };

    holdPeaks (0);

    const auto delaySize = dryDelay.getNumSamples();
    auto* dryL = dryDelay.getWritePointer (0);
    auto* dryR = dryDelay.getWritePointer (1);

    // In pieces of 512, the dry signal delayed by exactly the engine's latency:
    // written first, read `latency` samples back.
    float dry[2][512];

    for (int start = 0; start < total; start += 512)
    {
        const auto n = juce::jmin (512, total - start);
        auto* l = left + start;
        auto* r = stereoBuffer ? right + start : nullptr;

        for (int i = 0; i < n; ++i)
        {
            dryL[dryWrite] = l[i];
            dryR[dryWrite] = r != nullptr ? r[i] : l[i];

            auto read = dryWrite - latency;
            if (read < 0)
                read += delaySize;

            dry[0][i] = dryL[read];
            dry[1][i] = dryR[read];

            if (++dryWrite == delaySize)
                dryWrite = 0;
        }

        engine.process (l, r != nullptr ? r : l, n, settings);
        softEq.process (l, r, n, eqSettings);

        for (int i = 0; i < n; ++i)
        {
            const auto g = gainSmoothed.getNextValue();
            const auto b = bypassSmoothed.getNextValue();

            // Exact at both ends: bypassed is the delayed input itself.
            if (b >= 1.0f)
            {
                l[i] = dry[0][i];
                if (r != nullptr) r[i] = dry[1][i];
            }
            else
            {
                l[i] = b * dry[0][i] + (1.0f - b) * g * l[i];
                if (r != nullptr) r[i] = b * dry[1][i] + (1.0f - b) * g * r[i];
            }
        }
    }

    holdPeaks (2);
}

//==============================================================================
//  copyState(), never state.createXml(): copyState() flushes the knobs first.
void ClearboxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = parameters.copyState();

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ClearboxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

void ClearboxProcessor::resetKnobs()
{
    for (const auto* id : ClearParams::knobs)
        if (auto* parameter = parameters.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
}

juce::AudioProcessorEditor* ClearboxProcessor::createEditor()
{
    return new ClearboxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClearboxProcessor();
}
