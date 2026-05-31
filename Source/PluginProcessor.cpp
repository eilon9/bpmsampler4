#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// OLATimestretch
//==============================================================================

void OLATimestretch::prepare (int numChannels)
{
    numCh = numChannels;
    buf.assign (numChannels, std::vector<float> (BUF_SIZE, 0.0f));
    for (int i = 0; i < GRAIN_SIZE; ++i)
        hanningWindow[i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                      * (float)i / (float)(GRAIN_SIZE - 1)));
}

void OLATimestretch::reset (double startPos)
{
    for (auto& ch : buf) std::fill (ch.begin(), ch.end(), 0.0f);
    readIdx = writeIdx = 0;
    available = 0;
    inputPos  = startPos;
    ended     = false;
}

float OLATimestretch::interpolate (const juce::AudioBuffer<float>& src, int ch, double pos) const
{
    const int    n = src.getNumSamples();
    const float* d = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
    return interpSample (d, n, pos);
}

void OLATimestretch::synthesizeGrain (const juce::AudioBuffer<float>& source,
                                       double startSample, double endSample,
                                       double srRatio, float pitchJitter, bool loop)
{
    const double range = endSample - startSample;
    if (pitchJitter > 0.0f)
        srRatio *= std::pow (2.0, (double)((rng.nextFloat() * 2.0f - 1.0f) * pitchJitter) / 12.0);

    for (int g = 0; g < GRAIN_SIZE; ++g)
    {
        double pos = inputPos + (double)g * srRatio;
        if (loop && range > 0.0)
        {
            while (pos >= endSample)   pos -= range;
            while (pos <  startSample) pos += range;
        }
        const int idx = (writeIdx + g) % BUF_SIZE;
        for (int ch = 0; ch < numCh; ++ch)
            buf[ch][idx] += interpolate (source, ch, pos) * hanningWindow[g];
    }
    writeIdx  = (writeIdx + HOP_SIZE) % BUF_SIZE;
    inputPos += inputHop;
    available += HOP_SIZE;

    if (loop && range > 0.0) { while (inputPos >= endSample) inputPos -= range; while (inputPos < startSample) inputPos += range; }
    else if (!loop && inputPos >= endSample) ended = true;
}

bool OLATimestretch::process (const juce::AudioBuffer<float>& source,
                               juce::AudioBuffer<float>& output,
                               int numSamples, double startSample, double endSample,
                               double speed, double srRatio, double pitchFactor,
                               float pitchJitter, bool loop)
{
    pitchFactor = juce::jlimit (0.1, 8.0, pitchFactor);
    inputHop = (double)HOP_SIZE * speed * srRatio / pitchFactor;
    const double needed = (double)numSamples * pitchFactor + HOP_SIZE;
    while (available < needed && !ended)
        synthesizeGrain (source, startSample, endSample, srRatio, pitchJitter, loop);

    int lastCleared = (int)readIdx;
    for (int i = 0; i < numSamples; ++i)
    {
        const int r0 = (int)readIdx % BUF_SIZE, r1 = (r0+1) % BUF_SIZE;
        const float frac = (float)(readIdx - std::floor (readIdx));
        for (int ch = 0; ch < numCh; ++ch)
            output.setSample (ch, i, buf[ch][r0] * (1.0f - frac) + buf[ch][r1] * frac);
        readIdx  += pitchFactor;
        available = juce::jmax (0.0, available - pitchFactor);
        const int newInt = (int)readIdx;
        for (int j = lastCleared; j < newInt; ++j)
            for (int ch = 0; ch < numCh; ++ch)
                buf[ch][j % BUF_SIZE] = 0.0f;
        lastCleared = newInt;
    }
    return !(ended && available <= 0.0);
}

//==============================================================================
// WSOLATimestretch
//==============================================================================

void WSOLATimestretch::prepare (int numChannels)
{
    numCh = numChannels;
    buf.assign (numChannels, std::vector<float> (BUF_SIZE, 0.0f));
    for (int i = 0; i < GRAIN_SIZE; ++i)
        hanningWindow[i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                      * (float)i / (float)(GRAIN_SIZE - 1)));
}

void WSOLATimestretch::reset (double startPos)
{
    for (auto& ch : buf) std::fill (ch.begin(), ch.end(), 0.0f);
    readIdx = writeIdx = 0; available = 0; inputPos = startPos;
    inputHop = HOP_SIZE; ended = false; firstGrain = true; lastGrainPos = startPos;
}

float WSOLATimestretch::interpolate (const juce::AudioBuffer<float>& src, int ch, double pos) const
{
    const int    n = src.getNumSamples();
    const float* d = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
    return interpSample (d, n, pos);
}

int WSOLATimestretch::findBestOffset (const juce::AudioBuffer<float>& source,
                                       double startSample, double endSample) const
{
    if (firstGrain) return 0;
    const int n = source.getNumSamples();
    const double refBase = lastGrainPos + inputHop;
    const float* srcData = source.getReadPointer (0);
    int bestOffset = 0; float bestCorr = -std::numeric_limits<float>::max();
    for (int offset = -SEARCH_RANGE; offset <= SEARCH_RANGE; offset += 2)
    {
        float corr = 0.0f;
        for (int i = 0; i < CORR_LEN; i += 2)
        {
            const double rp = refBase + i, cp = inputPos + offset + i;
            if (rp < startSample || rp >= endSample || cp < startSample || cp >= endSample) continue;
            const int ri = (int)rp, ci = (int)cp;
            corr += (ri>=0&&ri<n?srcData[ri]:0.0f) * (ci>=0&&ci<n?srcData[ci]:0.0f);
        }
        if (corr > bestCorr) { bestCorr = corr; bestOffset = offset; }
    }
    return bestOffset;
}

void WSOLATimestretch::synthesizeGrain (const juce::AudioBuffer<float>& source,
                                         double grainPos, double startSample, double endSample,
                                         double srRatio, float pitchJitter, bool loop)
{
    const double range = endSample - startSample;
    if (pitchJitter > 0.0f)
        srRatio *= std::pow (2.0, (double)((rng.nextFloat() * 2.0f - 1.0f) * pitchJitter) / 12.0);
    for (int g = 0; g < GRAIN_SIZE; ++g)
    {
        double pos = grainPos + (double)g * srRatio;
        if (loop && range > 0.0) { while (pos>=endSample) pos-=range; while (pos<startSample) pos+=range; }
        const int idx = (writeIdx + g) % BUF_SIZE;
        for (int ch = 0; ch < numCh; ++ch)
            buf[ch][idx] += interpolate (source, ch, pos) * hanningWindow[g];
    }
    writeIdx = (writeIdx + HOP_SIZE) % BUF_SIZE; available += HOP_SIZE;
}

bool WSOLATimestretch::process (const juce::AudioBuffer<float>& source,
                                 juce::AudioBuffer<float>& output, int numSamples,
                                 double startSample, double endSample, double speed,
                                 double srRatio, double pitchFactor, float pitchJitter, bool loop)
{
    pitchFactor = juce::jlimit (0.1, 8.0, pitchFactor);
    inputHop = (double)HOP_SIZE * speed * srRatio / pitchFactor;
    const double needed = (double)numSamples * pitchFactor + HOP_SIZE;
    while (available < needed && !ended)
    {
        const int bestOffset = findBestOffset (source, startSample, endSample);
        const double grainPos = inputPos + bestOffset;
        synthesizeGrain (source, grainPos, startSample, endSample, srRatio, pitchJitter, loop);
        lastGrainPos = grainPos; firstGrain = false; inputPos += inputHop;
        if (loop && endSample > startSample)
        {
            const double range = endSample - startSample;
            while (inputPos >= endSample) inputPos -= range; while (inputPos < startSample) inputPos += range;
        }
        else if (!loop && inputPos >= endSample) ended = true;
    }
    int lastCleared = (int)readIdx;
    for (int i = 0; i < numSamples; ++i)
    {
        const int r0 = (int)readIdx % BUF_SIZE, r1 = (r0+1) % BUF_SIZE;
        const float frac = (float)(readIdx - std::floor (readIdx));
        for (int ch = 0; ch < numCh; ++ch)
            output.setSample (ch, i, buf[ch][r0] * (1.0f - frac) + buf[ch][r1] * frac);
        readIdx  += pitchFactor; available = juce::jmax (0.0, available - pitchFactor);
        const int newInt = (int)readIdx;
        for (int j = lastCleared; j < newInt; ++j)
            for (int ch = 0; ch < numCh; ++ch) buf[ch][j % BUF_SIZE] = 0.0f;
        lastCleared = newInt;
    }
    return !(ended && available <= 0.0);
}

//==============================================================================
// PhaseVocoder
//==============================================================================

void PhaseVocoder::prepare (int numChannels)
{
    numCh = numChannels;
    for (int i = 0; i < FFT_SIZE; ++i)
        analysisWindow[i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                       * (float)i / (float)(FFT_SIZE - 1)));
    outBuf.assign (numChannels, std::vector<float> (BUF_SIZE, 0.0f));
    lastPhase.assign  (numChannels, std::vector<float> (NUM_BINS, 0.0f));
    synthPhase.assign (numChannels, std::vector<float> (NUM_BINS, 0.0f));
}

void PhaseVocoder::reset (double startPos)
{
    for (auto& ch : outBuf)     std::fill (ch.begin(), ch.end(), 0.0f);
    for (auto& ch : lastPhase)  std::fill (ch.begin(), ch.end(), 0.0f);
    for (auto& ch : synthPhase) std::fill (ch.begin(), ch.end(), 0.0f);
    fftBuf.fill (0.0f);
    readIdx = writeIdx = 0; available = 0;
    inputPos = startPos; ended = false; firstFrame = true;
}

float PhaseVocoder::interpolate (const juce::AudioBuffer<float>& src, int ch, double pos) const
{
    const int    n = src.getNumSamples();
    const float* d = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
    return interpSample (d, n, pos);
}

