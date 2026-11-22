# Third-party notices

Clearbox is © 2026 T'Zorr and is distributed under the GNU Affero General
Public License, version 3 (AGPLv3) - see [LICENSE](LICENSE). This follows
from JUCE, below.

## JUCE

- **What:** the JUCE 9 framework (audio plugin client, audio and DSP
  modules, GUI). Not included in this repository - `CMakeLists.txt` expects
  its source at `~/JUCE` (or a path passed with `-DJUCE_PATH`) and builds it
  along with the plugin.
- **Copyright:** © Raw Material Software Limited - <https://juce.com>.
- **License:** JUCE 9 is dual-licensed. This project uses JUCE's free tier,
  the GNU Affero General Public License, version 3 (AGPLv3). Because JUCE is
  AGPLv3 here and Clearbox links it, Clearbox is itself AGPLv3 in turn - the
  AGPL's own requirement for any work that incorporates AGPL-licensed code.
  The full JUCE licence terms are at
  <https://github.com/juce-framework/JUCE/blob/master/LICENSE.md>.
- **What AGPLv3 asks of anyone who distributes this plugin (including a
  built AU/VST3, not only source):** make the complete corresponding source
  available under the same licence, and preserve the copyright and licence
  notices. This repository already is that source.

## Apple Accelerate

The FFTs run on Apple's Accelerate framework (vDSP), part of macOS. It is a
system library: linked, not included, and not covered by this licence.

## Everything else

The engine (the STFT, the ERB analysis, the target curve, the gates, Lift,
Trim, Bias and Brighten, the loudness offset, the resonance stage and the
soft bells) is Clearbox's own code. Parts of the panel - the knob, the
palette, the header controls and the preset menu - come from its siblings
Tonbox and Rackbox Plus, by the same author and under the same licence.

Ideas and published models it implements, without taking over any code:

- **Resonance:** the idea of finding prominent spectral peaks and laying a
  Gaussian notch on each comes from DyERS by Thiago Lobato (AGPL-3.0).
  Clearbox implements it independently.
- **Analysis bands:** the ERB scale of Glasberg and Moore.
- **Loudness:** Zwicker's loudness model, and the threshold in quiet after
  Terhardt; Boost follows the equal-loudness contours.

No other library is linked, and nothing is downloaded at build or run time.
