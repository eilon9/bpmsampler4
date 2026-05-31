# BPM Sampler — Claude Code Reference

## Project
JUCE 7 VST3/Standalone instrument plugin. CMake build. C++17. MIDI-triggered sample player with BPM sync and timestretch.

## Files
```
CMakeLists.txt          — JUCE CMake plugin setup (formats: VST3, Standalone)
Source/PluginProcessor.h/.cpp  — audio engine, APVTS, timestretch engines
Source/PluginEditor.h/.cpp     — UI (dark theme, DarkLAF)
Source/WaveformDisplay.h/.cpp  — AudioThumbnail waveform + slice marker component
Source/CurveData.h             — BPMCurveData struct (points, displayMin/Max, absMin/Max)
Source/CurveKnob.h/.cpp        — Rotary knob + "~" button that opens CurveEditorComponent
Source/CurveEditorComponent.h/.cpp — Breakpoint curve editor shown in a CallOutBox
JUCE/                   — git submodule (JUCE 7)
build/                  — CMake build output (gitignored)
```

## APVTS Parameters
| ID | Type | Range | Default |
|----|------|-------|---------|
| startPos | Float | 0–1 | 0 |
| endPos | Float | 0–1 | 1 |
| speed | Float | 0.25–4.0 | 1.0 |
| loopEnabled | Bool | — | false |
| bpmSync | Bool | — | false |
| numBeats | Float | 1–64 | 4 |
| tsMode | Choice | Off/OLA/WSOLA/Phase Vocoder | Off |
| speedSync | Bool | — | false |
| pitch | Float | -24–+24 semitones | 0 |
| sliceMode | Bool | — | false |
| sliceIndex | Int | 0–63 | 0 |
| granEnabled | Bool | — | false |
| granPos | Float | 0–1 | 0.5 |
| granPosJitter | Float | 0–1 | 0.1 |
| granDensity | Float | 1–100 grains/sec | 10 |
| granSize | Float | 10–500 ms | 80 |
| grainJitter | Float | 0–12 semitones | 0 |
| analogJitter | Float | 0–1 | 0 |

## Timestretch Engines (all in PluginProcessor.h)
- `OLATimestretch` — basic overlap-add, GRAIN_SIZE=2048, OVERLAP=4
- `WSOLATimestretch` — cross-correlation grain alignment, SEARCH_RANGE=HOP_SIZE
- `PhaseVocoder` — FFT_ORDER=11 (2048pt), HOP=512, 75% overlap; independent pitch shift via resample

## Granular Engine (PluginProcessor.h:175)
- `GranularEngine` — grain pool, up to MAX_GRAINS active simultaneously; each grain has its own envelope, read position, and pitch offset
- Mutually exclusive with timestretch modes; activated by `granEnabled` toggle
- `granPos` sets center position in the file; `granPosJitter` randomizes per-grain start within that region
- `granDensity` controls grains/sec; `granSize` controls grain length in ms
- `grainJitter` randomizes pitch per grain (semitones); `analogJitter` adds subtle drift
- UI: toggling granular swaps knob layout — cols 0/1/2/4 show gran-specific controls instead of regular ones
- Processed in `BPMSamplerProcessor::processGranular()` (PluginProcessor.cpp:1073)

## Slice System
- `slicePositions` (normalized 0–1) stored in `BPMSamplerProcessor`, protected by `sliceLock`
- Saved/loaded as XML child tree `"Slices"` — **must be extracted before `apvts.replaceState()`** or it will be overwritten
- WaveformDisplay handles mouse: double-click=add, right-click=remove, left-drag=move slice
- "Detect Transients" button runs RMS energy flux + adaptive threshold

## Slice Mode Playback Logic
- Loop OFF: play exactly slice N (sliceStart → sliceEnd), stop
- Loop ON: loop whole file [0, totalSamples]; sliceIndex sets phase-offset entry point on note-on; wraps at file end

## Curve System
- Four `BPMCurveData` members on the processor: `pitchCurve`, `speedCurve`, `startPosCurve`, `granPosCurve`
- `CurveKnob` wraps a `RangedAudioParameter&` + `BPMCurveData&`; the knob maps param norm → display range via `curveData.applyToNorm()`
- `CurveEditorComponent` takes `BPMCurveData&` by reference and modifies the processor's data directly; launched in a `CallOutBox` with `nullptr` parent (top-level, non-modal)
- Whenever curves change, `CurveKnob::applyRangeUpdate()` fires `onCurveChanged` → `processor.updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true))` so the host marks state dirty
- **CallOutBox close without mouseUp**: if the user clicks outside to dismiss the editor mid-drag, `mouseUp` never fires. The `CurveEditorComponent` destructor calls `notifyChange()` to flush any pending drag. Adding a new point also calls `notifyChange()` immediately in `mouseDown`.

## State Save/Load Pattern
Slices and curves are saved as XML children alongside the APVTS state. Key rules:

