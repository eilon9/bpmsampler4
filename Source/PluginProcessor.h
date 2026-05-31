#pragma once
#include <JuceHeader.h>
#include "CurveData.h"

//==============================================================================
// Audio-rate modulation
//==============================================================================
static constexpr int NUM_MOD_CHANNELS = 8;

enum SlotModTarget {
    kMod_Pitch       = 0,
    kMod_Speed       = 1,
    kMod_GranPos     = 2,
    kMod_GranPosJit  = 3,
    kMod_GrainJitter = 4,
    kMod_GranSize    = 5,
    kMod_GranDensity = 6,
    kMod_ScanLen     = 7,
    kMod_ScanSpd     = 8,
    kMod_ScanDep     = 9,
    kMod_StartPos    = 10,
    kNumSlotModTargets = 11
};
enum GlobalModTarget { kMod_ResoRoot = 0, kMod_ResoInharm = 1, kNumGlobalModTargets = 2 };

struct AudioModAssign {
    int   channel = -1;   // -1 = none, 0–7 = sidechain channel index
    float depth   = 1.0f; // ±1; sign inverts mod direction
};

// Precomputed mod sources passed to GranularEngine::process()
// Each buf pointer is null when the mod is inactive.
// Each scale = depth * physical_range  (precomputed in processSlotGranular).
struct GranModSources {
    const float* granPosBuf  = nullptr; double granPosScale  = 0.0; // sample offset
    const float* posJitBuf   = nullptr; double posJitScale   = 0.0; // sample offset
    const float* pitchJitBuf = nullptr; float  pitchJitScale = 0.0f;// semitones offset
    const float* granSizeBuf = nullptr; double granSizeScale = 0.0; // sample offset
    const float* densityBuf  = nullptr; double densityScale  = 0.0; // interval sample offset
    const float* scanLenBuf  = nullptr; double scanLenScale  = 0.0; // sample offset
    const float* scanSpdBuf  = nullptr; double scanSpdScale  = 0.0; // per-sample rate offset
    const float* scanDepBuf  = nullptr; float  scanDepScale  = 0.0f;// 0-1 offset
};

//==============================================================================
// Resonator filter bank helpers
//==============================================================================
struct BiquadState
{
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void reset() { x1 = x2 = y1 = y2 = 0.0f; }
};

struct BiquadCoeffs
{
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0;
    float gain = 0;
};

//==============================================================================
// Shared sample interpolation — cubic Catmull-Rom
//==============================================================================
inline float interpSample (const float* d, int n, double pos) noexcept
{
    if (n == 0) return 0.0f;
    const int   i0 = (int)pos;
    const float t  = (float)(pos - i0);
    auto s = [&](int i) -> float { return (i >= 0 && i < n) ? d[i] : 0.0f; };
    const float p0 = s(i0-1), p1 = s(i0), p2 = s(i0+1), p3 = s(i0+2);
    return p1 + 0.5f*t*((p2-p0) + t*((2.0f*p0 - 5.0f*p1 + 4.0f*p2 - p3) + t*(3.0f*(p1-p2)+p3-p0)));
}

//==============================================================================
// OLA timestretch
//==============================================================================
class OLATimestretch
{
public:
    static constexpr int GRAIN_SIZE = 2048;
    static constexpr int OVERLAP    = 4;
    static constexpr int HOP_SIZE   = GRAIN_SIZE / OVERLAP;
    static constexpr int BUF_SIZE   = GRAIN_SIZE * 8;

    void prepare (int numChannels);
    void reset   (double startSamplePos);

    bool process (const juce::AudioBuffer<float>& source,
                  juce::AudioBuffer<float>& output,
                  int numSamples,
                  double startSample, double endSample,
                  double speed, double srRatio,
                  double pitchFactor, float pitchJitter, bool loop);

    double getInputPosition() const { return inputPos; }

private:
    void  synthesizeGrain (const juce::AudioBuffer<float>& source,
                           double startSample, double endSample,
                           double srRatio, float pitchJitter, bool loop);
    float interpolate (const juce::AudioBuffer<float>& src, int ch, double pos) const;

