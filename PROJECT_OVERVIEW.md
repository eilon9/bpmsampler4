# BPM Sampler — Code Explanation

## What it is

A JUCE 7 VST3/Standalone **sample player plugin** built in C++17. It takes audio files, plays them back triggered by MIDI notes, and applies a chain of DSP processing. The name "BPM Sampler" reflects its BPM-sync and timestretch features.

---

## The Engine — PluginProcessor.h / PluginProcessor.cpp

The main class is `BPMSamplerProcessor : public juce::AudioProcessor`. This is the audio engine — it never touches the screen.

**Sample storage** lives in `slots[NUM_SLOTS=8]`, each a `SampleSlot` struct with a `juce::AudioBuffer<float>`, a file path, and a MIDI note range (`loNote`/`hiNote`). There are three ways to use those slots:

- **Single**: always use slot 0
- **MIDI Map**: on note-on, scan all 8 slots and find whose `loNote`/`hiNote` contains that MIDI note — result stored in the `activeSlot` atomic
- **Morph**: `morphPos` (0–7 float) crossfades two adjacent slots; each gets its own independent playhead in `slotPlaybackPos[8]`

**The audio path per block** (`processBlock`, ~1100 lines) is:
1. Parse the `MidiBuffer` — note-on starts playback, note-off sets `noteHeld = false`
2. `refreshBufs` lambda resolves which slot(s) are active and reads per-slot pitch/speed/volume from APVTS param pointers (`slotPitchPtrs[i]`, etc.)
3. Route to one of four playback modes:
   - **Normal** (`processNormal`): advance `playbackPos` by `step = speed * (fileSampleRate / hostSampleRate)`, resample with linear interpolation
   - **OLA** (`OLATimestretch`): overlap-add granular timestretch. Grain size 2048, 4x overlap, Hanning window. Stretches without pitch change.
   - **WSOLA** (`WSOLATimestretch`): adds cross-correlation to find the best grain alignment point, reducing phase artifacts
   - **Phase Vocoder** (`PhaseVocoder`): FFT-based (2048-point, 512-hop). Tracks per-bin phase accumulators for pitch-coherent stretching. Also does independent pitch shift by resampling the output
4. **Granular** (`processGranular`): spawns up to 32 concurrent grains from `GranularEngine`. Each grain has its own read position, step rate (for pitch), age counter, and cosine envelope. Density controls how often new grains spawn.
5. **Resonator** (`applyResonator`): runs a bank of up to 32 biquad bandpass filters — one per harmonic. Root frequency x harmonic number x inharmonicity factor. Includes a feedback delay loop (`resoDelayBuf[2][8192]`).
6. **Analog jitter**: two smoothed noise sources (0.5 Hz "wow" + 4 Hz "flutter") added to the pitch before any engine sees it.

**State persistence** (`getStateInformation` / `setStateInformation`) serializes everything to XML: APVTS parameters, slice positions (saved as `"Slices"` ValueTree child), curve data (`"PitchCurve"` etc.), and per-slot metadata. The code carefully extracts slices *before* calling `apvts.replaceState()` to avoid them being overwritten.

---

## The UI — PluginEditor.h / PluginEditor.cpp

`BPMSamplerEditor : public juce::AudioProcessorEditor` and also a `juce::Timer` (fires every 40ms ~= 25fps).

The look-and-feel is `DarkLAF` — a custom subclass with dark backgrounds (0xff1a1a2e), cyan accents (0xff00d4ff), pill-shaped toggles, and arc-style rotary knobs drawn by hand in `drawRotarySlider`.

The layout is a **7-column knob grid** at 740x650px. The columns are: Start/Slice, End, Speed, Pitch, Beats, Jitter, Drift. In granular mode, columns 0/1/2/4 are swapped out for granular-specific controls (GranPos, PosJitter, Density, Size).

The **side panel** (310px, toggled by a button) shows 8 rows of per-slot controls: file button, MIDI Lo/Hi labels, pitch/speed/volume compact knobs, and a per-slot timestretch mode combo.

