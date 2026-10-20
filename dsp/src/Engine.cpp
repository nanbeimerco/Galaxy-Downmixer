#include "gdm/Engine.h"

#include <algorithm>
#include <cmath>

#include "gdm/AudioWriter.h"

namespace gdm {

struct Engine::Prepared {
    float coeffL[kMaxChannelBits] = {};
    float coeffR[kMaxChannelBits] = {};
    float muteTarget[kMaxChannelBits] = {};

    bool lfeLowPass = true;
    BiquadCoeffs lfe[2];

    int delaySamples = 0;
    float masterGain = 1.0f;

    int commonCount = 0;
    double commonGain = 1.0;
    BiquadCoeffs common[kMaxPeqFilters];

    int earCount[2] = {0, 0};
    double earGain[2] = {1.0, 1.0};
    BiquadCoeffs ear[2][kMaxPeqFilters];

    float swapTarget = 0.0f;
    float rampStep = 1.0f;  // per-sample increment for mute/swap ramps
};

namespace {

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

int resolveDelaySamples(double ms, double sampleRate) {
    const int maxSamples = static_cast<int>(std::ceil(Engine::kMaxDelayMs * sampleRate / 1000.0));
    const int d = static_cast<int>(std::lround(ms * sampleRate / 1000.0));
    return std::clamp(d, 0, maxSamples);
}

int prepareSection(const PeqSection& section, bool enabled, double sampleRate,
                   BiquadCoeffs* out, double* gain) {
    if (!enabled) {
        *gain = 1.0;
        return 0;
    }
    *gain = dbToLinear(section.preampDb);
    const int n = std::min(static_cast<int>(section.filters.size()), kMaxPeqFilters);
    for (int i = 0; i < n; ++i) out[i] = designBiquad(section.filters[i], sampleRate);
    return n;
}

inline float rampToward(float cur, float target, float step) {
    if (cur < target) return std::min(cur + step, target);
    if (cur > target) return std::max(cur - step, target);
    return cur;
}

}  // namespace

Engine::Engine() = default;

Engine::~Engine() {
    delete active_;
    delete pending_.exchange(nullptr);
    delete retired_.exchange(nullptr);
}

bool Engine::configure(double sampleRate, uint32_t channelMask) {
    const std::vector<uint32_t> bits = channelsInMask(channelMask);
    if (bits.empty() || sampleRate <= 0.0) return false;

    std::lock_guard<std::mutex> lock(controlMutex_);
    sampleRate_ = sampleRate;
    channelMask_ = channelMask;
    numChannels_ = static_cast<int>(bits.size());
    idxFL_ = idxFR_ = -1;
    for (int i = 0; i < kMaxChannelBits; ++i) {
        channelBits_[i] = i < numChannels_ ? bits[i] : 0;
        isLfe_[i] = i < numChannels_ && isLfeBit(bits[i]);
        if (channelBits_[i] == kFL) idxFL_ = i;
        if (channelBits_[i] == kFR) idxFR_ = i;
    }

    const size_t maxDelay =
        static_cast<size_t>(std::ceil(kMaxDelayMs * sampleRate / 1000.0)) + 1;
    delayL_.assign(maxDelay, 0.0f);
    delayR_.assign(maxDelay, 0.0f);

    delete pending_.exchange(nullptr);
    delete retired_.exchange(nullptr);
    delete active_;
    active_ = build(settings_);
    commonCount_ = active_->commonCount;
    earCount_[0] = active_->earCount[0];
    earCount_[1] = active_->earCount[1];
    snapRamps(*active_);
    reset();
    readMeters();
    return true;
}

void Engine::setSettings(const Settings& settings) {
    std::lock_guard<std::mutex> lock(controlMutex_);
    settings_ = settings;
    if (numChannels_ == 0) return;  // applied by configure()

    delete retired_.exchange(nullptr, std::memory_order_acq_rel);
    Prepared* p = build(settings_);
    delete pending_.exchange(p, std::memory_order_acq_rel);
}

Settings Engine::settings() const {
    std::lock_guard<std::mutex> lock(controlMutex_);
    return settings_;
}

int Engine::dryDelaySamples() const {
    std::lock_guard<std::mutex> lock(controlMutex_);
    return resolveDelaySamples(settings_.dryDelayMs, sampleRate_);
}

void Engine::setPreEqWriter(AudioWriter* writer) {
    preEqWriter_.store(writer, std::memory_order_release);
}

AudioWriter* Engine::preEqWriter() const {
    return preEqWriter_.load(std::memory_order_acquire);
}

Engine::Prepared* Engine::build(const Settings& s) const {
    auto* p = new Prepared();
    const bool mono = numChannels_ == 1 && channelBits_[0] == kFL;
    for (int i = 0; i < numChannels_; ++i) {
        const uint32_t bit = channelBits_[i];
        MixCoeff c = s.matrix.get(bit);
        if (mono) c = {1.0f, 1.0f};
        p->coeffL[i] = c.l;
        p->coeffR[i] = c.r;
        p->muteTarget[i] = (s.muteMask & bit) ? 0.0f : 1.0f;
    }

    p->lfeLowPass = s.lfeLowPassEnabled;
    const auto lp = butterworthLowPass4(s.lfeCutoffHz);
    p->lfe[0] = designBiquad(lp[0], sampleRate_);
    p->lfe[1] = designBiquad(lp[1], sampleRate_);

    p->delaySamples = resolveDelaySamples(s.dryDelayMs, sampleRate_);
    p->masterGain = static_cast<float>(dbToLinear(s.masterGainDb));

    p->commonCount = prepareSection(s.commonPeq, s.commonPeqEnabled, sampleRate_,
                                    p->common, &p->commonGain);
    p->earCount[0] = prepareSection(s.leftPeq, s.earPeqEnabled, sampleRate_,
                                    p->ear[0], &p->earGain[0]);
    p->earCount[1] = prepareSection(s.rightPeq, s.earPeqEnabled, sampleRate_,
                                    p->ear[1], &p->earGain[1]);

    p->swapTarget = s.swapLR ? 1.0f : 0.0f;
    const double rampSamples = std::max(1.0, s.rampMs * sampleRate_ / 1000.0);
    p->rampStep = static_cast<float>(1.0 / rampSamples);
    return p;
}

void Engine::snapRamps(const Prepared& p) {
    for (int i = 0; i < kMaxChannelBits; ++i) muteGain_[i] = p.muteTarget[i];
    swap_ = p.swapTarget;
}

void Engine::installPending() {
    // Only swap when the previous retiree has been collected, so the audio
    // thread never has to free memory itself.
    if (retired_.load(std::memory_order_acquire) != nullptr) return;
    Prepared* p = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if (p == nullptr) return;

    if (p->commonCount != commonCount_) {
        for (auto& st : commonState_) for (auto& b : st) b.reset();
        commonCount_ = p->commonCount;
    }
    for (int ch = 0; ch < 2; ++ch) {
        if (p->earCount[ch] != earCount_[ch]) {
            for (auto& b : earState_[ch]) b.reset();
            earCount_[ch] = p->earCount[ch];
        }
    }
    retired_.store(active_, std::memory_order_release);
    active_ = p;
}

void Engine::reset() {
    std::fill(delayL_.begin(), delayL_.end(), 0.0f);
    std::fill(delayR_.begin(), delayR_.end(), 0.0f);
    delayPos_ = 0;
    for (auto& ch : lfeState_) for (auto& b : ch) b.reset();
    for (auto& st : commonState_) for (auto& b : st) b.reset();
    for (auto& st : earState_) for (auto& b : st) b.reset();
}

void Engine::process(const float* in, float* out, size_t frames) {
    if (active_ == nullptr || numChannels_ == 0) return;
    installPending();
    const Prepared& p = *active_;
    const int n = numChannels_;
    const size_t delaySize = delayL_.size();
    const size_t delay = static_cast<size_t>(p.delaySamples);
    AudioWriter* preEqWriter = preEqWriter_.load(std::memory_order_relaxed);

    float inPeak[kMaxChannelBits] = {};
    float inSum[kMaxChannelBits] = {};
    float mixPeak[2] = {}, mixSum[2] = {};
    float outPeak[2] = {}, outSum[2] = {};
    uint32_t clipped = 0;

    for (size_t f = 0; f < frames; ++f) {
        const float* x = in + f * n;

        float dryL = 0.0f, dryR = 0.0f, lfeL = 0.0f, lfeR = 0.0f;
        for (int ch = 0; ch < n; ++ch) {
            const float v = x[ch];
            const float a = std::fabs(v);
            if (a > inPeak[ch]) inPeak[ch] = a;
            inSum[ch] += v * v;

            muteGain_[ch] = rampToward(muteGain_[ch], p.muteTarget[ch], p.rampStep);
            const float g = v * muteGain_[ch];

            if (isLfe_[ch] && p.lfeLowPass) {
                double y = lfeState_[ch][0].process(p.lfe[0], g);
                y = lfeState_[ch][1].process(p.lfe[1], y);
                lfeL += p.coeffL[ch] * static_cast<float>(y);
                lfeR += p.coeffR[ch] * static_cast<float>(y);
            } else {
                dryL += p.coeffL[ch] * g;
                dryR += p.coeffR[ch] * g;
            }
        }

        // The dry delay is common to every non-LFE channel, so it is applied
        // once to the summed L/R instead of per input channel.
        delayL_[delayPos_] = dryL;
        delayR_[delayPos_] = dryR;
        const size_t readPos = (delayPos_ + delaySize - delay) % delaySize;
        double L = delayL_[readPos] + lfeL;
        double R = delayR_[readPos] + lfeR;
        delayPos_ = (delayPos_ + 1) % delaySize;

        {
            const float al = std::fabs(static_cast<float>(L));
            const float ar = std::fabs(static_cast<float>(R));
            if (al > mixPeak[0]) mixPeak[0] = al;
            if (ar > mixPeak[1]) mixPeak[1] = ar;
            mixSum[0] += static_cast<float>(L * L);
            mixSum[1] += static_cast<float>(R * R);
        }

        L *= p.masterGain;
        R *= p.masterGain;

        if (preEqWriter != nullptr) {
            preEqWriter->push(static_cast<float>(L), static_cast<float>(R));
        }

        L *= p.commonGain;
        R *= p.commonGain;
        for (int i = 0; i < p.commonCount; ++i) {
            L = commonState_[0][i].process(p.common[i], L);
            R = commonState_[1][i].process(p.common[i], R);
        }

        swap_ = rampToward(swap_, p.swapTarget, p.rampStep);
        if (swap_ != 0.0f) {
            const double s = swap_;
            const double l = L, r = R;
            L = (1.0 - s) * l + s * r;
            R = (1.0 - s) * r + s * l;
        }

        L *= p.earGain[0];
        for (int i = 0; i < p.earCount[0]; ++i) L = earState_[0][i].process(p.ear[0][i], L);
        R *= p.earGain[1];
        for (int i = 0; i < p.earCount[1]; ++i) R = earState_[1][i].process(p.ear[1][i], R);

        const float outL = static_cast<float>(L);
        const float outR = static_cast<float>(R);
        {
            const float al = std::fabs(outL), ar = std::fabs(outR);
            if (al > outPeak[0]) outPeak[0] = al;
            if (ar > outPeak[1]) outPeak[1] = ar;
            outSum[0] += outL * outL;
            outSum[1] += outR * outR;
            if (al > 1.0f) ++clipped;
            if (ar > 1.0f) ++clipped;
        }

        // Every input sample of this frame has been read, so writing in place
        // is safe from here on.
        float* y = out + f * n;
        for (int ch = 0; ch < n; ++ch) y[ch] = 0.0f;
        if (idxFL_ >= 0 && idxFR_ >= 0) {
            y[idxFL_] = outL;
            y[idxFR_] = outR;
        } else if (idxFL_ >= 0) {
            y[idxFL_] = 0.5f * (outL + outR);
        } else if (n >= 2) {
            y[0] = outL;
            y[1] = outR;
        }
    }

    for (auto& ch : lfeState_) for (auto& b : ch) b.flushDenormals();
    for (auto& st : commonState_) for (auto& b : st) b.flushDenormals();
    for (auto& st : earState_) for (auto& b : st) b.flushDenormals();

    for (int ch = 0; ch < n; ++ch) inMeter_[ch].add(inPeak[ch], inSum[ch]);
    for (int i = 0; i < 2; ++i) {
        mixMeter_[i].add(mixPeak[i], mixSum[i]);
        outMeter_[i].add(outPeak[i], outSum[i]);
    }
    clipped_.fetch_add(clipped, std::memory_order_relaxed);
    meterFrames_.fetch_add(static_cast<uint32_t>(frames), std::memory_order_relaxed);
}

MeterSnapshot Engine::readMeters() {
    MeterSnapshot s;
    s.numChannels = numChannels_;
    s.frames = meterFrames_.exchange(0, std::memory_order_relaxed);
    for (int ch = 0; ch < kMaxChannelBits; ++ch) {
        s.channelBits[ch] = channelBits_[ch];
        s.input[ch] = inMeter_[ch].take(s.frames);
    }
    for (int i = 0; i < 2; ++i) {
        s.mix[i] = mixMeter_[i].take(s.frames);
        s.out[i] = outMeter_[i].take(s.frames);
    }
    s.clippedSamples = clipped_.exchange(0, std::memory_order_relaxed);
    return s;
}

}  // namespace gdm