    std::vector<std::vector<float>> buf;
    int    numCh     = 0;
    double readIdx   = 0.0;
    int    writeIdx  = 0;
    double available = 0.0;
    double inputPos  = 0.0;
    double inputHop  = HOP_SIZE;
    bool   ended     = false;
    std::array<float, GRAIN_SIZE> hanningWindow;
    juce::Random rng;
};

//==============================================================================
// WSOLA timestretch
//==============================================================================
class WSOLATimestretch
{
public:
    static constexpr int GRAIN_SIZE   = 2048;
    static constexpr int OVERLAP      = 4;
    static constexpr int HOP_SIZE     = GRAIN_SIZE / OVERLAP;
    static constexpr int BUF_SIZE     = GRAIN_SIZE * 8;
    static constexpr int SEARCH_RANGE = HOP_SIZE;
    static constexpr int CORR_LEN     = HOP_SIZE;

    void prepare (int numChannels);
    void reset   (double startSamplePos);

    bool process (const juce::AudioBuffer<float>& source,
                  juce::AudioBuffer<float>& output,
                  int numSamples,
                  double startSample, double endSample,
                  double speed, double srRatio,
                  double pitchFactor, float pitchJitter, bool loop);

    double getInputPosition() const { return inputPos; }

private:
    int   findBestOffset  (const juce::AudioBuffer<float>& source,
                           double startSample, double endSample) const;
    void  synthesizeGrain (const juce::AudioBuffer<float>& source,
                           double grainPos,
                           double startSample, double endSample,
                           double srRatio, float pitchJitter, bool loop);
    float interpolate     (const juce::AudioBuffer<float>& src, int ch, double pos) const;

    std::vector<std::vector<float>> buf;
    int    numCh        = 0;
    double readIdx      = 0.0;
    int    writeIdx     = 0;
    double available    = 0.0;
    double inputPos     = 0.0;
    double inputHop     = HOP_SIZE;
    bool   ended        = false;
    bool   firstGrain   = true;
    double lastGrainPos = 0.0;
    std::array<float, GRAIN_SIZE> hanningWindow;
    juce::Random rng;
};

//==============================================================================
// Phase Vocoder
//==============================================================================
class PhaseVocoder
{
public:
    static constexpr int FFT_ORDER = 11;
    static constexpr int FFT_SIZE  = 1 << FFT_ORDER;
    static constexpr int HOP_SIZE  = FFT_SIZE / 4;
    static constexpr int BUF_SIZE  = FFT_SIZE * 8;
    static constexpr int NUM_BINS  = FFT_SIZE / 2 + 1;

    void prepare (int numChannels);
    void reset   (double startSamplePos);

    bool process (const juce::AudioBuffer<float>& source,
                  juce::AudioBuffer<float>& output,
                  int numSamples,
                  double startSample, double endSample,
                  double speed, double srRatio,
                  double pitchFactor, float pitchJitter, bool loop);

    double getInputPosition() const { return inputPos; }

private:
    void  synthesizeFrame (const juce::AudioBuffer<float>& source,
                           double startSample, double endSample,
                           double srRatio, float pitchJitter, bool loop);
    float interpolate     (const juce::AudioBuffer<float>& src, int ch, double pos) const;

    juce::dsp::FFT fft { FFT_ORDER };
    std::array<float, FFT_SIZE>     analysisWindow;
    std::array<float, FFT_SIZE * 2> fftBuf;
    std::vector<std::vector<float>> lastPhase;
    std::vector<std::vector<float>> synthPhase;
    std::vector<std::vector<float>> outBuf;

    int    numCh      = 0;
    double readIdx    = 0.0;
    int    writeIdx   = 0;
    double available  = 0.0;
    double inputPos   = 0.0;
    double inputHop   = HOP_SIZE;
    bool   ended      = false;
    bool   firstFrame = true;
    juce::Random rng;
};