The **timer callback** does the "UI polling" work: updates the playback cursor position on `WaveformDisplay`, syncs slice markers, shows/hides the morph slider based on sample mode, and enables/disables controls (e.g., the speed knob is greyed out when BPM sync is on).

---

## Curve System — CurveData.h / CurveKnob.h / CurveEditorComponent.h

`BPMCurveData` is a 32-point piecewise-linear curve stored as `{x, y}` pairs in [0,1]x[0,1]. It has a `displayMin/displayMax` range (e.g., -24 to +24 semitones for pitch) and an `evaluate(x)` that does linear interpolation between points.

`CurveKnob` wraps a standard JUCE `Slider` plus a "~" button. The knob displays `applyToNorm(paramNorm)` — the normalized parameter value passed through the curve to get the displayed value. Clicking "~" opens a `CurveEditorComponent` inside a `CallOutBox`. The editor lets you add/drag/remove breakpoints and drag the range labels to rescale. When the editor closes (including mid-drag, handled in the destructor via `notifyChange()`), it fires `onCurveChanged` which calls `processor.updateHostDisplay()` to mark state dirty.

---

## Waveform Display — WaveformDisplay.h / WaveformDisplay.cpp

`WaveformDisplay : public juce::Component` renders the audio thumbnail (via JUCE's `AudioThumbnail`), a start (green) and end (red) triangle marker, a white playback cursor, and in slice mode — orange vertical slice lines with index labels. Mouse interactions: drag start/end markers to reposition, double-click to add a slice, right-click a slice to delete it, left-drag a slice to move it. All of these fire callbacks into `PluginEditor` which forwards them to the processor.

---

## Data Flow Summary

```
MIDI note-on
    |
    v
processBlock()
    +-- refreshBufs  ->  pick active slot(s)
    +-- analog jitter (wow + flutter)
    +-- route to engine:
    |       +-- Normal (resample)
    |       +-- OLA timestretch
    |       +-- WSOLA timestretch
    |       +-- Phase Vocoder
    |       +-- Granular (32-grain cloud)
    +-- applyResonator (biquad bank + delay)
    +-- slot volume gain  ->  output buffer

Timer (40ms)
    +-- update playback cursor
    +-- sync slice markers
    +-- toggle UI sections by mode
```

---

## File Map

| File | Purpose | Size |
|------|---------|------|
| `Source/PluginProcessor.h` | Audio engine header, all engine class definitions | 433 lines |
| `Source/PluginProcessor.cpp` | Audio engine, timestretch algos, resonator, granular | 1654 lines |
| `Source/PluginEditor.h` | UI component declarations | 145 lines |
| `Source/PluginEditor.cpp` | UI layout, DarkLAF, timer callback | 957 lines |
| `Source/CurveData.h` | BPMCurveData struct — piecewise-linear curve | 126 lines |
| `Source/CurveEditorComponent.h/.cpp` | Interactive curve editor UI | 352 lines |
| `Source/CurveKnob.h/.cpp` | Rotary knob + curve button component | 186 lines |
| `Source/WaveformDisplay.h/.cpp` | Waveform + slice marker display | 276 lines |
| `CMakeLists.txt` | JUCE CMake build config (VST3 + Standalone) | 56 lines |

---

## Key Concepts to Know

- **APVTS** (AudioProcessorValueTreeState): JUCE's parameter system. Every knob/toggle is registered here and automatically saved/restored with the preset.
- **Audio thread vs. message thread**: processBlock runs on a real-time thread with no allocations allowed. The UI runs on the message thread. Shared state uses atomics and a CriticalSection for slices.
- **Timestretch**: all three engines (OLA, WSOLA, PV) work by filling a circular input buffer from the file and pulling grains/frames out at a different rate. The ratio between input and output rates = the stretch factor.
- **Granular**: unlike timestretch, granular doesn't try to be coherent. It fires many short grains from a position cloud and lets the density/overlap create the texture.
- **Resonator**: not an effect pedal-style reverb — it's a filterbank tuned to harmonics of a root frequency. Pluck a sample through it and it "rings" those frequencies.