- **Slices**: saved as `"Slices"` ValueTree child; must be extracted **before** `apvts.replaceState()` or it gets overwritten — already correctly implemented
- **Curves**: saved as direct XML children (`"PitchCurve"`, `"SpeedCurve"`, `"StartPosCurve"`, `"GranPosCurve"`) of the root element after `state.createXml()`
- In `getStateInformation`: **remove stale curve children before writing** — `ValueTree::fromXml` on a previous load would have embedded them into the APVTS state, causing duplicates on the next save
- In `setStateInformation`: load curves from XML **before** `apvts.replaceState()`; iterate to find the **last** matching element (not first) to handle legacy files that may have duplicates; remove curve children from the ValueTree before passing to `replaceState()`

## Multi-sample System
- 8 `SampleSlot` entries in `BPMSamplerProcessor::slots[NUM_SLOTS]`
- Each slot has: `buffer`, `sampleRate`, `filePath`, `loNote`/`hiNote` (MIDI note range, -1 = unassigned), `tsMode` (int, -1=use global, 0=Off, 1=OLA, 2=WSOLA, 3=PV)
- **Per-slot pitch/speed/volume are APVTS params**: `slot{i}Pitch` (−24–+24 st), `slot{i}Spd` (0.1–4.0×), `slot{i}Vol` (0–2 linear). Exposed to host automation and MIDI CC in Reaper. Audio thread reads via cached `slotPitchPtrs[i]`/`slotSpdPtrs[i]`/`slotVolPtrs[i]` atomics (set in constructor).
- Three modes via `sampleMode` APVTS param: 0=Single, 1=MIDI Map, 2=Morph
- **MIDI Map**: note-on scans slots to find one whose `loNote`/`hiNote` contains the note
- **Morph**: `morphPos` param (0–7) crossfades between adjacent slots
- **Per-slot tsMode**: `-1` = inherit global `tsMode` param; `0`=Off, `1`=OLA, `2`=WSOLA, `3`=PV
- Side panel (PANEL_W=310) shows per-slot rows: index | file button | Lo | Hi | Pitch | Spd | Vol | TS
- Slot data saved/loaded as `"SampleSlot"` ValueTree children with properties: `index`, `filePath`, `loNote`, `hiNote`, `tsMode`. Pitch/Spd/Vol are in APVTS state.

## Polyphony System
- Global `polyVoices` APVTS param (Int 1–8, default 1); UI ComboBox "1 Voice" … "8 Voices" in the sample bar
- Each `SampleSlot` holds `VoiceState voices[MAX_POLY_VOICES]` (MAX_POLY_VOICES=8)
- `VoiceState` contains: `active` (atomic bool), `noteHeld`, `midiNote`, `midiNotePitchSemitones`, `triggerAge` (for voice stealing), `playbackPos`, `playbackPosAtom`, per-voice TS engines (OLA/WSOLA/PV), granular engine, jitter state
- `startSlotPlayback`: finds a free voice within [0..maxVoices); if all full, steals lowest `triggerAge` (oldest voice). `voiceAgeCounter` is a monotonically incrementing int assigned to each trigger.
- `processOneSlot`: iterates all voices, processes each into `voiceMixBuffer`, adds to `outBuf`. Resonator and character filters applied once on the summed output.
- Note-off matched by note number to the oldest active voice with that note (`stopNoteInSlot`). Loop-off: voice stops immediately; loop-on: voice continues until file end.
- `getIsPlaying(slotIdx)` → `hasAnyActiveVoice()`; `getPlaybackPositionNorm` returns position of most recently triggered active voice (highest triggerAge).

## Morph Mode Architecture

Each slot has its own independent voice pool. Every audio block, the two active slots (sa and sb, derived from morphPos) are each processed via `processOneSlot` into `morphTempBuffer` and volume-crossfaded into `buffer`. The morph slider is purely a volume crossfader. The morph display uses `getPlaybackPositionNorm` per slot.

## Resonator Parameters (full list)
| ID | Range | Default | Notes |
|----|-------|---------|-------|
| resoEnabled | Bool | false | |
| resoRoot | 20–2000 Hz | 220 | log scale |
| resoQ | 0.5–500 | 5 | per-harmonic via resoQTaper |
| resoDecay | 0.01–5 s | 0.5 | ring time; higher harmonics decay at 1/(h+1) rate |
| resoTaper | 0–2 | 0.5 | amplitude rolloff across harmonics |
| resoQTaper | -1–1 | 0 | Q scaling per harmonic |
| resoInharm | 0–0.5 | 0 | systematic stretch of partial frequencies |
| resoDrive | 0–1 | 0 | soft nonlinear excitation (tanh waveshaper on dry before filterbank) |
| resoScatter | 0–2 % | 0 | per-harmonic static frequency nudge (Knuth hash); breaks perfect comb |
| resoTime | 0.5–50 ms | 10 | delay send time |
| resoFeedback | 0–0.95 | 0.3 | delay self-feedback |
| resoMix | 0–1 | 0.5 | wet/dry mix |
| resoHarmonics | 8/16/32 | 8 | number of active partials |
| resoSeries | All/Odd/Even | All | harmonic series filter |