//==============================================================================
// Global analog output stage
//==============================================================================
class AnalogOutputStage
{
public:
    static constexpr int MAX_CH = 2;

    void prepare (double sampleRate)
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        preAlpha = 1.0f - std::exp (-twoPi * 3000.0f  / (float)sampleRate);
        airAlpha = 1.0f - std::exp (-twoPi * 20000.0f / (float)sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& s : preState) s = 0.0f;
        for (auto& s : deState)  s = 0.0f;
        for (auto& s : airState) s = 0.0f;
    }

    // drive=0 → air rolloff only (always-on DAC character);
    // drive>0 → adds even-harmonic coloration + frequency-dependent saturation.
    void process (juce::AudioBuffer<float>& buffer, float drive)
    {
        const int numCh = juce::jmin (buffer.getNumChannels(), MAX_CH);
        const int n     = buffer.getNumSamples();

        const float dcOffset  = 0.004f + drive * 0.055f; // even-harmonic floor; tiny at drive=0
        const float satInputG = 1.0f + drive * 0.5f;
        const float satNorm   = 1.0f / satInputG;
        const float preBoost  = drive * 0.65f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float x = data[i];

                // Pre-emphasis: boost highs into saturation
                preState[ch] += preAlpha * (x - preState[ch]);
                float xPre = x + preBoost * (x - preState[ch]);

                // Asymmetric saturation: DC offset before tanh → even harmonics.
                // At drive=0 the offset is tiny (~-48 dB 2nd harmonic at 0 dBFS).
                float sat = (std::tanh ((xPre + dcOffset) * satInputG)
                           - std::tanh (dcOffset * satInputG)) * satNorm;

                // Partial de-emphasis: net result is gentle presence peak
                deState[ch] += preAlpha * (sat - deState[ch]);
                float deEmph = sat - (preBoost * 0.5f) * (sat - deState[ch]);

                // Air rolloff: ~20 kHz 1-pole LP — always active regardless of drive
                airState[ch] += airAlpha * (deEmph - airState[ch]);

                data[i] = airState[ch];
            }
        }
    }

private:
    float preAlpha = 0.0f;
    float airAlpha = 0.0f;
    float preState[MAX_CH] = {};
    float deState[MAX_CH]  = {};
    float airState[MAX_CH] = {};
};

//==============================================================================
// LPC cross-synthesis morph engine
//==============================================================================
struct LPCMorphEngine
{
    static constexpr int LPC_ORDER  = 12;
    static constexpr int LPC_WINDOW = 512;

    // Rolling analysis windows (ch0 of each slot, last LPC_WINDOW samples)
    float analysisWinA[LPC_WINDOW] {};
    float analysisWinB[LPC_WINDOW] {};

    // Smoothed PARCOR + derived filter coefficients
    float parcorA[LPC_ORDER]    {};
    float parcorB[LPC_ORDER]    {};
    float lpcCoeffsA[LPC_ORDER] {};
    float lpcCoeffsB[LPC_ORDER] {};

    // Per-channel sample history for inverse filtering (most-recent-first)
    float histA[2][LPC_ORDER] {};
    float histB[2][LPC_ORDER] {};

    // IIR synthesis state per channel
    float synthesisState[2][LPC_ORDER] {};

    // Analysis window RMS (for excitation normalisation)
    float windowRmsA = 1e-6f;
    float windowRmsB = 1e-6f;

    // Adaptive output-gain correction per channel
    float lpcGainSmooth[2] = { 1.0f, 1.0f };

