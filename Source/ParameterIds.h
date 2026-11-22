//
//  ParameterIds.h
//  Clearbox
//
//  Every parameter the host can see, named in one place.
//
//  These ids are the identity a saved session is restored by; once a session
//  exists none of them may change. Rename a display name instead.
//

#pragma once

#include <juce_core/juce_core.h>

namespace ClearParams
{
    constexpr int stateVersion = 1;

    inline const juce::Identifier stateTreeType { "CLEARBOX" };

    inline constexpr const char* lift     = "lift";       // 0 ... 100 %: made up below the target curve
    inline constexpr const char* trim     = "trim";       // 0 ... 100 %: taken off above it
    inline constexpr const char* resonance = "resonance"; // 0 ... 100 %: ringing peaks brought down to the threshold
    inline constexpr const char* resSens   = "ressens";   // 0 ... 100 %: Sensitivity, threshold 12 ... 3 dB
    inline constexpr const char* bias     = "bias";       // -100 ... 100 %
    inline constexpr const char* brighten = "brighten";   // -100 ... 100 %
    inline constexpr const char* boost    = "boost";      // -50 ... 50 dB
    inline constexpr const char* gain     = "gain";       // output, -24 ... 24 dB
    inline constexpr const char* bypass   = "bypass";
    inline constexpr const char* analyze  = "analyze";    // the analyzer: knobs off, the deviation shown

    // The soft EQ after the dynamic one: three peak bands, Q follows the gain.
    inline constexpr const char* eq1Freq  = "eq1freq";    // Low: 30 Hz ... 2 kHz
    inline constexpr const char* eq1Gain  = "eq1gain";    // -12 ... 12 dB
    inline constexpr const char* eq3Freq  = "eq3freq";    // Mid: 150 Hz ... 8 kHz
    inline constexpr const char* eq3Gain  = "eq3gain";    // -12 ... 12 dB
    inline constexpr const char* eq2Freq  = "eq2freq";    // High: 500 Hz ... 16 kHz
    inline constexpr const char* eq2Gain  = "eq2gain";    // -12 ... 12 dB

    /** Every knob, for Reset. Bypass and Analyze are switches and stay as they are. */
    inline constexpr const char* knobs[] { lift, trim, bias, brighten, resonance, resSens, eq1Freq, eq1Gain, eq3Freq, eq3Gain,
                                           eq2Freq, eq2Gain, boost, gain };
}