void PhaseVocoder::synthesizeFrame (const juce::AudioBuffer<float>& source,
                                     double startSample, double endSample,
                                     double srRatio, float pitchJitter, bool loop)
{
    const double range = endSample - startSample;
    if (pitchJitter > 0.0f)
        srRatio *= std::pow (2.0, (double)((rng.nextFloat() * 2.0f - 1.0f) * pitchJitter) / 12.0);
    const float twoPi = juce::MathConstants<float>::twoPi;

    for (int ch = 0; ch < numCh; ++ch)
    {
        fftBuf.fill (0.0f);
        for (int n = 0; n < FFT_SIZE; ++n)
        {
            double pos = inputPos + (double)n * srRatio;
            if (loop && range > 0.0) { while (pos>=endSample) pos-=range; while (pos<startSample) pos+=range; }
            fftBuf[n] = interpolate (source, ch, pos) * analysisWindow[n];
        }
        fft.performRealOnlyForwardTransform (fftBuf.data(), false);

        for (int k = 0; k <= FFT_SIZE / 2; ++k)
        {
            const float re = fftBuf[2*k], im = fftBuf[2*k+1];
            const float mag = std::sqrt (re*re + im*im);
            const float phase = std::atan2 (im, re);
            float delta = phase - lastPhase[ch][k] - twoPi * (float)k * (float)inputHop / (float)FFT_SIZE;
            delta -= twoPi * std::round (delta / twoPi);
            const float trueFreq = twoPi * (float)k / (float)FFT_SIZE + delta / (float)inputHop;
            synthPhase[ch][k] += trueFreq * (float)HOP_SIZE;
            fftBuf[2*k]   = mag * std::cos (synthPhase[ch][k]);
            fftBuf[2*k+1] = mag * std::sin (synthPhase[ch][k]);
            lastPhase[ch][k] = phase;
        }
        for (int k = 1; k < FFT_SIZE / 2; ++k)
        {
            fftBuf[2*(FFT_SIZE-k)]   =  fftBuf[2*k];
            fftBuf[2*(FFT_SIZE-k)+1] = -fftBuf[2*k+1];
        }
        fftBuf[FFT_SIZE+1] = 0.0f;
        fft.performRealOnlyInverseTransform (fftBuf.data());
        for (int n = 0; n < FFT_SIZE; ++n)
            outBuf[ch][(writeIdx+n) % BUF_SIZE] += fftBuf[n] * analysisWindow[n];
    }
    writeIdx = (writeIdx + HOP_SIZE) % BUF_SIZE; available += HOP_SIZE;
    inputPos += inputHop;
    if (loop && range > 0.0) { while (inputPos>=endSample) inputPos-=range; while (inputPos<startSample) inputPos+=range; }
    else if (!loop && inputPos >= endSample) ended = true;
    firstFrame = false;
}

bool PhaseVocoder::process (const juce::AudioBuffer<float>& source,
                             juce::AudioBuffer<float>& output, int numSamples,
                             double startSample, double endSample, double speed,
                             double srRatio, double pitchFactor, float pitchJitter, bool loop)
{
    pitchFactor = juce::jlimit (0.1, 8.0, pitchFactor);
    inputHop = (double)HOP_SIZE * speed * srRatio / pitchFactor;
    const double needed = (double)numSamples * pitchFactor + HOP_SIZE;
    while (available < needed && !ended)
        synthesizeFrame (source, startSample, endSample, srRatio, pitchJitter, loop);

    int lastCleared = (int)readIdx;
    for (int i = 0; i < numSamples; ++i)
    {
        const int r0 = (int)readIdx % BUF_SIZE, r1 = (r0+1) % BUF_SIZE;
        const float frac = (float)(readIdx - std::floor (readIdx));
        for (int ch = 0; ch < numCh; ++ch)
            output.setSample (ch, i, outBuf[ch][r0] * (1.0f - frac) + outBuf[ch][r1] * frac);
        readIdx  += pitchFactor; available = juce::jmax (0.0, available - pitchFactor);
        const int newInt = (int)readIdx;
        for (int j = lastCleared; j < newInt; ++j)
            for (int ch = 0; ch < numCh; ++ch) outBuf[ch][j % BUF_SIZE] = 0.0f;
        lastCleared = newInt;
    }
    return !(ended && available <= 0.0);
}