    void reset()
    {
        std::fill (analysisWinA, analysisWinA + LPC_WINDOW, 0.0f);
        std::fill (analysisWinB, analysisWinB + LPC_WINDOW, 0.0f);
        std::fill (parcorA,     parcorA     + LPC_ORDER, 0.0f);
        std::fill (parcorB,     parcorB     + LPC_ORDER, 0.0f);
        std::fill (lpcCoeffsA,  lpcCoeffsA  + LPC_ORDER, 0.0f);
        std::fill (lpcCoeffsB,  lpcCoeffsB  + LPC_ORDER, 0.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (histA[ch],          histA[ch]          + LPC_ORDER, 0.0f);
            std::fill (histB[ch],          histB[ch]          + LPC_ORDER, 0.0f);
            std::fill (synthesisState[ch], synthesisState[ch] + LPC_ORDER, 0.0f);
            lpcGainSmooth[ch] = 1.0f;
        }
        windowRmsA = 1e-6f;
        windowRmsB = 1e-6f;
    }

    // Levinson-Durbin LPC analysis on a LPC_WINDOW-sample signal.
    static void computeLPC (const float* x, float* coeffs, float* parcorOut);
    // Rebuild filter coefficients from PARCOR — stable by construction.
    static void stepUp     (const float* parcor, float* coeffs);

    // Call once per block: appends block output to rolling windows, re-analyses,
    // smooths PARCOR, and recomputes filter coefficients + window RMS.
    void updateCoeffs (const juce::AudioBuffer<float>& bufA,
                       const juce::AudioBuffer<float>& bufB,
                       int blockSize, double sampleRate);

    // Cross-synthesise bufA and bufB into outBuf (which is already clear).
    void process (const juce::AudioBuffer<float>& bufA,
                  const juce::AudioBuffer<float>& bufB,
                  juce::AudioBuffer<float>& outBuf,
                  float crossfade, int blockSize, double sampleRate);
};

//==============================================================================
// Granular engine
//==============================================================================
class GranularEngine
{
public:
    static constexpr int MAX_GRAINS = 32;

    void prepare (int numChannels) { numCh = numChannels; reset(); }

    double getScanPhase() const { return scanPhase; }

    void reset()
    {
        counter   = 0.0;
        scanPhase = 0.0;
        scanDir   = 1.0;
        for (auto& g : grains) g.active = false;
    }