## Resonator Known Issues / In Progress

### State buildup during root modulation (FIXED)
When root is modulated while a repeating sequence runs, the biquad state (y1/y2) accumulated energy coherently. Fixed by applying `std::tanh` **inside** the per-harmonic biquad loop (wrapping the entire y0 computation) so state is always bounded to ±1. Applying tanh only to the output does not work — the nonlinearity must be inside the feedback loop.

### Volume inconsistency across Decay range (FIXED)
Root cause: `b0 = peakGain * (1-r_h) * 0.5` makes b0 ~500x smaller at 5s decay than at 0.01s, so for percussive inputs the long-decay filter barely builds up during the hit and rings very quietly. A clamp at `1e-4f` helped but left a ~21 dB gap (measured as ~25 dB perceptually).

**Fix**: `b0` and filter character are untouched (preserves root-mod artifact suppression). A per-harmonic `compFactor` is multiplied into `resoCoeffs[h].gain` in `updateResoCoeffs`. The reference is `halfBW` at 0.01 s decay for that harmonic; `compFactor = min(halfBW_ref / halfBW_actual, 8.0)`. Capped at 8× (~18 dB) to limit clipping on sustained resonant tones; residual gap after the fix ≈ 3 dB.

## 14-bit MIDI CC Modulation System

Replaces the old audio-rate sidechain mod. External sources (e.g. MSEG Mod VST3) send 14-bit CC pairs; the sampler decodes them and routes them to mod targets.

### CC Mapping
- CC 1–8 = MSB for mod slots 0–7
- CC 33–40 = LSB for mod slots 0–7 (update fires on LSB arrival)
- Norm value = `(MSB<<7 | LSB) / 16383.0f`
- `juce::SmoothedValue<float, Linear>` per slot, 5ms ramp → fills `modBuffer` per-sample each block

### Mod Targets
| Enum | Target | Path | Range |
|------|--------|------|-------|
| `kMod_Pitch` | pitch semitones | per-sample (normal), block-rate (TS + granular) | ±48 st |
| `kMod_Speed` | playback speed | per-sample (normal), block-rate (TS) | +3.75× |
| `kMod_StartPos` | start position | block-rate in `processOneSlot` + note-on in `startSlotPlayback` | 0–1 |
| `kMod_GranPos` | granular center pos | per-sample | 0–1 |
| `kMod_GranPosJit` | granular pos jitter | per-sample | 0–1 |
| `kMod_GrainJitter` | grain pitch jitter | per-sample | 0–12 st |
| `kMod_GranSize` | grain size | per-sample | 10–500 ms |
| `kMod_GranDensity` | grain density | per-sample | 1–100 /s |
| `kMod_ScanLen` | scan length | per-sample | — |
| `kMod_ScanSpd` | scan speed | per-sample | — |
| `kMod_ScanDep` | scan depth | per-sample | — |
| `kMod_ResoRoot` | resonator root (global) | block-rate | — |
| `kMod_ResoInharm` | resonator inharmonicity (global) | block-rate | — |

### Key Implementation Details
- `AudioModAssign` struct: `channel` (−1=none, 0–7) + `depth` (±1). **Default depth = 1.0** so assignments are active immediately.
- `applyMod(base, assign, chanPtrs, numCh, sampleIdx, lo, hi, range)` → `clamp(base + chan[sampleIdx] * depth * range, lo, hi)`
- `modChanPtrs[i]` set each block from `modBuffer.getWritePointer(i)` after SmoothedValue fill
- Right-click any CurveKnob or slider to open `ModAssignPanel` — shows live signal dots per channel, depth slider

### startPos mod — note-on timing
`startSlotPlayback` reads `midiMod[channel].getCurrentValue()` (end-of-previous-block smoothed value) and applies mod to `voice.playbackPos` at trigger time. This is necessary because the mod buffer isn't filled yet when the MIDI loop runs.

### Pitch mod — processing path coverage
- **Normal path** (`processSlotNormal`): per-sample mod via `hasMod` / mod path
- **Timestretch path** (`processSlotTimestretch`): block-rate mod applied to `semitones` before `pitchFactor` computation
- **Granular path** (`processSlotGranular`): block-rate mod applied to `pitchSemitones` before `pitchFactor` computation

### File-end declick
When loop=OFF and voice hits `endSample` at sample `i` in `processSlotNormal`, a 64-sample fade is applied backward from sample `i` to `i−63` (gain = f/64) before deactivating the voice. Applied in both fast path and mod path.

## Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Output: `build/BPMSampler4_artefacts/Release/VST3/BPMSampler4.vst3`