//==============================================================================
// Parameter layout
//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout
BPMSamplerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ---- Per-slot params ----
    for (int i = 0; i < 8; ++i)
    {
        const juce::String s = "s" + juce::String (i) + "_";
        const juce::String n = "Slot " + juce::String (i) + " ";

        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"startPos", n+"Start",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"endPos", n+"End",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"speed", n+"Speed",
            juce::NormalisableRange<float> (0.01f, 16.0f, 0.001f, 0.4f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"speedFine", n+"Speed Fine",
            juce::NormalisableRange<float> (0.5f, 2.0f, 0.001f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"pitch", n+"Pitch",
            juce::NormalisableRange<float> (-72.0f, 72.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterBool> (s+"loopEnabled", n+"Loop", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (s+"gateEnabled", n+"Gate", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (s+"bpmSync", n+"BPM Sync", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"numBeats", n+"Beats",
            juce::NormalisableRange<float> (1.0f, 64.0f, 1.0f), 4.0f));
        params.push_back (std::make_unique<juce::AudioParameterBool> (s+"speedSync", n+"Speed Sync", false));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (s+"tsMode", n+"TS Mode",
            juce::StringArray { "Off", "OLA", "WSOLA", "Phase Vocoder" }, 0));
        params.push_back (std::make_unique<juce::AudioParameterBool> (s+"sliceMode", n+"Slice Mode", false));
        params.push_back (std::make_unique<juce::AudioParameterInt> (s+"sliceIndex", n+"Slice", 0, 63, 0));
        params.push_back (std::make_unique<juce::AudioParameterBool> (s+"granEnabled", n+"Granular", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"granPos", n+"Gran Pos",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"granPosJitter", n+"Gran Pos Jitter",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.1f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"granDensity", n+"Gran Density",
            juce::NormalisableRange<float> (1.0f, 100.0f, 0.1f, 0.4f), 10.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"granSize", n+"Gran Size",
            juce::NormalisableRange<float> (10.0f, 500.0f, 1.0f, 0.5f), 80.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"grainJitter", n+"Grain Jitter",
            juce::NormalisableRange<float> (0.0f, 12.0f, 0.01f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"analogJitter", n+"Analog Jitter",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"granScanLen", n+"Gran Scan Length",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"granScanSpd", n+"Gran Scan Speed",
            juce::NormalisableRange<float> (0.0f, 4.0f, 0.001f, 0.4f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"granScanDep", n+"Gran Scan Depth",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"granProb", n+"Gran Probability",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterBool> (s+"midiNoteMode", n+"MIDI Note Mode", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"vol", n+"Volume",
            juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterBool>  (s+"envEnabled", n+"Envelope", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"envAttack",  n+"Attack",
            juce::NormalisableRange<float> (1.0f, 5000.0f, 0.1f, 0.3f), 10.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"envDecay",   n+"Decay",
            juce::NormalisableRange<float> (1.0f, 5000.0f, 0.1f, 0.3f), 100.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"envSustain", n+"Sustain",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (s+"envRelease", n+"Release",
            juce::NormalisableRange<float> (1.0f, 5000.0f, 0.1f, 0.3f), 200.0f));
    }

    // ---- Global params ----
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("sampleMode", "Sample Mode",
        juce::StringArray { "Single", "MIDI Map", "Morph" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("morphPos", "Morph Position",
        juce::NormalisableRange<float> (0.0f, 7.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("morphAlgo", "Morph Algorithm", false));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("polyVoices", "Polyphony", 1, 8, 1));

    // Resonator (global state, per-slot filter banks)
    params.push_back (std::make_unique<juce::AudioParameterBool>   ("resoEnabled",   "Resonator",          false));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoRoot",      "Resonator Root",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 0.1f, 0.4f), 220.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoQ",         "Resonator Q",
        juce::NormalisableRange<float> (0.5f, 500.0f, 0.01f, 0.3f), 5.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoDecay",     "Resonator Decay",
        juce::NormalisableRange<float> (0.01f, 5.0f, 0.001f, 0.3f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoTaper",     "Resonator Taper",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoQTaper",    "Resonator Q Taper",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoInharm",    "Resonator Inharmonicity",
        juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoDrive",     "Resonator Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoScatter",   "Resonator Scatter",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoTime",      "Resonator Delay",
        juce::NormalisableRange<float> (0.5f, 50.0f, 0.01f, 0.5f), 10.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoFeedback",  "Resonator Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.3f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>  ("resoMix",       "Resonator Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("resoHarmonics", "Resonator Harmonics",
        juce::StringArray { "8", "16", "32" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("resoSeries",    "Resonator Series",
        juce::StringArray { "All", "Odd", "Even" }, 0));

    // Character (global)
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("hissEnabled",   "Hiss",             false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("outputSat",     "Output Saturation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
// Slice management (per slot)
//==============================================================================

void BPMSamplerProcessor::addSlice (int slotIdx, float normPos)
{
    normPos = juce::jlimit (0.0f, 1.0f, normPos);
    auto& slot = slots[slotIdx];
    const juce::ScopedLock sl (slot.sliceLock);
    for (auto p : slot.slicePositions)
        if (std::abs (p - normPos) < 0.005f) return;
    slot.slicePositions.push_back (normPos);
    std::sort (slot.slicePositions.begin(), slot.slicePositions.end());
}

void BPMSamplerProcessor::removeSliceAt (int slotIdx, int index)
{
    auto& slot = slots[slotIdx];
    const juce::ScopedLock sl (slot.sliceLock);
    if (index >= 0 && index < (int)slot.slicePositions.size())
        slot.slicePositions.erase (slot.slicePositions.begin() + index);
}

void BPMSamplerProcessor::clearSlices (int slotIdx)
{
    auto& slot = slots[slotIdx];
    const juce::ScopedLock sl (slot.sliceLock);
    slot.slicePositions.clear();
}

void BPMSamplerProcessor::moveSlice (int slotIdx, float fromPos, float toPos)
{
    toPos = juce::jlimit (0.0f, 1.0f, toPos);
    auto& slot = slots[slotIdx];
    const juce::ScopedLock sl (slot.sliceLock);
    float bestDist = 0.02f; int bestIdx = -1;
    for (int i = 0; i < (int)slot.slicePositions.size(); ++i)
    {
        const float d = std::abs (slot.slicePositions[i] - fromPos);
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    if (bestIdx >= 0)
    {
        slot.slicePositions[bestIdx] = toPos;
        std::sort (slot.slicePositions.begin(), slot.slicePositions.end());
    }
}

void BPMSamplerProcessor::detectTransients (int slotIdx, float threshold)
{
    auto& slot = slots[slotIdx];
    const int totalSamples = slot.buffer.getNumSamples();
    if (totalSamples == 0) return;

    const int windowSize = 512, hopSize = 256;
    const int numCh = slot.buffer.getNumChannels();

    std::vector<float> energy;
    energy.reserve ((totalSamples / hopSize) + 1);
    for (int i = 0; i + windowSize <= totalSamples; i += hopSize)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* d = slot.buffer.getReadPointer (ch);
            for (int j = 0; j < windowSize; ++j) sum += d[i+j] * d[i+j];
        }
        energy.push_back (std::sqrt (sum / (float)(windowSize * numCh)));
    }
    if (energy.size() < 2) return;

    std::vector<float> flux (energy.size(), 0.0f);
    for (int i = 1; i < (int)energy.size(); ++i)
        flux[i] = juce::jmax (0.0f, energy[i] - energy[i-1]);

    float meanFlux = 0.0f;
    for (float f : flux) meanFlux += f;
    meanFlux /= (float)flux.size();

    const float adaptiveThreshold = meanFlux * (1.0f + threshold * 10.0f);
    const int   minDistFrames     = juce::jmax (1, (int)(0.05 * slot.sampleRate / hopSize));

    const juce::ScopedLock sl (slot.sliceLock);
    slot.slicePositions.clear();
    int lastPeak = -minDistFrames;
    for (int i = 1; i + 1 < (int)flux.size(); ++i)
    {
        if (flux[i] > adaptiveThreshold && flux[i] >= flux[i-1] && flux[i] >= flux[i+1]
            && (i - lastPeak) >= minDistFrames)
        {
            const float normPos = (float)(i * hopSize) / (float)totalSamples;
            if (normPos > 0.01f && normPos < 0.99f)
            {
                slot.slicePositions.push_back (normPos);
                lastPeak = i;
            }
        }
    }
    std::sort (slot.slicePositions.begin(), slot.slicePositions.end());
}

//==============================================================================
// LPCMorphEngine implementation
//==============================================================================

void LPCMorphEngine::computeLPC (const float* x, float* coeffs, float* parcorOut)
{
    float windowed[LPC_WINDOW];
    for (int i = 0; i < LPC_WINDOW; ++i)
    {
        const double w = 0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi
                                                  * i / (LPC_WINDOW - 1)));
        windowed[i] = (float)(x[i] * w);
    }

    double r[LPC_ORDER + 1];
    for (int lag = 0; lag <= LPC_ORDER; ++lag)
    {
        double sum = 0.0;
        for (int i = lag; i < LPC_WINDOW; ++i)
            sum += windowed[i] * windowed[i - lag];
        r[lag] = sum;
    }

    if (r[0] < 1e-10)
    {
        std::fill (coeffs,    coeffs    + LPC_ORDER, 0.0f);
        std::fill (parcorOut, parcorOut + LPC_ORDER, 0.0f);
        return;
    }

    double a   [LPC_ORDER + 1] = {};
    double aTmp[LPC_ORDER + 1] = {};
    double error = r[0];

    for (int m = 1; m <= LPC_ORDER; ++m)
    {
        double sum = r[m];
        for (int j = 1; j < m; ++j)
            sum += a[j] * r[m - j];

        double k = -sum / error;
        k = juce::jlimit (-0.95, 0.95, k);
        parcorOut[m - 1] = (float)k;

        aTmp[m] = k;
        for (int j = 1; j < m; ++j)
            aTmp[j] = a[j] + k * a[m - j];
        for (int j = 1; j <= m; ++j)
            a[j] = aTmp[j];

        error *= (1.0 - k * k);
        if (error < 1e-30) break;
    }

    for (int i = 0; i < LPC_ORDER; ++i)
        coeffs[i] = (float)a[i + 1];
}

void LPCMorphEngine::stepUp (const float* parcor, float* coeffs)
{
    double a   [LPC_ORDER + 1] = {};
    double aTmp[LPC_ORDER + 1] = {};

    for (int m = 1; m <= LPC_ORDER; ++m)
    {
        const double k = juce::jlimit (-0.95, 0.95, (double)parcor[m - 1]);
        aTmp[m] = k;
        for (int j = 1; j < m; ++j)
            aTmp[j] = a[j] + k * a[m - j];
        for (int j = 1; j <= m; ++j)
            a[j] = aTmp[j];
    }

    for (int i = 0; i < LPC_ORDER; ++i)
        coeffs[i] = (float)a[i + 1];
}

void LPCMorphEngine::updateCoeffs (const juce::AudioBuffer<float>& bufA,
                                    const juce::AudioBuffer<float>& bufB,
                                    int n, double sr)
{
    const float alpha = std::exp (-(float)n / (0.040f * (float)sr));

    auto appendToWindow = [n](float* win, const juce::AudioBuffer<float>& buf)
    {
        const int numNew = juce::jmin (n, LPC_WINDOW);
        const int keep   = LPC_WINDOW - numNew;
        if (keep > 0) std::memmove (win, win + numNew, (size_t)keep * sizeof (float));
        const float* src = buf.getReadPointer (0);
        std::memcpy (win + keep, src + (n - numNew), (size_t)numNew * sizeof (float));
    };
    appendToWindow (analysisWinA, bufA);
    appendToWindow (analysisWinB, bufB);

    auto analyzeSlot = [&](float* win, float* parcor, float* lpcCoeffs, float& rms)
    {
        float newCoeffs[LPC_ORDER], newParcor[LPC_ORDER];
        computeLPC (win, newCoeffs, newParcor);
        for (int i = 0; i < LPC_ORDER; ++i)
            parcor[i] = alpha * parcor[i] + (1.0f - alpha) * newParcor[i];
        stepUp (parcor, lpcCoeffs);
        float gk = 0.98f;
        for (int i = 0; i < LPC_ORDER; ++i, gk *= 0.98f) lpcCoeffs[i] *= gk;
        double sum = 0.0;
        for (int i = 0; i < LPC_WINDOW; ++i) sum += (double)win[i] * win[i];
        rms = juce::jmax (1e-6f, (float)std::sqrt (sum / LPC_WINDOW));
    };
    analyzeSlot (analysisWinA, parcorA, lpcCoeffsA, windowRmsA);
    analyzeSlot (analysisWinB, parcorB, lpcCoeffsB, windowRmsB);
}

void LPCMorphEngine::process (const juce::AudioBuffer<float>& bufA,
                               const juce::AudioBuffer<float>& bufB,
                               juce::AudioBuffer<float>& outBuf,
                               float crossfade, int numSamples, double sr)
{
    const int numCh = juce::jmin (outBuf.getNumChannels(), 2);

    // Phase 1 (crossfade 0→0.5): filter morphs A→B, excitation = A
    // Phase 2 (crossfade 0.5→1): filter = B, excitation morphs A→B
    const float filterMix = juce::jmin (1.0f, crossfade * 2.0f);
    const float excitMix  = juce::jmax (0.0f, crossfade * 2.0f - 1.0f);

    float morphParcor[LPC_ORDER];
    for (int i = 0; i < LPC_ORDER; ++i)
        morphParcor[i] = (1.0f - filterMix) * parcorA[i] + filterMix * parcorB[i];

    float morphCoeffs[LPC_ORDER];
    stepUp (morphParcor, morphCoeffs);
    float gk = 0.98f;
    for (int i = 0; i < LPC_ORDER; ++i, gk *= 0.98f)
        morphCoeffs[i] *= gk;

    const float normA      = 1.0f / windowRmsA;
    const float normB      = 1.0f / windowRmsB;
    const float centerness = 1.0f - std::abs (2.0f * crossfade - 1.0f);
    const float targetRms  = ((1.0f - crossfade) * windowRmsA + crossfade * windowRmsB)
                           * (1.0f - 0.20f * centerness);

    for (int ch = 0; ch < numCh; ++ch)
    {
        const int    chA     = juce::jmin (ch, bufA.getNumChannels() - 1);
        const int    chB     = juce::jmin (ch, bufB.getNumChannels() - 1);
        const float* inA     = bufA.getReadPointer (chA);
        const float* inB     = bufB.getReadPointer (chB);
        float*       out     = outBuf.getWritePointer (ch);
        float*       synthSt = synthesisState[ch];
        float*       hA      = histA[ch];
        float*       hB      = histB[ch];

        for (int i = 0; i < LPC_ORDER; ++i)
            if (!std::isfinite (synthSt[i]))
                { std::fill (synthSt, synthSt + LPC_ORDER, 0.0f); break; }

        for (int n = 0; n < numSamples; ++n)
        {
            // Inverse-filter each slot to extract excitation residual
            float eA = inA[n], eB = inB[n];
            for (int i = 0; i < LPC_ORDER; ++i)
            {
                const int   p  = n - 1 - i;          // sample index relative to block start
                const float sA = (p >= 0) ? inA[p] : hA[-(p + 1)];
                const float sB = (p >= 0) ? inB[p] : hB[-(p + 1)];
                eA += lpcCoeffsA[i] * sA;
                eB += lpcCoeffsB[i] * sB;
            }

            const float eRaw  = targetRms * ((1.0f - excitMix) * eA * normA
                                           +           excitMix  * eB * normB);
            // Soft-limit before entering the IIR — prevents transient stacking from
            // overdriving the synthesis filter's resonances.
            const float limit = juce::jmax (3.0f * targetRms, 1e-6f);
            const float e     = limit * std::tanh (eRaw / limit);

            float y = e;
            for (int i = 0; i < LPC_ORDER; ++i)
                y -= morphCoeffs[i] * synthSt[i];

            if (!std::isfinite (y)) y = 0.0f;
            y = juce::jlimit (-1.0f, 1.0f, y);

            for (int i = LPC_ORDER - 1; i > 0; --i)
                synthSt[i] = synthSt[i - 1];
            synthSt[0] = y;

            out[n] = y;
        }

        // Adaptive gain: correct synthesis-filter energy drift across crossfade positions
        float outPow = 0.0f;
        for (int n = 0; n < numSamples; ++n) outPow += out[n] * out[n];
        outPow /= (float)numSamples;

        if (outPow > 1e-10f)
        {
            const float desired = juce::jlimit (0.1f, 4.0f, targetRms / std::sqrt (outPow));
            const float tc = std::exp (-(float)numSamples / (0.020f * (float)sr));
            lpcGainSmooth[ch] = tc * lpcGainSmooth[ch] + (1.0f - tc) * desired;
        }
        juce::FloatVectorOperations::multiply (out, lpcGainSmooth[ch], numSamples);

        // Update per-channel history (most-recent-first) for next block's inverse filter
        float newHistA[LPC_ORDER], newHistB[LPC_ORDER];
        for (int i = 0; i < LPC_ORDER; ++i)
        {
            const int srcIdx = numSamples - 1 - i;
            newHistA[i] = (srcIdx >= 0) ? inA[srcIdx] : hA[-(srcIdx + 1)];
            newHistB[i] = (srcIdx >= 0) ? inB[srcIdx] : hB[-(srcIdx + 1)];
        }
        std::memcpy (hA, newHistA, LPC_ORDER * sizeof (float));
        std::memcpy (hB, newHistB, LPC_ORDER * sizeof (float));
    }
}

//==============================================================================
// Constructor / Destructor
//==============================================================================

BPMSamplerProcessor::BPMSamplerProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output",  juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Mod 1-2", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Mod 3-4", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    formatManager.registerBasicFormats();

    for (int i = 0; i < NUM_SLOTS; ++i)
        slotThumbnails.emplace_back (
            std::make_unique<juce::AudioThumbnail> (512, formatManager, thumbnailCache));

    for (int i = 0; i < NUM_SLOTS; ++i)
    {
        const juce::String s = "s" + juce::String (i) + "_";
        auto& p = slotPtrs[i];
        p.startPos     = apvts.getRawParameterValue (s + "startPos");
        p.endPos       = apvts.getRawParameterValue (s + "endPos");
        p.speed        = apvts.getRawParameterValue (s + "speed");
        p.pitch        = apvts.getRawParameterValue (s + "pitch");
        p.speedParam   = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (s + "speed"));
        p.pitchParam   = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (s + "pitch"));
        p.loop         = apvts.getRawParameterValue (s + "loopEnabled");
        p.gate         = apvts.getRawParameterValue (s + "gateEnabled");
        p.bpmSync      = apvts.getRawParameterValue (s + "bpmSync");
        p.numBeats     = apvts.getRawParameterValue (s + "numBeats");
        p.speedSync    = apvts.getRawParameterValue (s + "speedSync");
        p.speedFine    = apvts.getRawParameterValue (s + "speedFine");
        p.tsMode       = apvts.getRawParameterValue (s + "tsMode");
        p.sliceMode    = apvts.getRawParameterValue (s + "sliceMode");
        p.sliceIndex   = apvts.getRawParameterValue (s + "sliceIndex");
        p.granEnabled  = apvts.getRawParameterValue (s + "granEnabled");
        p.granPos      = apvts.getRawParameterValue (s + "granPos");
        p.granPosJit   = apvts.getRawParameterValue (s + "granPosJitter");
        p.granDensity  = apvts.getRawParameterValue (s + "granDensity");
        p.granSize     = apvts.getRawParameterValue (s + "granSize");
        p.grainJitter  = apvts.getRawParameterValue (s + "grainJitter");
        p.analogJitter = apvts.getRawParameterValue (s + "analogJitter");
        p.granScanLen  = apvts.getRawParameterValue (s + "granScanLen");
        p.granScanSpd  = apvts.getRawParameterValue (s + "granScanSpd");
        p.granScanDep  = apvts.getRawParameterValue (s + "granScanDep");
        p.granProb     = apvts.getRawParameterValue (s + "granProb");
        p.midiNoteMode = apvts.getRawParameterValue (s + "midiNoteMode");
        p.vol          = apvts.getRawParameterValue (s + "vol");
        p.envEnabled   = apvts.getRawParameterValue (s + "envEnabled");
        p.envAttack    = apvts.getRawParameterValue (s + "envAttack");
        p.envDecay     = apvts.getRawParameterValue (s + "envDecay");
        p.envSustain   = apvts.getRawParameterValue (s + "envSustain");
        p.envRelease   = apvts.getRawParameterValue (s + "envRelease");

        // Init per-slot curves
        slots[i].pitchCurve    = BPMCurveData::create (-24.f,  24.f,  -72.f, 72.f);
        slots[i].speedCurve    = BPMCurveData::create (0.25f,   4.f, 0.01f, 16.f);
        slots[i].startPosCurve = BPMCurveData::create (0.f,     1.f,   0.f,  1.f);
        slots[i].granPosCurve  = BPMCurveData::create (0.f,     1.f,   0.f,  1.f);
    }
}

BPMSamplerProcessor::~BPMSamplerProcessor() {}

//==============================================================================
// Prepare / Release
//==============================================================================

void BPMSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;
    for (int i = 0; i < NUM_SLOTS; ++i)
    {
        for (auto& voice : slots[i].voices)
        {
            voice.olaEngine.prepare   (2);
            voice.wsolaEngine.prepare (2);
            voice.pvEngine.prepare    (2);
            voice.granEngine.prepare  (2);
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (std::begin (slots[i].resoDelayBuf[ch]), std::end (slots[i].resoDelayBuf[ch]), 0.0f);
            slots[i].resoDelayWrite[ch] = 0;
            for (int h = 0; h < SampleSlot::MAX_RES_HARMONICS; ++h)
                slots[i].resoStates[ch][h].reset();
        }
    }
    morphTempBuffer.setSize (2, samplesPerBlock, false, true, true);
    lpcBufA.setSize         (2, samplesPerBlock, false, true, true);
    lpcBufB.setSize         (2, samplesPerBlock, false, true, true);
    lpcMorph.reset();
    lpcLastSa = -1;
    lpcLastSb = -1;
    voiceMixBuffer.setSize  (2, samplesPerBlock, false, true, true);
    modBuffer.setSize       (NUM_MOD_CHANNELS, samplesPerBlock, false, true, false);
    for (int i = 0; i < NUM_MOD_CHANNELS; ++i)
        midiMod[i].reset (sampleRate, 0.005);
    globalOutputStage.prepare (sampleRate);
}

void BPMSamplerProcessor::releaseResources() {}

bool BPMSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Each input bus must be stereo or disabled
    for (const auto& bus : layouts.inputBuses)
        if (bus != juce::AudioChannelSet::stereo() && !bus.isDisabled())
            return false;

    return true;

}

//==============================================================================
// Helpers
//==============================================================================

double BPMSamplerProcessor::calculateEffectiveSpeed (int slotIdx, double baseSpeed,
                                                      double fileDurSecs) const
{
    double hostBPM = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBPM = *bpm;

    const double numBeats  = (double)*slotPtrs[slotIdx].numBeats;
    const double targetDur = (numBeats * 60.0) / hostBPM;
    if (targetDur <= 0.0) return baseSpeed;
    return fileDurSecs / targetDur;
}

double BPMSamplerProcessor::snapToMusicalSpeed (double speed) const
{
    static const double vals[] = { 0.25, 0.333, 0.5, 0.667, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
    double best = vals[0], bestDist = std::abs (speed - vals[0]);
    for (auto v : vals) { double d = std::abs (speed - v); if (d < bestDist) { bestDist = d; best = v; } }
    return best;
}

double BPMSamplerProcessor::getPlaybackPositionNorm (int slotIdx) const
{
    const int n = slots[slotIdx].buffer.getNumSamples();
    if (n <= 0) return 0.0;
    // Return position of the most recently triggered active voice
    int newestAge = -1, newestV = 0;
    for (int v = 0; v < SampleSlot::MAX_POLY_VOICES; ++v)
    {
        const auto& voice = slots[slotIdx].voices[v];
        if (voice.active.load() && voice.triggerAge > newestAge)
        {
            newestAge = voice.triggerAge;
            newestV   = v;
        }
    }
    return juce::jlimit (0.0, 1.0,
        slots[slotIdx].voices[newestV].playbackPosAtom.load() / (double)n);
}

//==============================================================================
// Note-on / trigger
//==============================================================================

void BPMSamplerProcessor::startSlotPlayback (int slotIdx, int noteNum)
{
    auto& slot = slots[slotIdx];
    if (!slot.hasFile()) return;

    const int maxVoices = juce::jlimit (1, SampleSlot::MAX_POLY_VOICES,
                                        (int)*apvts.getRawParameterValue ("polyVoices"));

    // Find a free voice; if none, steal the oldest (lowest triggerAge)
    int voiceIdx = -1;
    int oldestAge = INT_MAX;
    for (int v = 0; v < maxVoices; ++v)
    {
        if (!slot.voices[v].active.load()) { voiceIdx = v; break; }
        if (slot.voices[v].triggerAge < oldestAge)
        {
            oldestAge = slot.voices[v].triggerAge;
            voiceIdx  = v;
        }
    }
    if (voiceIdx < 0) voiceIdx = 0;

    auto& voice           = slot.voices[voiceIdx];
    voice.midiNote        = noteNum;
    voice.triggerAge      = ++voiceAgeCounter;

    const auto& ptrs = slotPtrs[slotIdx];
    const int   nSmp = slot.buffer.getNumSamples();
    const bool  sliceM = *ptrs.sliceMode > 0.5f;

    double startSmp;
    {
        const juce::ScopedTryLock sl (slot.sliceLock);
        if (sliceM && sl.isLocked() && !slot.slicePositions.empty())
        {
            int idx = juce::jlimit (0, (int)slot.slicePositions.size() - 1, (int)*ptrs.sliceIndex);
            startSmp = slot.slicePositions[idx] * nSmp;
        }
        else
        {
            const float basePosNorm = (float)slot.startPosCurve.applyToNorm (*ptrs.startPos);
            const auto& startPosMod = slotMod[slotIdx][kMod_StartPos];
            if (startPosMod.channel >= 0 && startPosMod.channel < NUM_MOD_CHANNELS)
            {
                const float modVal    = midiMod[startPosMod.channel].getCurrentValue();
                const float moddedNorm = juce::jlimit (0.0f, 1.0f,
                    basePosNorm + modVal * startPosMod.depth * 1.0f);
                startSmp = (double)moddedNorm * nSmp;
            }
            else
            {
                startSmp = (double)basePosNorm * nSmp;
            }
        }
    }

    voice.playbackPos = startSmp;
    voice.playbackPosAtom.store (startSmp);
    const int tsMode = (int)*ptrs.tsMode;
    switch (tsMode)
    {
        case 1: voice.olaEngine.reset   (startSmp); break;
        case 2: voice.wsolaEngine.reset (startSmp); break;
        case 3: voice.pvEngine.reset    (startSmp); break;
        default: break;
    }
    voice.granEngine.reset();
    voice.envPhase = VoiceState::EnvPhase::Attack;
    voice.envValue = 0.0f;
    voice.active.store (true);
    voice.noteHeld = true;
}

void BPMSamplerProcessor::stopNoteInSlot (int slotIdx, int noteNum)
{
    // Find the oldest active voice with this note and mark it for release
    int foundV = -1, oldestAge = INT_MAX;
    for (int v = 0; v < SampleSlot::MAX_POLY_VOICES; ++v)
    {
        auto& voice = slots[slotIdx].voices[v];
        if (voice.active.load() && voice.midiNote == noteNum && voice.triggerAge < oldestAge)
        {
            oldestAge = voice.triggerAge;
            foundV    = v;
        }
    }
    if (foundV >= 0)
    {
        auto& v = slots[slotIdx].voices[foundV];
        v.noteHeld = false;
        if (*slotPtrs[slotIdx].gate <= 0.5f)
            return; // gate off: note-off ignored, voice plays to end or loops forever
        if (*slotPtrs[slotIdx].envEnabled > 0.5f)
            v.envPhase = VoiceState::EnvPhase::Release;
        else
            v.active.store (false);
    }
}

//==============================================================================
// Per-slot processing
//==============================================================================

void BPMSamplerProcessor::processSlotNormal (SampleSlot& slot, VoiceState& voice, int slotIdx,
                                              juce::AudioBuffer<float>& buffer,
                                              int numSamples,
                                              double startSample, double endSample,
                                              double baseSpeed, double srRatio, double jitterFactor,
                                              double basePitchSemitones, bool loop)
{
    const int outCh    = buffer.getNumChannels();
    const double range = endSample - startSample;
    const int n        = slot.buffer.getNumSamples();

    const auto& pitchMod = slotMod[slotIdx][kMod_Pitch];
    const auto& speedMod = slotMod[slotIdx][kMod_Speed];
    const bool hasMod    = (pitchMod.channel >= 0 && pitchMod.channel < modChanCount)
                        || (speedMod.channel >= 0 && speedMod.channel < modChanCount);

    auto lerp = [&] (int ch, double pos) -> float {
        const int c = juce::jmin (ch, slot.buffer.getNumChannels() - 1);
        return interpSample (slot.buffer.getReadPointer (c), n, pos);
    };

    // Fast path: no mod assigned — compute advance once
    if (!hasMod)
    {
        const double advance = baseSpeed * srRatio * jitterFactor
                             * std::pow (2.0, basePitchSemitones / 12.0);
        for (int i = 0; i < numSamples; ++i)
        {
            if (!voice.active.load()) break;
            for (int ch = 0; ch < outCh; ++ch)
                buffer.setSample (ch, i, lerp (ch, voice.playbackPos));
            voice.playbackPos += advance;
            if (voice.playbackPos >= endSample)
            {
                if (loop && range > 0.0)
                    voice.playbackPos = startSample + std::fmod (voice.playbackPos - startSample, range);
                else
                {
                    const int fadeLen = juce::jmin (64, i + 1);
                    for (int ch = 0; ch < outCh; ++ch)
                        for (int f = 0; f < fadeLen; ++f)
                            buffer.setSample (ch, i - f, buffer.getSample (ch, i - f) * (float)f / (float)fadeLen);
                    voice.active.store (false);
                    voice.playbackPos = startSample;
                }
            }
        }
    }
    else
    {
        // Mod path: recompute advance per-sample
        for (int i = 0; i < numSamples; ++i)
        {
            if (!voice.active.load()) break;

            const float modSpd = applyMod ((float)baseSpeed,  speedMod, modChanPtrs, modChanCount, i, 0.01f, 16.0f,  3.75f); // range=4.0-0.25
            const float modPit = applyMod ((float)basePitchSemitones, pitchMod, modChanPtrs, modChanCount, i, -96.0f, 96.0f, 48.0f); // ±48 st full range

            const double advance = (double)modSpd * srRatio * jitterFactor
                                 * std::pow (2.0, (double)modPit / 12.0);

            for (int ch = 0; ch < outCh; ++ch)
                buffer.setSample (ch, i, lerp (ch, voice.playbackPos));
            voice.playbackPos += advance;
            if (voice.playbackPos >= endSample)
            {
                if (loop && range > 0.0)
                    voice.playbackPos = startSample + std::fmod (voice.playbackPos - startSample, range);
                else
                {
                    const int fadeLen = juce::jmin (64, i + 1);
                    for (int ch = 0; ch < outCh; ++ch)
                        for (int f = 0; f < fadeLen; ++f)
                            buffer.setSample (ch, i - f, buffer.getSample (ch, i - f) * (float)f / (float)fadeLen);
                    voice.active.store (false);
                    voice.playbackPos = startSample;
                }
            }
        }
    }
    voice.playbackPosAtom.store (voice.playbackPos);
}

void BPMSamplerProcessor::processSlotTimestretch (SampleSlot& slot, VoiceState& voice, int slotIdx,
                                                   juce::AudioBuffer<float>& buffer,
                                                   int numSamples,
                                                   double startSample, double endSample,
                                                   double speed, double srRatio,
                                                   bool loop, int tsMode)
{
    const auto& ptrs = slotPtrs[slotIdx];
    const float  pitchNorm = ptrs.pitchParam ? ptrs.pitchParam->getValue() : *ptrs.pitch;
    double semitones = (double)slot.pitchCurve.applyToNorm (pitchNorm)
                     + voice.midiNotePitchSemitones;
    {
        const auto& pitchMod = slotMod[slotIdx][kMod_Pitch];
        if (pitchMod.channel >= 0 && pitchMod.channel < modChanCount && modChanPtrs[pitchMod.channel])
            semitones = (double)applyMod ((float)semitones, pitchMod, modChanPtrs, modChanCount, 0, -96.0f, 96.0f, 48.0f);
    }
    {
        const auto& speedMod = slotMod[slotIdx][kMod_Speed];
        if (speedMod.channel >= 0 && speedMod.channel < modChanCount && modChanPtrs[speedMod.channel])
            speed = (double)applyMod ((float)speed, speedMod, modChanPtrs, modChanCount, 0, 0.01f, 16.0f, 3.75f);
    }
    const double pitchFactor = std::pow (2.0, semitones / 12.0);
    const float  pitchJitter = *ptrs.grainJitter;

    bool stillGoing = true;
    switch (tsMode)
    {
        case 1:
            stillGoing = voice.olaEngine.process (slot.buffer, buffer, numSamples,
                             startSample, endSample, speed, srRatio, pitchFactor, pitchJitter, loop);
            voice.playbackPos = voice.olaEngine.getInputPosition(); break;
        case 2:
            stillGoing = voice.wsolaEngine.process (slot.buffer, buffer, numSamples,
                             startSample, endSample, speed, srRatio, pitchFactor, pitchJitter, loop);
            voice.playbackPos = voice.wsolaEngine.getInputPosition(); break;
        case 3:
            stillGoing = voice.pvEngine.process (slot.buffer, buffer, numSamples,
                             startSample, endSample, speed, srRatio, pitchFactor, pitchJitter, loop);
            voice.playbackPos = voice.pvEngine.getInputPosition(); break;
        default: break;
    }
    voice.playbackPosAtom.store (voice.playbackPos);
    if (!stillGoing) { voice.active.store (false); voice.playbackPos = startSample; voice.playbackPosAtom.store (startSample); }
}

void BPMSamplerProcessor::processSlotGranular (SampleSlot& slot, VoiceState& voice, int slotIdx,
                                               juce::AudioBuffer<float>& buffer,
                                               int numSamples,
                                               double startSample, double endSample)
{
    const auto& ptrs = slotPtrs[slotIdx];
    const float  pitchNorm     = ptrs.pitchParam ? ptrs.pitchParam->getValue() : *ptrs.pitch;
    double pitchSemitones = (double)slot.pitchCurve.applyToNorm (pitchNorm)
                          + voice.midiNotePitchSemitones;
    {
        const auto& pitchMod = slotMod[slotIdx][kMod_Pitch];
        if (pitchMod.channel >= 0 && pitchMod.channel < modChanCount && modChanPtrs[pitchMod.channel])
            pitchSemitones = (double)applyMod ((float)pitchSemitones, pitchMod, modChanPtrs, modChanCount, 0, -96.0f, 96.0f, 48.0f);
    }
    const double pitchFactor = std::pow (2.0, pitchSemitones / 12.0);
    const float  pitchJitter = *ptrs.grainJitter;
    const double srRatio     = slot.sampleRate / currentSampleRate;

    const float  granPosNorm   = slot.granPosCurve.applyToNorm (*ptrs.granPos);
    const float  granPosJitter = *ptrs.granPosJit;
    const float  granDensity   = *ptrs.granDensity;
    const float  granSizeMs    = *ptrs.granSize;
    const float  granProb      = *ptrs.granProb;
    const float  granScanLen   = *ptrs.granScanLen;
    const float  granScanSpd   = *ptrs.granScanSpd;
    const float  granScanDep   = *ptrs.granScanDep;

    const double range             = endSample - startSample;
    const double centerPos         = startSample + (double)granPosNorm * range;
    const double posJitterSamps    = (double)granPosJitter * range * 0.5;
    const int    grainLenSamps     = juce::jlimit (64, (int)(currentSampleRate * 0.5),
                                                   (int)(granSizeMs * 0.001 * currentSampleRate));
    const double grainInterval     = currentSampleRate / (double)granDensity;
    const double step              = srRatio * pitchFactor * (double)voice.currentJitterFactor;
    const double scanLenSamps      = (double)granScanLen * range;
    const double scanSpeedPerSample= (double)granScanSpd / currentSampleRate;

    // Build per-grain / per-sample mod sources
    auto modChan = [&] (SlotModTarget t) -> const float* {
        const int ch = slotMod[slotIdx][t].channel;
        return (ch >= 0 && ch < modChanCount) ? modChanPtrs[ch] : nullptr;
    };
    auto modDep = [&] (SlotModTarget t) { return slotMod[slotIdx][t].depth; };

    const double maxGrainSamps = (double)(int)(currentSampleRate * 0.5) - 64.0;
    const double maxInterval   = currentSampleRate;   // density=1 g/s
    const double minInterval   = currentSampleRate / 100.0; // density=100 g/s

    GranModSources gmod;
    gmod.granPosBuf  = modChan (kMod_GranPos);     gmod.granPosScale  = modDep (kMod_GranPos)     * range;
    gmod.posJitBuf   = modChan (kMod_GranPosJit);  gmod.posJitScale   = modDep (kMod_GranPosJit)  * range * 0.5;
    gmod.pitchJitBuf = modChan (kMod_GrainJitter); gmod.pitchJitScale = modDep (kMod_GrainJitter) * 12.0f;
    gmod.granSizeBuf = modChan (kMod_GranSize);    gmod.granSizeScale = modDep (kMod_GranSize)    * maxGrainSamps;
    gmod.densityBuf  = modChan (kMod_GranDensity); gmod.densityScale  = -(double)modDep (kMod_GranDensity) * (maxInterval - minInterval);
    gmod.scanLenBuf  = modChan (kMod_ScanLen);     gmod.scanLenScale  = modDep (kMod_ScanLen)     * range;
    gmod.scanSpdBuf  = modChan (kMod_ScanSpd);     gmod.scanSpdScale  = modDep (kMod_ScanSpd)     * (4.0 / currentSampleRate);
    gmod.scanDepBuf  = modChan (kMod_ScanDep);     gmod.scanDepScale  = modDep (kMod_ScanDep)     * 1.0f;

    voice.granEngine.process (slot.buffer, buffer, numSamples,
                              centerPos, posJitterSamps, grainLenSamps,
                              grainInterval, step, pitchJitter,
                              granProb, scanLenSamps, scanSpeedPerSample, granScanDep, gmod);

    const double scanOffset    = voice.granEngine.getScanPhase() * (double)granScanDep * scanLenSamps;
    const double scannedCenter = juce::jlimit (0.0, (double)(slot.buffer.getNumSamples() - 1),
                                               centerPos + scanOffset);
    voice.playbackPos = scannedCenter;
    voice.playbackPosAtom.store (scannedCenter);
}

void BPMSamplerProcessor::updateVoiceJitter (SampleSlot& slot, VoiceState& voice, int slotIdx)
{
    juce::ignoreUnused (slot, slotIdx);
    const float amount = 1.0f;
    if (amount > 0.0f)
    {
        const float blockRate = (float)currentSampleRate / (float)juce::jmax (1, currentBlockSize);
        const float twoPi     = juce::MathConstants<float>::twoPi;
        const float wowAlpha  = 1.0f - std::exp (-twoPi * 0.5f / blockRate);
        const float flutAlpha = 1.0f - std::exp (-twoPi * 4.0f / blockRate);
        const float noise     = voice.jitterRng.nextFloat() * 2.0f - 1.0f;
        voice.jitterWowState     += wowAlpha  * (noise - voice.jitterWowState);
        voice.jitterFlutterState += flutAlpha * (noise - voice.jitterFlutterState);
        const float cents        = (voice.jitterWowState * 0.7f + voice.jitterFlutterState * 0.3f)
                                   * amount * 10.0f;
        voice.currentJitterFactor = std::pow (2.0f, cents / 1200.0f);
    }
    else
    {
        voice.currentJitterFactor = 1.0f;
    }
}

void BPMSamplerProcessor::updateSlotResoCoeffs (SampleSlot& slot,
                                                 float root, float q, int numH, float taper,
                                                 float inharm, float qTaper, int seriesMode,
                                                 float decaySecs, float scatter)
{
    const float nyquist   = (float)(currentSampleRate * 0.5);
    const float halfBW_ref = (1.0f - std::exp (-1.0f / (0.01f * (float)currentSampleRate))) * 0.5f;

    for (int h = 0; h < SampleSlot::MAX_RES_HARMONICS; ++h)
    {
        if (seriesMode == 1 && h % 2 == 1) { slot.resoCoeffs[h].gain = 0.0f; continue; }
        if (seriesMode == 2 && h % 2 == 0) { slot.resoCoeffs[h].gain = 0.0f; continue; }

        const uint32_t hHash    = static_cast<uint32_t> (h + 1) * 2654435761u;
        const float    hashNorm = (static_cast<float> (hHash >> 16) / 32767.5f) - 1.0f;
        const float    scatterHz = hashNorm * (scatter * 0.01f);
        const float freq = root * (float)(h + 1) * (1.0f + inharm * (float)h + scatterHz);

        if (h >= numH || freq >= nyquist || freq <= 0.0f) { slot.resoCoeffs[h].gain = 0.0f; continue; }

        const float hq = juce::jlimit (0.1f, 50.0f, q * std::pow (1.0f / (float)(h + 1), qTaper));
        const float w0 = juce::MathConstants<float>::twoPi * freq / (float)currentSampleRate;
        const float ringSamps = juce::jmax (1.0f, decaySecs * (float)currentSampleRate / (float)(h + 1));
        const float r_h = std::exp (-1.0f / ringSamps);
        const float peakGain     = juce::jmin (hq, 20.0f);
        const float halfBandwidth = juce::jmax (1e-4f, (1.0f - r_h) * 0.5f);
        slot.resoCoeffs[h].b0  =  peakGain * halfBandwidth;
        slot.resoCoeffs[h].b2  = -peakGain * halfBandwidth;
        slot.resoCoeffs[h].a1  = -2.0f * r_h * std::cos (w0);
        slot.resoCoeffs[h].a2  =  r_h * r_h;
        const float compFactor = juce::jmax (1.0f, juce::jmin (halfBW_ref / halfBandwidth, 8.0f));
        slot.resoCoeffs[h].gain = compFactor * ((taper < 0.001f) ? 1.0f
                                                 : std::pow (1.0f / (float)(h + 1), taper));
    }
}

void BPMSamplerProcessor::applySlotResonator (SampleSlot& slot,
                                               juce::AudioBuffer<float>& buffer,
                                               int /*slotIdx*/)
{
    if (!(*apvts.getRawParameterValue ("resoEnabled") > 0.5f)) return;

    const float baseRoot  = *apvts.getRawParameterValue ("resoRoot");
    const float q         = *apvts.getRawParameterValue ("resoQ");
    const float taper     = *apvts.getRawParameterValue ("resoTaper");
    const float mix       = *apvts.getRawParameterValue ("resoMix");
    const float feedback  = *apvts.getRawParameterValue ("resoFeedback");
    const float timeSec   = *apvts.getRawParameterValue ("resoTime") * 0.001f;
    const int   harmChoice= (int)*apvts.getRawParameterValue ("resoHarmonics");
    const int   numH      = (harmChoice == 0) ? 8 : (harmChoice == 1) ? 16 : 32;
    const float baseInharm= *apvts.getRawParameterValue ("resoInharm");
    const float qTaper    = *apvts.getRawParameterValue ("resoQTaper");
    const int   seriesMode= (int)*apvts.getRawParameterValue ("resoSeries");
    const float decaySecs = *apvts.getRawParameterValue ("resoDecay");
    const float drive     = *apvts.getRawParameterValue ("resoDrive");
    const float scatter   = *apvts.getRawParameterValue ("resoScatter");

    slot.resoDelaySamps = juce::jlimit (1, SampleSlot::RES_DELAY_SIZE - 1,
                                        (int)(timeSec * (float)currentSampleRate));

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Mod sources for root and inharm
    const auto& rootAssign   = globalMod[kMod_ResoRoot];
    const auto& inharmAssign = globalMod[kMod_ResoInharm];
    const float* rootModBuf   = (rootAssign.channel  >= 0 && rootAssign.channel  < modChanCount) ? modChanPtrs[rootAssign.channel]  : nullptr;
    const float* inharmModBuf = (inharmAssign.channel >= 0 && inharmAssign.channel < modChanCount) ? modChanPtrs[inharmAssign.channel] : nullptr;

    // Sub-block size for coefficient updates (32 samples ≈ 1380 updates/sec at 44.1kHz)
    constexpr int SUB_BLOCK = 32;
    float lastRoot   = -1.0f; // force first update
    float lastInharm = -1.0f;

    for (int base = 0; base < numSamples; base += SUB_BLOCK)
    {
        const int blockEnd  = juce::jmin (base + SUB_BLOCK, numSamples);
        const int midSample = base + (blockEnd - base) / 2;

        const float root   = rootModBuf   ? juce::jlimit (20.0f, 2000.0f, baseRoot   + rootModBuf  [midSample] * rootAssign.depth   * 1980.0f) : baseRoot;
        const float inharm = inharmModBuf ? juce::jlimit (0.0f,   0.5f,   baseInharm + inharmModBuf[midSample] * inharmAssign.depth *    0.5f) : baseInharm;

        if (root != lastRoot || inharm != lastInharm)
        {
            updateSlotResoCoeffs (slot, root, q, numH, taper, inharm, qTaper, seriesMode, decaySecs, scatter);
            lastRoot   = root;
            lastInharm = inharm;
        }

        for (int ch = 0; ch < numChannels && ch < 2; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            for (int i = base; i < blockEnd; ++i)
            {
                const float dry = data[i];
                const int readPos = (slot.resoDelayWrite[ch] - slot.resoDelaySamps
                                     + SampleSlot::RES_DELAY_SIZE) & (SampleSlot::RES_DELAY_SIZE - 1);
                const float delayOut   = slot.resoDelayBuf[ch][readPos];
                const float shaped     = std::tanh (dry * 3.0f) * (1.0f / 3.0f);
                const float excitation = dry + drive * (shaped - dry);

                float filtered = 0.0f;
                for (int h = 0; h < numH; ++h)
                {
                    if (slot.resoCoeffs[h].gain == 0.0f) continue;
                    const float y0 = std::tanh (slot.resoCoeffs[h].b0 * excitation
                                   + slot.resoCoeffs[h].b2 * slot.resoStates[ch][h].x2
                                   - slot.resoCoeffs[h].a1 * slot.resoStates[ch][h].y1
                                   - slot.resoCoeffs[h].a2 * slot.resoStates[ch][h].y2);
                    slot.resoStates[ch][h].x2 = slot.resoStates[ch][h].x1;
                    slot.resoStates[ch][h].x1 = excitation;
                    slot.resoStates[ch][h].y2 = slot.resoStates[ch][h].y1;
                    slot.resoStates[ch][h].y1 = y0;
                    filtered += y0 * slot.resoCoeffs[h].gain;
                }
                slot.resoDelayBuf[ch][slot.resoDelayWrite[ch]] = filtered + feedback * delayOut;
                slot.resoDelayWrite[ch] = (slot.resoDelayWrite[ch] + 1) & (SampleSlot::RES_DELAY_SIZE - 1);
                data[i] = dry * (1.0f - mix) + (filtered + delayOut) * mix;
            }
        }
    }
}

void BPMSamplerProcessor::applySlotCharacter (SampleSlot& slot,
                                               juce::AudioBuffer<float>& buffer,
                                               int /*slotIdx*/)
{
    // Hiss
    if (*apvts.getRawParameterValue ("hissEnabled") > 0.5f)
    {
        constexpr float level = 0.0002f;
        const float hpCoeff = 0.0285f;
        const int nch = juce::jmin (buffer.getNumChannels(), 2);
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float noise = slot.hissRng.nextFloat() * 2.0f - 1.0f;
                slot.hissHpState[ch] += (noise - slot.hissHpState[ch]) * hpCoeff;
                data[i] += (noise - slot.hissHpState[ch]) * level;
            }
        }
    }

    // Saturation
    const float satAmount = *apvts.getRawParameterValue ("outputSat");
    if (satAmount > 0.001f)
    {
        const float drive     = 1.0f + satAmount * 7.0f;
        const float bias      = satAmount * 0.08f;
        const float biasTanh  = std::tanh (drive * bias);
        const float sech2     = 1.0f - biasTanh * biasTanh;
        const float invDrive  = 1.0f / (drive * sech2);
        const float lpCoeff   = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                                   * 3000.0f / (float)getSampleRate());
        const float preGain   = satAmount * 2.5f;
        const float deGain    = preGain / (1.0f + preGain);
        for (int ch = 0; ch < juce::jmin (buffer.getNumChannels(), 2); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                slot.satPreLp[ch] += lpCoeff * (data[i] - slot.satPreLp[ch]);
                float y = data[i] + preGain * (data[i] - slot.satPreLp[ch]);
                y = (std::tanh (drive * (y + bias)) - biasTanh) * invDrive;
                slot.satDeLp[ch] += lpCoeff * (y - slot.satDeLp[ch]);
                data[i] = y - deGain * (y - slot.satDeLp[ch]);
            }
        }
    }
}

//==============================================================================
// processOneSlot
//==============================================================================

void BPMSamplerProcessor::processOneSlot (int slotIdx, juce::AudioBuffer<float>& outBuf)
{
    auto& slot       = slots[slotIdx];
    const auto& ptrs = slotPtrs[slotIdx];

    outBuf.clear();
    if (!slot.hasFile() || !slot.hasAnyActiveVoice()) return;

    const int    nSmp    = slot.buffer.getNumSamples();
    const bool   loop    = *ptrs.loop > 0.5f;
    const bool   bpmSync = *ptrs.bpmSync > 0.5f;
    const bool   spdSync = *ptrs.speedSync > 0.5f;
    const int    tsMode  = (int)*ptrs.tsMode;
    const bool   sliceM  = *ptrs.sliceMode > 0.5f;
    const bool   granEn  = *ptrs.granEnabled > 0.5f;
    const bool   midiNM  = *ptrs.midiNoteMode > 0.5f;

    double startSample = (double)slot.startPosCurve.applyToNorm (*ptrs.startPos) * nSmp;
    double endSample   = (double)*ptrs.endPos * nSmp;

    // Apply startPos modulation (block-rate; skipped in slice mode which overrides startSample)
    if (!sliceM && modChanCount > 0)
    {
        const auto& startPosMod = slotMod[slotIdx][kMod_StartPos];
        if (startPosMod.channel >= 0 && startPosMod.channel < modChanCount)
        {
            const float basePosNorm = (float)(startSample / nSmp);
            const float moddedNorm  = applyMod (basePosNorm, startPosMod, modChanPtrs, modChanCount, 0, 0.0f, 1.0f, 1.0f);
            startSample = juce::jlimit (0.0, (double)(nSmp - 1), (double)moddedNorm * nSmp);
        }
    }

    if (sliceM)
    {
        const juce::ScopedTryLock sl (slot.sliceLock);
        if (sl.isLocked() && !slot.slicePositions.empty())
        {
            int idx = juce::jlimit (0, (int)slot.slicePositions.size() - 1, (int)*ptrs.sliceIndex);
            const double ss = slot.slicePositions[idx] * nSmp;
            const double se = (idx + 1 < (int)slot.slicePositions.size())
                              ? slot.slicePositions[idx + 1] * nSmp : (double)nSmp;
            if (loop) { startSample = 0.0; endSample = (double)nSmp; }
            else      { startSample = ss;  endSample = se; }
        }
    }
    if (endSample <= startSample + 1.0) endSample = startSample + 2.0;

    const float  speedNorm = ptrs.speedParam ? ptrs.speedParam->getValue() : *ptrs.speed;
    double speed = (double)slot.speedCurve.applyToNorm (speedNorm);
    if (bpmSync)      speed = calculateEffectiveSpeed (slotIdx, speed, (double)nSmp / slot.sampleRate);
    else if (spdSync) speed = snapToMusicalSpeed (speed);
    speed *= juce::jlimit (0.5, 2.0, (double)*ptrs.speedFine);
    speed = juce::jlimit (0.01, 16.0, speed);

    const float  pitchNorm    = ptrs.pitchParam ? ptrs.pitchParam->getValue() : *ptrs.pitch;
    const double pitchSemitones = (double)slot.pitchCurve.applyToNorm (pitchNorm);
    const double srRatio      = slot.sampleRate / currentSampleRate;
    const int    n            = outBuf.getNumSamples();

    // Precompute envelope parameters for this block (per-slot, evaluated once)
    const bool  envEn        = *ptrs.envEnabled > 0.5f;
    float envAttackStep  = 0.0f, envDecayStep = 0.0f;
    float envSustainLvl  = 1.0f, envReleaseStep = 0.0f;
    if (envEn)
    {
        const float sr32        = (float)currentSampleRate;
        const float attackSamps = juce::jmax (1.0f, *ptrs.envAttack  * 0.001f * sr32);
        const float decaySamps  = juce::jmax (1.0f, *ptrs.envDecay   * 0.001f * sr32);
        envSustainLvl           = juce::jlimit (0.0f, 1.0f, ptrs.envSustain->load());
        const float relSamps    = juce::jmax (1.0f, *ptrs.envRelease * 0.001f * sr32);
        envAttackStep  = 1.0f / attackSamps;
        envDecayStep   = juce::jmax (0.0f, 1.0f - envSustainLvl) / decaySamps;
        envReleaseStep = 1.0f / relSamps;
    }

    // Process each active voice, summing into outBuf
    for (auto& voice : slot.voices)
    {
        if (!voice.active.load()) continue;

        voice.midiNotePitchSemitones = midiNM ? (double)(voice.midiNote - 60) : 0.0;
        updateVoiceJitter (slot, voice, slotIdx);

        voiceMixBuffer.clear();
        if (granEn)
        {
            processSlotGranular (slot, voice, slotIdx, voiceMixBuffer, n, startSample, endSample);
        }
        else if (tsMode > 0)
        {
            processSlotTimestretch (slot, voice, slotIdx, voiceMixBuffer, n, startSample, endSample,
                                    speed, srRatio * voice.currentJitterFactor, loop, tsMode);
        }
        else
        {
            processSlotNormal (slot, voice, slotIdx, voiceMixBuffer, n, startSample, endSample,
                               speed, srRatio, voice.currentJitterFactor,
                               voice.midiNotePitchSemitones, loop);
        }

        // Apply amplitude envelope per-sample
        if (envEn)
        {
            bool done = false;
            for (int i = 0; i < n && !done; ++i)
            {
                switch (voice.envPhase)
                {
                    case VoiceState::EnvPhase::Attack:
                        voice.envValue += envAttackStep;
                        if (voice.envValue >= 1.0f)
                        { voice.envValue = 1.0f; voice.envPhase = VoiceState::EnvPhase::Decay; }
                        break;
                    case VoiceState::EnvPhase::Decay:
                        voice.envValue = juce::jmax (envSustainLvl, voice.envValue - envDecayStep);
                        if (voice.envValue <= envSustainLvl)
                            voice.envPhase = VoiceState::EnvPhase::Sustain;
                        break;
                    case VoiceState::EnvPhase::Sustain:
                        voice.envValue = envSustainLvl;
                        break;
                    case VoiceState::EnvPhase::Release:
                        voice.envValue -= envReleaseStep;
                        if (voice.envValue <= 0.0f)
                        {
                            voice.envValue = 0.0f;
                            voice.envPhase = VoiceState::EnvPhase::Off;
                            voice.active.store (false);
                            voiceMixBuffer.applyGain (i, n - i, 0.0f);
                            done = true;
                        }
                        break;
                    default:
                        break;
                }
                if (!done)
                    voiceMixBuffer.applyGain (i, 1, voice.envValue);
            }
        }

        for (int ch = 0; ch < outBuf.getNumChannels(); ++ch)
            outBuf.addFrom (ch, 0, voiceMixBuffer, ch, 0, n);
    }

    outBuf.applyGain (*ptrs.vol);
    applySlotResonator (slot, outBuf, slotIdx);
    applySlotCharacter (slot, outBuf, slotIdx);
}

//==============================================================================
// processBlock
//==============================================================================

void BPMSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumSamples() == 0)
        return;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    const int sampleMode = (int)*apvts.getRawParameterValue ("sampleMode");
    const int n          = buffer.getNumSamples();

    // --- MIDI ---
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            const int noteNum = msg.getNoteNumber();
            if (sampleMode == 0) // Single
            {
                startSlotPlayback (selectedSlot.load(), noteNum);
            }
            else if (sampleMode == 1) // MIDI Map
            {
                for (int i = 0; i < NUM_SLOTS; ++i)
                {
                    if (slots[i].hasFile() && slots[i].loNote >= 0
                        && noteNum >= slots[i].loNote && noteNum <= slots[i].hiNote)
                    {
                        activeSlot.store (i);
                        startSlotPlayback (i, noteNum);
                        break;
                    }
                }
            }
            else // Morph: trigger by note range if assigned, else trigger all slots
            {
                bool anyRangeAssigned = false;
                for (int i = 0; i < NUM_SLOTS; ++i)
                {
                    if (slots[i].loNote >= 0)
                    {
                        anyRangeAssigned = true;
                        if (slots[i].hasFile() && noteNum >= slots[i].loNote && noteNum <= slots[i].hiNote)
                            startSlotPlayback (i, noteNum);
                    }
                }
                if (!anyRangeAssigned)
                {
                    for (int i = 0; i < NUM_SLOTS; ++i)
                        startSlotPlayback (i, noteNum);
                }
            }
        }
        else if (msg.isNoteOff())
        {
            const int noteNum = msg.getNoteNumber();
            if (sampleMode == 0)
            {
                stopNoteInSlot (selectedSlot.load(), noteNum);
            }
            else if (sampleMode == 1)
            {
                for (int i = 0; i < NUM_SLOTS; ++i)
                    if (slots[i].hasFile() && slots[i].loNote >= 0
                        && noteNum >= slots[i].loNote && noteNum <= slots[i].hiNote)
                        { stopNoteInSlot (i, noteNum); break; }
            }
            else // Morph note-off: mirror the note-on routing
            {
                bool anyRangeAssigned = false;
                for (int i = 0; i < NUM_SLOTS; ++i)
                {
                    if (slots[i].loNote >= 0)
                    {
                        anyRangeAssigned = true;
                        if (noteNum >= slots[i].loNote && noteNum <= slots[i].hiNote)
                            stopNoteInSlot (i, noteNum);
                    }
                }
                if (!anyRangeAssigned)
                {
                    for (int i = 0; i < NUM_SLOTS; ++i)
                        stopNoteInSlot (i, noteNum);
                }
            }
        }
        else if (msg.isController())
        {
            const int cc  = msg.getControllerNumber();
            const int val = msg.getControllerValue();
            if (cc >= 1 && cc <= 8)
            {
                ccMSB[cc - 1] = (uint8_t)val;
            }
            else if (cc >= 33 && cc <= 40)
            {
                const int idx = cc - 33;
                ccLSB[idx] = (uint8_t)val;
                const float norm = (float)((ccMSB[idx] << 7) | ccLSB[idx]) / 16383.0f;
                midiMod[idx].setTargetValue (norm);
            }
        }
    }

    // Fill mod buffer per-sample from smoothed MIDI CC values
    modChanCount = NUM_MOD_CHANNELS;
    for (int i = 0; i < NUM_MOD_CHANNELS; ++i)
    {
        float* buf = modBuffer.getWritePointer (i);
        for (int s = 0; s < numSamples; ++s)
            buf[s] = midiMod[i].getNextValue();
        modChanPtrs[i] = buf;
        const float lastVal = buf[numSamples - 1];
        modChannelLevels[i].store   (lastVal);
        modChannelInstVals[i].store (lastVal);
    }

    // --- Audio ---
    if (sampleMode == 0) // Single
    {
        const int si = selectedSlot.load();
        processOneSlot (si, buffer);
    }
    else if (sampleMode == 1) // MIDI Map
    {
        processOneSlot (activeSlot.load(), buffer);
    }
    else // Morph
    {
        const float mp       = *apvts.getRawParameterValue ("morphPos");
        const int   sa       = juce::jlimit (0, NUM_SLOTS - 2, (int)mp);
        const int   sb       = sa + 1;
        const float frac     = mp - (float)sa;
        const bool  lpcMode  = *apvts.getRawParameterValue ("morphAlgo") > 0.5f;

        if (lpcMode)
        {
            const bool hasA = slots[sa].hasFile() && slots[sa].hasAnyActiveVoice();
            const bool hasB = slots[sb].hasFile() && slots[sb].hasAnyActiveVoice();

            // Reset LPC state when the slot pair changes
            if (sa != lpcLastSa || sb != lpcLastSb)
            {
                lpcMorph.reset();
                lpcLastSa = sa;
                lpcLastSb = sb;
            }

            if (hasA && hasB)
            {
                processOneSlot (sa, lpcBufA);
                processOneSlot (sb, lpcBufB);
                lpcMorph.updateCoeffs (lpcBufA, lpcBufB, n, currentSampleRate);
                lpcMorph.process      (lpcBufA, lpcBufB, buffer, frac, n, currentSampleRate);
            }
            else if (hasA || hasB)
            {
                // One slot missing — pass-through the active one at its volume weight
                const int   activeIdx = hasA ? sa : sb;
                const float g         = hasA ? (1.0f - frac) : frac;
                processOneSlot (activeIdx, morphTempBuffer);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.addFrom (ch, 0, morphTempBuffer, ch, 0, n, g);
                lpcMorph.reset();
            }
        }
        else
        {
            float targetGains[NUM_SLOTS] = {};
            targetGains[sa] = 1.0f - frac;
            targetGains[sb] = frac;

            const float rampStep = (float)n / (0.02f * (float)currentSampleRate);
            float endGains[NUM_SLOTS];
            for (int i = 0; i < NUM_SLOTS; ++i)
            {
                const float delta = juce::jlimit (-rampStep, rampStep, targetGains[i] - morphSlotGain[i]);
                endGains[i] = morphSlotGain[i] + delta;
            }

            for (int i = 0; i < NUM_SLOTS; ++i)
            {
                const float startG = morphSlotGain[i];
                const float endG   = endGains[i];
                if (startG < 0.0001f && endG < 0.0001f) { morphSlotGain[i] = 0.0f; continue; }
                if (!slots[i].hasFile() || !slots[i].hasAnyActiveVoice()) { morphSlotGain[i] = endG; continue; }

                processOneSlot (i, morphTempBuffer);

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.addFromWithRamp (ch, 0, morphTempBuffer.getReadPointer (ch), n,
                                            startG, endG);
                morphSlotGain[i] = endG;
            }
        }

        morphDisplaySa.store (sa);
        morphDisplaySb.store (sb);
        morphDisplayFrac.store (frac);
        morphDisplayPosA.store (slots[sa].hasFile() ? getPlaybackPositionNorm (sa) : 0.0);
        morphDisplayPosB.store (slots[sb].hasFile() ? getPlaybackPositionNorm (sb) : 0.0);
    }

    // Global analog output stage — runs once on the final stereo mix
    globalOutputStage.process (buffer, 1.0f);
}