    void process (const juce::AudioBuffer<float>& source,
                  juce::AudioBuffer<float>&       output,
                  int    numSamples,
                  double centerPos,
                  double posJitterSamps,
                  int    grainLenSamps,
                  double grainInterval,
                  double step,
                  float  pitchJitter,
                  float  probability,
                  double scanLenSamps,
                  double scanSpeedPerSample,
                  float  scanDepth,
                  const GranModSources& mod = GranModSources{})
    {
        const int totalSamples = source.getNumSamples();
        if (totalSamples == 0 || grainLenSamps < 2) return;

        const double expectedOverlap = (double)grainLenSamps / std::max (1.0, grainInterval);
        const float  norm  = 1.0f / (float)std::sqrt (std::max (1.0, expectedOverlap));
        const int    outCh = juce::jmin (output.getNumChannels(), numCh);

        const double posJitterNorm   = posJitterSamps / std::max (1.0, (double)grainLenSamps * 0.5);
        const double pitchJitterNorm = (double)pitchJitter / 12.0;
        const double timingVar = juce::jlimit (0.0, 1.0, std::max (posJitterNorm, pitchJitterNorm));

        const double maxInterval = (double)totalSamples;
        const int    maxGrainLen = totalSamples / 2;

        for (int i = 0; i < numSamples; ++i)
        {
            // Per-sample scan param mod
            const double effScanLen = scanLenBuf_apply (scanLenSamps, mod.scanLenBuf, mod.scanLenScale, i, 0.0, (double)(totalSamples - 1));
            const float  effScanDep = scanDepBuf_apply (scanDepth,    mod.scanDepBuf, mod.scanDepScale, i, 0.0f, 1.0f);
            const double effScanSpd = scanLenBuf_apply (scanSpeedPerSample, mod.scanSpdBuf, mod.scanSpdScale, i, 0.0, 100.0);

            const double scanOffset   = scanPhase * (double)effScanDep * effScanLen;
            const double scannedCenter = juce::jlimit (0.0, (double)(totalSamples - 1),
                                                       centerPos + scanOffset);

            counter -= 1.0;
            if (counter <= 0.0)
            {
                // Per-grain mod applied at spawn
                const double effCenter  = mod.granPosBuf  ? juce::jlimit (0.0, (double)(totalSamples-1), scannedCenter + (double)mod.granPosBuf[i]  * mod.granPosScale)  : scannedCenter;
                const double effPosJit  = mod.posJitBuf   ? juce::jmax (0.0, posJitterSamps + (double)mod.posJitBuf[i]  * mod.posJitScale)   : posJitterSamps;
                const float  effPitJit  = mod.pitchJitBuf ? juce::jlimit (0.0f, 48.0f, pitchJitter + mod.pitchJitBuf[i] * mod.pitchJitScale) : pitchJitter;
                const int    effGrnLen  = mod.granSizeBuf ? juce::jlimit (64, maxGrainLen, (int)((double)grainLenSamps + (double)mod.granSizeBuf[i] * mod.granSizeScale))  : grainLenSamps;
                const double effInterval= mod.densityBuf  ? juce::jlimit (1.0, maxInterval, grainInterval + (double)mod.densityBuf[i] * mod.densityScale) : grainInterval;

                const double expDraw = -std::log (std::max (rng.nextDouble(), 1e-10));
                counter += (1.0 - timingVar + expDraw * timingVar) * effInterval;
                if (probability >= 1.0f || rng.nextFloat() < probability)
                {
                    for (int g = 0; g < MAX_GRAINS; ++g)
                    {
                        if (!grains[g].active)
                        {
                            double pos = effCenter + nextGaussian() * effPosJit * 0.5;
                            pos = juce::jlimit (0.0, (double)(totalSamples - 1), pos);
                            double pitchMult = 1.0;
                            if (effPitJit > 0.0f)
                                pitchMult = std::pow (2.0,
                                    nextGaussian() * (double)effPitJit * 0.5 / 12.0);
                            grains[g] = { true, pos, step * pitchMult, 0, effGrnLen };
                            break;
                        }
                    }
                }
            }
            for (int g = 0; g < MAX_GRAINS; ++g)
            {
                if (!grains[g].active) continue;
                const float env = 0.5f * (1.0f - std::cos (
                    juce::MathConstants<float>::twoPi * (float)grains[g].age
                    / (float)(grains[g].len - 1)));
                for (int ch = 0; ch < outCh; ++ch)
                    output.addSample (ch, i, interpolate (source, ch, grains[g].pos) * env * norm);
                grains[g].pos += grains[g].step;
                if (++grains[g].age >= grains[g].len)
                    grains[g].active = false;
            }

            scanPhase += effScanSpd * scanDir;
            if (scanPhase >= 1.0) { scanPhase = 2.0 - scanPhase; scanDir = -1.0; }
            else if (scanPhase < 0.0) { scanPhase = -scanPhase; scanDir =  1.0; }
        }
    }

private:
    struct Grain { bool active=false; double pos=0.0; double step=1.0; int age=0; int len=0; };

    double nextGaussian()
    {
        // Box-Muller transform — standard normal N(0,1)
        const double u1 = std::max (rng.nextDouble(), 1e-10);
        const double u2 = rng.nextDouble();
        return std::sqrt (-2.0 * std::log (u1))
               * std::cos (juce::MathConstants<double>::twoPi * u2);
    }

    float interpolate (const juce::AudioBuffer<float>& src, int ch, double pos) const
    {
        const int    n = src.getNumSamples();
        const float* d = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
        return interpSample (d, n, pos);
    }

    // Helpers used inside process() to apply per-sample mod to scan params
    static double scanLenBuf_apply (double base, const float* buf, double scale, int i, double lo, double hi) noexcept
    {
        if (!buf) return base;
        return juce::jlimit (lo, hi, base + (double)buf[i] * scale);
    }
    static float scanDepBuf_apply (float base, const float* buf, float scale, int i, float lo, float hi) noexcept
    {
        if (!buf) return base;
        return juce::jlimit (lo, hi, base + buf[i] * scale);
    }

    Grain        grains[MAX_GRAINS] = {};
    int          numCh      = 0;
    double       counter    = 0.0;
    double       scanPhase  = 0.0;
    double       scanDir    = 1.0;
    juce::Random rng;
};

//==============================================================================
// Per-voice playback state (one per polyphony voice, owned by SampleSlot)
//==============================================================================
struct VoiceState
{
    std::atomic<bool>   active   { false };
    bool                noteHeld { false };
    int                 midiNote { 60 };
    double              midiNotePitchSemitones { 0.0 };
    int                 triggerAge { 0 };  // lower = older; used for voice stealing

    double              playbackPos { 0.0 };
    std::atomic<double> playbackPosAtom { 0.0 };

    OLATimestretch      olaEngine;
    WSOLATimestretch    wsolaEngine;
    PhaseVocoder        pvEngine;
    GranularEngine      granEngine;

    float jitterWowState     { 0.0f };
    float jitterFlutterState { 0.0f };
    juce::Random jitterRng;
    float currentJitterFactor { 1.0f };

    enum class EnvPhase { Off, Attack, Decay, Sustain, Release };
    EnvPhase envPhase { EnvPhase::Off };
    float    envValue { 0.0f };
};

//==============================================================================
// Per-slot sampler instance — owns audio data, curves, and a pool of voices.
//==============================================================================
struct SampleSlot
{
    SampleSlot() = default;

    // Audio data
    juce::AudioBuffer<float> buffer;
    double       sampleRate = 44100.0;
    juce::String filePath;
    int loNote = -1;
    int hiNote = -1;

    // Curves
    BPMCurveData pitchCurve;
    BPMCurveData speedCurve;
    BPMCurveData startPosCurve;
    BPMCurveData granPosCurve;

    // Slices
    juce::CriticalSection sliceLock;
    std::vector<float>    slicePositions;

    // Polyphonic voice pool
    static constexpr int MAX_POLY_VOICES = 8;
    VoiceState voices[MAX_POLY_VOICES];

    // Character filters (applied once on summed voice output)
    float hissHpState[2] = { 0.0f, 0.0f };
    juce::Random hissRng;
    float satPreLp[2]    = { 0.0f, 0.0f };
    float satDeLp[2]     = { 0.0f, 0.0f };

    // Resonator (applied once on summed voice output)
    static constexpr int MAX_RES_HARMONICS = 32;
    static constexpr int RES_DELAY_SIZE    = 8192;
    BiquadState  resoStates[2][MAX_RES_HARMONICS];
    BiquadCoeffs resoCoeffs[MAX_RES_HARMONICS];
    float        resoDelayBuf[2][RES_DELAY_SIZE] = {};
    int          resoDelayWrite[2] = { 0, 0 };
    int          resoDelaySamps    = 0;

    bool hasFile() const { return buffer.getNumSamples() > 0; }

    bool hasAnyActiveVoice() const
    {
        for (const auto& v : voices) if (v.active.load()) return true;
        return false;
    }

    JUCE_DECLARE_NON_COPYABLE (SampleSlot)
};

//==============================================================================
class BPMSamplerProcessor : public juce::AudioProcessor
{
public:
    BPMSamplerProcessor();
    ~BPMSamplerProcessor() override;