//==============================================================================
// File loading
//==============================================================================

bool BPMSamplerProcessor::loadAudioFileToSlot (const juce::File& file, int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= NUM_SLOTS) return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) return false;

    juce::AudioBuffer<float> newBuf ((int)reader->numChannels, (int)reader->lengthInSamples);
    reader->read (&newBuf, 0, (int)reader->lengthInSamples, 0, true, true);

    for (auto& voice : slots[slotIndex].voices)
        voice.active.store (false);
    slots[slotIndex].buffer     = std::move (newBuf);
    slots[slotIndex].sampleRate = reader->sampleRate;
    slots[slotIndex].filePath   = file.getFullPathName();

    slotThumbnails[slotIndex]->setSource (new juce::FileInputSource (file));

    if (onSlotLoaded)
        juce::MessageManager::callAsync ([this, slotIndex] { onSlotLoaded (slotIndex); });

    return true;
}

void BPMSamplerProcessor::setSlotNoteRange (int slotIndex, int lo, int hi)
{
    if (slotIndex >= 0 && slotIndex < NUM_SLOTS)
    {
        slots[slotIndex].loNote = lo;
        slots[slotIndex].hiNote = hi;
    }
}

//==============================================================================
// State
//==============================================================================

void BPMSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    for (int i = 0; i < NUM_SLOTS; ++i)
    {
        juce::ValueTree slotNode ("SampleSlot4");
        slotNode.setProperty ("index",    i,                    nullptr);
        slotNode.setProperty ("filePath", slots[i].filePath,    nullptr);
        slotNode.setProperty ("loNote",   slots[i].loNote,      nullptr);
        slotNode.setProperty ("hiNote",   slots[i].hiNote,      nullptr);

        {
            const juce::ScopedLock sl (slots[i].sliceLock);
            juce::ValueTree slicesNode ("Slices");
            for (float pos : slots[i].slicePositions)
            {
                juce::ValueTree sn ("Slice");
                sn.setProperty ("pos", pos, nullptr);
                slicesNode.addChild (sn, -1, nullptr);
            }
            slotNode.addChild (slicesNode, -1, nullptr);
        }

        state.addChild (slotNode, -1, nullptr);
    }

    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // Remove stale curve elements, then write fresh per-slot curves
    for (int i = 0; i < NUM_SLOTS; ++i)
    {
        const juce::String prefix = "S" + juce::String (i);
        for (const char* suffix : { "PitchCurve", "SpeedCurve", "StartPosCurve", "GranPosCurve" })
        {
            const juce::String tag = prefix + suffix;
            while (auto* old = xml->getChildByName (tag))
                xml->removeChildElement (old, true);
        }
        auto saveCurve = [&] (const BPMCurveData& cd, const char* suffix) {
            auto* el = xml->createNewChildElement (prefix + suffix);
            cd.saveToXml (*el);
        };
        saveCurve (slots[i].pitchCurve,    "PitchCurve");
        saveCurve (slots[i].speedCurve,    "SpeedCurve");
        saveCurve (slots[i].startPosCurve, "StartPosCurve");
        saveCurve (slots[i].granPosCurve,  "GranPosCurve");
    }

    // Save mod assignments
    while (auto* old = xml->getChildByName ("AudioMod")) xml->removeChildElement (old, true);
    auto* modEl = xml->createNewChildElement ("AudioMod");
    for (int s = 0; s < NUM_SLOTS; ++s)
        for (int t = 0; t < kNumSlotModTargets; ++t)
        {
            const auto& a = slotMod[s][t];
            if (a.channel < 0) continue;
            auto* el = modEl->createNewChildElement ("SM");
            el->setAttribute ("s", s);
            el->setAttribute ("t", t);
            el->setAttribute ("ch", a.channel);
            el->setAttribute ("d", (double)a.depth);
        }
    for (int t = 0; t < kNumGlobalModTargets; ++t)
    {
        const auto& a = globalMod[t];
        if (a.channel < 0) continue;
        auto* el = modEl->createNewChildElement ("GM");
        el->setAttribute ("t", t);
        el->setAttribute ("ch", a.channel);
        el->setAttribute ("d", (double)a.depth);
    }

    copyXmlToBinary (*xml, destData);
}

void BPMSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr) return;

    // Load per-slot curves
    for (int i = 0; i < NUM_SLOTS; ++i)
    {
        const juce::String prefix = "S" + juce::String (i);
        auto loadCurve = [&] (BPMCurveData& cd, const char* suffix) {
            juce::XmlElement* last = nullptr;
            for (auto* ch = xml->getFirstChildElement(); ch != nullptr; ch = ch->getNextElement())
                if (ch->hasTagName (prefix + suffix)) last = ch;
            if (last) cd.loadFromXml (*last);
        };
        loadCurve (slots[i].pitchCurve,    "PitchCurve");
        loadCurve (slots[i].speedCurve,    "SpeedCurve");
        loadCurve (slots[i].startPosCurve, "StartPosCurve");
        loadCurve (slots[i].granPosCurve,  "GranPosCurve");
    }

    auto state = juce::ValueTree::fromXml (*xml);
    if (!state.isValid()) return;

    // Extract SampleSlot4 nodes
    for (int i = state.getNumChildren() - 1; i >= 0; --i)
    {
        auto child = state.getChild (i);
        if (child.hasType ("SampleSlot4"))
        {
            const int idx = (int)child.getProperty ("index", -1);
            if (idx >= 0 && idx < NUM_SLOTS)
            {
                slots[idx].loNote = (int)child.getProperty ("loNote", -1);
                slots[idx].hiNote = (int)child.getProperty ("hiNote", -1);

                auto slicesNode = child.getChildWithName ("Slices");
                if (slicesNode.isValid())
                {
                    std::vector<float> sv;
                    for (int j = 0; j < slicesNode.getNumChildren(); ++j)
                        sv.push_back ((float)(double)slicesNode.getChild (j).getProperty ("pos", 0.0f));
                    std::sort (sv.begin(), sv.end());
                    const juce::ScopedLock sl (slots[idx].sliceLock);
                    slots[idx].slicePositions = std::move (sv);
                }

                const juce::String path = child.getProperty ("filePath", "").toString();
                if (path.isNotEmpty())
                {
                    const juce::File f (path);
                    if (f.existsAsFile()) loadAudioFileToSlot (f, idx);
                }
            }
            state.removeChild (child, nullptr);
        }
    }

    // Remove curve XML children before passing to replaceState
    for (int i = 0; i < NUM_SLOTS; ++i)
    {
        const juce::String prefix = "S" + juce::String (i);
        for (const char* suffix : { "PitchCurve", "SpeedCurve", "StartPosCurve", "GranPosCurve" })
        {
            auto child = state.getChildWithName (prefix + suffix);
            if (child.isValid()) state.removeChild (child, nullptr);
        }
    }

    // Load mod assignments
    if (auto* modEl = xml->getChildByName ("AudioMod"))
    {
        for (auto* el = modEl->getFirstChildElement(); el != nullptr; el = el->getNextElement())
        {
            if (el->hasTagName ("SM"))
            {
                const int s = el->getIntAttribute ("s", -1);
                const int t = el->getIntAttribute ("t", -1);
                if (s >= 0 && s < NUM_SLOTS && t >= 0 && t < kNumSlotModTargets)
                {
                    slotMod[s][t].channel = el->getIntAttribute ("ch", -1);
                    slotMod[s][t].depth   = (float)el->getDoubleAttribute ("d", 0.0);
                }
            }
            else if (el->hasTagName ("GM"))
            {
                const int t = el->getIntAttribute ("t", -1);
                if (t >= 0 && t < kNumGlobalModTargets)
                {
                    globalMod[t].channel = el->getIntAttribute ("ch", -1);
                    globalMod[t].depth   = (float)el->getDoubleAttribute ("d", 0.0);
                }
            }
        }
    }

    apvts.replaceState (state);
}

//==============================================================================

juce::AudioProcessorEditor* BPMSamplerProcessor::createEditor()
{
    return new BPMSamplerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BPMSamplerProcessor();
}