    void prepareToPlay   (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock    (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BPMSampler4"; }
    bool  acceptsMidi()  const override { return true;  }
    bool  producesMidi() const override { return false; }
    bool  isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()                             override { return 1; }
    int  getCurrentProgram()                          override { return 0; }
    void setCurrentProgram (int)                      override {}
    const juce::String getProgramName (int)           override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    bool loadAudioFileToSlot (const juce::File& file, int slotIndex);
    void setSlotNoteRange    (int slotIndex, int lo, int hi);

    // Load to whichever slot is currently selected in the editor
    bool loadAudioFile (const juce::File& file) { return loadAudioFileToSlot (file, selectedSlot.load()); }

    double getPlaybackPositionNorm (int slotIdx) const;
    bool   getIsPlaying            (int slotIdx) const { return slots[slotIdx].hasAnyActiveVoice(); }

    // Returns peak level of mod channel ch over the last block (0 if not connected)
    float getModChannelLevel (int ch) const
    {
        if (ch < 0 || ch >= modChanCount || modChanPtrs[ch] == nullptr) return 0.0f;
        return modChannelLevels[ch].load();
    }
    // Returns instantaneous (last-sample) value of mod channel ch (0 if not connected)
    float getModChannelInstValue (int ch) const
    {
        if (ch < 0 || ch >= modChanCount) return 0.0f;
        return modChannelInstVals[ch].load();
    }
    int   getModChannelCount() const { return modChanCount; }

    void addSlice         (int slotIdx, float normPos);
    void removeSliceAt    (int slotIdx, int index);
    void clearSlices      (int slotIdx);
    void moveSlice        (int slotIdx, float fromPos, float toPos);
    void detectTransients (int slotIdx, float threshold = 0.3f);

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioThumbnailCache thumbnailCache { 10 };
    std::vector<std::unique_ptr<juce::AudioThumbnail>> slotThumbnails;

    // Morph display state (audio thread → GUI)
    std::atomic<double> morphDisplayPosA { 0.0 };
    std::atomic<double> morphDisplayPosB { 0.0 };
    std::atomic<float>  morphDisplayFrac { 0.0f };
    std::atomic<int>    morphDisplaySa   { 0 };
    std::atomic<int>    morphDisplaySb   { 1 };

    std::function<void(int)> onSlotLoaded;

    static constexpr int NUM_SLOTS = 8;
    SampleSlot slots[NUM_SLOTS];

    // Audio-rate modulation assignments (editor reads/writes; audio thread reads via transient pointers below)
    AudioModAssign slotMod  [NUM_SLOTS][kNumSlotModTargets] = {};
    AudioModAssign globalMod[kNumGlobalModTargets]          = {};

    // Set by the editor when the user clicks a slot; read in Single mode
    std::atomic<int> selectedSlot { 0 };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::String sp (int slotIdx, const char* name) const
    {
        return "s" + juce::String (slotIdx) + "_" + name;
    }

    struct SlotParamPtrs
    {
        std::atomic<float>* startPos     = nullptr;
        std::atomic<float>* endPos       = nullptr;
        std::atomic<float>*      speed      = nullptr;
        std::atomic<float>*      pitch      = nullptr;
        juce::RangedAudioParameter* speedParam = nullptr;
        juce::RangedAudioParameter* pitchParam = nullptr;
        std::atomic<float>* loop         = nullptr;
        std::atomic<float>* gate         = nullptr;
        std::atomic<float>* bpmSync      = nullptr;
        std::atomic<float>* numBeats     = nullptr;
        std::atomic<float>* speedSync    = nullptr;
        std::atomic<float>* speedFine    = nullptr;
        std::atomic<float>* tsMode       = nullptr;
        std::atomic<float>* sliceMode    = nullptr;
        std::atomic<float>* sliceIndex   = nullptr;
        std::atomic<float>* granEnabled  = nullptr;
        std::atomic<float>* granPos      = nullptr;
        std::atomic<float>* granPosJit   = nullptr;
        std::atomic<float>* granDensity  = nullptr;
        std::atomic<float>* granSize     = nullptr;
        std::atomic<float>* grainJitter  = nullptr;
        std::atomic<float>* analogJitter = nullptr;
        std::atomic<float>* granScanLen  = nullptr;
        std::atomic<float>* granScanSpd  = nullptr;
        std::atomic<float>* granScanDep  = nullptr;
        std::atomic<float>* granProb     = nullptr;
        std::atomic<float>* midiNoteMode = nullptr;
        std::atomic<float>* vol          = nullptr;
        std::atomic<float>* envEnabled   = nullptr;
        std::atomic<float>* envAttack    = nullptr;
        std::atomic<float>* envDecay     = nullptr;
        std::atomic<float>* envSustain   = nullptr;
        std::atomic<float>* envRelease   = nullptr;
    };
    SlotParamPtrs slotPtrs[NUM_SLOTS];

    double calculateEffectiveSpeed (int slotIdx, double baseSpeed, double fileDurSecs) const;
    double snapToMusicalSpeed      (double speed) const;

    void startSlotPlayback (int slotIdx, int noteNum);
    void stopNoteInSlot    (int slotIdx, int noteNum);
    void processOneSlot    (int slotIdx, juce::AudioBuffer<float>& outBuf);

    void processSlotNormal (SampleSlot& slot, VoiceState& voice, int slotIdx,
                            juce::AudioBuffer<float>& buffer,
                            int numSamples, double startSample, double endSample,
                            double baseSpeed, double srRatio, double jitterFactor,
                            double basePitchSemitones, bool loop);

    void processSlotTimestretch (SampleSlot& slot, VoiceState& voice, int slotIdx,
                                 juce::AudioBuffer<float>& buffer,
                                 int numSamples, double startSample, double endSample,
                                 double speed, double srRatio, bool loop, int tsMode);

    void processSlotGranular (SampleSlot& slot, VoiceState& voice, int slotIdx,
                              juce::AudioBuffer<float>& buffer,
                              int numSamples, double startSample, double endSample);

    void updateVoiceJitter    (SampleSlot& slot, VoiceState& voice, int slotIdx);
    void updateSlotResoCoeffs (SampleSlot& slot, float root, float q, int numH,
                               float taper, float inharm, float qTaper,
                               int seriesMode, float decaySecs, float scatter);
    void applySlotResonator   (SampleSlot& slot, juce::AudioBuffer<float>& buffer, int slotIdx);
    void applySlotCharacter   (SampleSlot& slot, juce::AudioBuffer<float>& buffer, int slotIdx);

    // MIDI 14-bit CC mod sources (CC 1-8 = MSB, CC 33-40 = LSB → slots 0-7)
    uint8_t ccMSB[NUM_MOD_CHANNELS] = {};
    uint8_t ccLSB[NUM_MOD_CHANNELS] = {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midiMod[NUM_MOD_CHANNELS];

    // Mod signal storage: filled per-block from midiMod SmoothedValues
    juce::AudioBuffer<float> modBuffer;
    const float* modChanPtrs[NUM_MOD_CHANNELS] = {};
    int          modChanCount = 0;
    std::atomic<float> modChannelLevels[NUM_MOD_CHANNELS]   = {};
    std::atomic<float> modChannelInstVals[NUM_MOD_CHANNELS] = {};

    // Returns base val shifted by mod signal * scale, clamped to [lo, hi]
    static float applyMod (float base, const AudioModAssign& a,
                           const float* const* chans, int numCh,
                           int sampleIdx, float lo, float hi, float range) noexcept
    {
        if (a.channel < 0 || a.channel >= numCh || chans[a.channel] == nullptr)
            return base;
        return juce::jlimit (lo, hi, base + chans[a.channel][sampleIdx] * a.depth * range);
    }

    juce::AudioFormatManager formatManager;

    std::atomic<int>   activeSlot    { 0 };

    int  voiceAgeCounter = 0;
    juce::AudioBuffer<float> voiceMixBuffer;

    float morphSlotGain[NUM_SLOTS] = {};
    juce::AudioBuffer<float> morphTempBuffer;

    int            lpcLastSa = -1;
    int            lpcLastSb = -1;
    LPCMorphEngine lpcMorph;
    juce::AudioBuffer<float> lpcBufA, lpcBufB;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;

    AnalogOutputStage globalOutputStage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BPMSamplerProcessor)
};
