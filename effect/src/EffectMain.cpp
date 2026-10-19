// AOSP audio effect library wrapping gdm::Engine.
//
// Loaded by the audio HAL (android.hardware.audio.service) through
// audio_effects_sec.xml and attached to every music stream session as a
// post-processing insert effect, where it receives the track's own channel
// layout (7.1.4 for Apple Music Atmos) before AudioFlinger downmixes it.

#include <android/log.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "Protocol.h"
#include "aosp_effect.h"
#include "gdm/ApoParser.h"
#include "gdm/Engine.h"
#include "gdm/Options.h"

#define LOG_TAG "GalaxyDownmixer"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

namespace proto = gdm::protocol;

constexpr char kOptionsPath[] = "/vendor/etc/galaxy_downmixer/options.conf";
constexpr char kDefaultEqPath[] = "/vendor/etc/galaxy_downmixer/eq.txt";
constexpr size_t kChunkFrames = 256;

// type 24c7d03d-8e80-43c2-94a8-34770cb8db35
// uuid a6e3f002-29bf-40a2-bfcf-df47efb3a224 (referenced by audio_effects_sec.xml)
const effect_descriptor_t kDescriptor = {
    {0x24c7d03d, 0x8e80, 0x43c2, 0x94a8, {0x34, 0x77, 0x0c, 0xb8, 0xdb, 0x35}},
    {0xa6e3f002, 0x29bf, 0x40a2, 0xbfcf, {0xdf, 0x47, 0xef, 0xb3, 0xa2, 0x24}},
    EFFECT_CONTROL_API_VERSION,
    EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_FIRST,
    50,   // cpuLoad, 0.1 MIPS units
    128,  // memoryUsage, KB
    "Galaxy Downmixer",
    "Galaxy Downmixer project",
};

int64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

std::string maskNames(uint32_t mask) {
    std::string s;
    for (uint32_t bit : gdm::channelsInMask(mask)) {
        if (!s.empty()) s += ' ';
        s += gdm::channelName(bit);
    }
    return s;
}

bool readFile(const char* path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

struct Instance;

// State shared by every instance in the HAL process.
struct Global {
    std::mutex mutex;  // guards everything below except the atomics
    std::vector<Instance*> instances;
    gdm::Settings settings;
    bool loaded = false;

    std::atomic<bool> passthrough{false};
    std::atomic<int> logIntervalS{5};
    std::atomic<int64_t> lastMeterReadNs{0};
};

Global& global() {
    static Global* g = new Global();  // never destroyed: the HAL may unload us late
    return *g;
}

struct Instance {
    const effect_interface_s* itfe = nullptr;  // must stay the first member
    int32_t session = 0;
    int32_t io = 0;

    gdm::Engine engine;
    effect_config_t config{};
    bool configured = false;
    uint8_t format = GDM_AUDIO_FORMAT_PCM_FLOAT;
    bool accumulate = false;
    std::vector<float> scratchIn, scratchOut;

    std::atomic<bool> enabled{false};
    std::atomic_flag busy = ATOMIC_FLAG_INIT;  // SET_CONFIG/RESET vs process()

    std::atomic<uint64_t> frames{0};
    std::atomic<int64_t> lastProcessNs{0};
    int64_t lastLogNs = 0;
    bool loggedFirstBlock = false;
};

void lockBusy(Instance* e) {
    while (e->busy.test_and_set(std::memory_order_acquire)) sched_yield();
}
void unlockBusy(Instance* e) { e->busy.clear(std::memory_order_release); }

// --- settings ---------------------------------------------------------------

void loadFilesLocked(Global& g) {
    gdm::Settings s;
    std::string text;
    gdm::Options opts;
    if (readFile(kOptionsPath, &text)) {
        opts = gdm::parseOptions(text, s);
        for (const auto& w : opts.warnings) ALOGW("%s", w.c_str());
    } else {
        ALOGI("no %s, using defaults", kOptionsPath);
    }

    const std::string eqPath = opts.eqFile.empty() ? kDefaultEqPath : opts.eqFile;
    std::string eqText;
    if (readFile(eqPath.c_str(), &eqText)) {
        const gdm::ApoProfile profile = gdm::parseApo(eqText);
        for (const auto& w : profile.warnings) ALOGW("eq %s", w.c_str());
        gdm::applyProfile(profile, s);
    } else if (!opts.eqFile.empty()) {
        ALOGW("cannot read eq_file %s", eqPath.c_str());
    }

    g.settings = s;
    g.passthrough.store(!opts.enabled);
    g.logIntervalS.store(opts.logIntervalS);
    g.loaded = true;
    ALOGI("settings: enabled=%d swap=%d mute=0x%X master=%.2f dB lfe_lpf=%d "
          "eq=%s (common %zu, L %zu, R %zu filters) log=%ds",
          opts.enabled ? 1 : 0, s.swapLR ? 1 : 0, s.muteMask, s.masterGainDb,
          s.lfeLowPassEnabled ? 1 : 0, eqPath.c_str(), s.commonPeq.filters.size(),
          s.leftPeq.filters.size(), s.rightPeq.filters.size(), opts.logIntervalS);
}

void pushSettingsLocked(Global& g) {
    for (Instance* e : g.instances) e->engine.setSettings(g.settings);
}

Instance* mostRecentLocked(Global& g, Instance* fallback) {
    Instance* best = fallback;
    int64_t bestNs = fallback ? fallback->lastProcessNs.load() : -1;
    for (Instance* e : g.instances) {
        const int64_t t = e->lastProcessNs.load();
        if (t > bestNs) {
            best = e;
            bestNs = t;
        }
    }
    return best;
}

// --- processing ---------------------------------------------------------------

inline int16_t toS16(float v) {
    const float s = v * 32768.0f;
    if (s >= 32767.0f) return 32767;
    if (s <= -32768.0f) return -32768;
    return static_cast<int16_t>(s < 0 ? s - 0.5f : s + 0.5f);
}

void passThrough(Instance* e, audio_buffer_t* in, audio_buffer_t* out, size_t frames) {
    const size_t samples = frames * static_cast<size_t>(e->engine.numChannels());
    if (e->format == GDM_AUDIO_FORMAT_PCM_FLOAT) {
        if (e->accumulate) {
            for (size_t i = 0; i < samples; ++i) out->f32[i] += in->f32[i];
        } else if (in->raw != out->raw) {
            memmove(out->f32, in->f32, samples * sizeof(float));
        }
    } else {
        if (e->accumulate) {
            for (size_t i = 0; i < samples; ++i)
                out->s16[i] = toS16((out->s16[i] + in->s16[i]) / 32768.0f);
        } else if (in->raw != out->raw) {
            memmove(out->s16, in->s16, samples * sizeof(int16_t));
        }
    }
}

void runEngine(Instance* e, audio_buffer_t* in, audio_buffer_t* out, size_t frames) {
    if (e->format == GDM_AUDIO_FORMAT_PCM_FLOAT && !e->accumulate) {
        e->engine.process(in->f32, out->f32, frames);
        return;
    }
    const size_t n = static_cast<size_t>(e->engine.numChannels());
    for (size_t pos = 0; pos < frames; pos += kChunkFrames) {
        const size_t len = std::min(kChunkFrames, frames - pos);
        const size_t base = pos * n, count = len * n;
        float* si = e->scratchIn.data();
        float* so = e->scratchOut.data();
        if (e->format == GDM_AUDIO_FORMAT_PCM_FLOAT) {
            memcpy(si, in->f32 + base, count * sizeof(float));
        } else {
            for (size_t i = 0; i < count; ++i) si[i] = in->s16[base + i] / 32768.0f;
        }
        e->engine.process(si, so, len);
        if (e->format == GDM_AUDIO_FORMAT_PCM_FLOAT) {
            for (size_t i = 0; i < count; ++i) out->f32[base + i] += so[i];  // accumulate
        } else if (e->accumulate) {
            for (size_t i = 0; i < count; ++i)
                out->s16[base + i] = toS16(out->s16[base + i] / 32768.0f + so[i]);
        } else {
            for (size_t i = 0; i < count; ++i) out->s16[base + i] = toS16(so[i]);
        }
    }
}

// Periodic meter line in logcat, skipped while a controller is polling meters
// (reading them clears the accumulators).
void maybeLog(Instance* e, int64_t now) {
    Global& g = global();
    const int interval = g.logIntervalS.load(std::memory_order_relaxed);
    if (interval <= 0) return;
    if (now - e->lastLogNs < static_cast<int64_t>(interval) * 1000000000LL) return;
    if (now - g.lastMeterReadNs.load(std::memory_order_relaxed) < 2000000000LL) return;
    e->lastLogNs = now;

    const gdm::MeterSnapshot m = e->engine.readMeters();
    char line[1024];
    int len = snprintf(line, sizeof(line), "session %d peak dBFS |", e->session);
    for (int ch = 0; ch < m.numChannels && len < static_cast<int>(sizeof(line)) - 32; ++ch) {
        len += snprintf(line + len, sizeof(line) - len, " %s %.1f", gdm::channelName(m.channelBits[ch]),
                        gdm::linearToDbfs(m.input[ch].peak));
    }
    if (len < static_cast<int>(sizeof(line)) - 96) {
        snprintf(line + len, sizeof(line) - len, " | mix %.1f %.1f | out %.1f %.1f | clip %u",
                 gdm::linearToDbfs(m.mix[0].peak), gdm::linearToDbfs(m.mix[1].peak),
                 gdm::linearToDbfs(m.out[0].peak), gdm::linearToDbfs(m.out[1].peak),
                 m.clippedSamples);
    }
    ALOGI("%s", line);
}

int32_t Process(effect_handle_t self, audio_buffer_t* in, audio_buffer_t* out) {
    auto* e = reinterpret_cast<Instance*>(self);
    if (e == nullptr || in == nullptr || out == nullptr || in->raw == nullptr || out->raw == nullptr)
        return -EINVAL;
    if (e->busy.test_and_set(std::memory_order_acquire)) return 0;  // reconfiguring

    if (!e->configured) {
        unlockBusy(e);
        return -EINVAL;
    }
    const size_t frames = std::min(in->frameCount, out->frameCount);
    const bool on = e->enabled.load(std::memory_order_relaxed);
    if (!on || global().passthrough.load(std::memory_order_relaxed)) {
        passThrough(e, in, out, frames);
    } else {
        runEngine(e, in, out, frames);
    }

    const int64_t now = nowNs();
    e->frames.fetch_add(frames, std::memory_order_relaxed);
    e->lastProcessNs.store(now, std::memory_order_relaxed);
    if (!e->loggedFirstBlock) {
        e->loggedFirstBlock = true;
        e->lastLogNs = now;
        ALOGI("session %d: first block, %zu frames x %d ch", e->session, frames,
              e->engine.numChannels());
    }
    maybeLog(e, now);
    unlockBusy(e);
    return on ? 0 : -ENODATA;
}

// --- commands -------------------------------------------------------------------

int32_t setConfig(Instance* e, const effect_config_t& cfg) {
    const buffer_config_t& i = cfg.inputCfg;
    const buffer_config_t& o = cfg.outputCfg;
    if (i.samplingRate != o.samplingRate || i.channels != o.channels || i.format != o.format) {
        ALOGW("session %d: rejecting config in(%u Hz, 0x%X, fmt %u) != out(%u Hz, 0x%X, fmt %u)",
              e->session, i.samplingRate, i.channels, i.format, o.samplingRate, o.channels, o.format);
        return -EINVAL;
    }
    if ((i.format != GDM_AUDIO_FORMAT_PCM_FLOAT && i.format != GDM_AUDIO_FORMAT_PCM_16_BIT) ||
        (i.channels & GDM_AUDIO_CHANNEL_REPRESENTATION_MASK) != 0 ||
        (o.accessMode != EFFECT_BUFFER_ACCESS_WRITE && o.accessMode != EFFECT_BUFFER_ACCESS_ACCUMULATE)) {
        ALOGW("session %d: rejecting config fmt %u mask 0x%X access %u", e->session, i.format,
              i.channels, o.accessMode);
        return -EINVAL;
    }

    lockBusy(e);
    const bool ok = e->engine.configure(i.samplingRate, i.channels);
    if (ok) {
        e->config = cfg;
        e->format = i.format;
        e->accumulate = o.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE;
        const size_t n = static_cast<size_t>(e->engine.numChannels());
        e->scratchIn.assign(kChunkFrames * n, 0.0f);
        e->scratchOut.assign(kChunkFrames * n, 0.0f);
        e->configured = true;
        e->loggedFirstBlock = false;
    }
    unlockBusy(e);

    if (!ok) {
        ALOGW("session %d: unsupported layout 0x%X", e->session, i.channels);
        return -EINVAL;
    }
    ALOGI("session %d: configured %u Hz, mask 0x%X (%d ch: %s), format %s, %s", e->session,
          i.samplingRate, i.channels, e->engine.numChannels(), maskNames(i.channels).c_str(),
          i.format == GDM_AUDIO_FORMAT_PCM_FLOAT ? "float" : "s16",
          e->accumulate ? "accumulate" : "write");
    return 0;
}

size_t paddedSize(uint32_t psize) { return ((psize + 3) / 4) * 4; }

template <typename T>
bool readValue(const effect_param_t* p, T* out) {
    if (p->vsize < sizeof(T)) return false;
    memcpy(out, p->data + paddedSize(p->psize), sizeof(T));
    return true;
}

int32_t setParam(Instance* e, const effect_param_t* p) {
    if (p->psize < sizeof(int32_t)) return -EINVAL;
    int32_t key = 0;
    memcpy(&key, p->data, sizeof(key));
    Global& g = global();
    std::lock_guard<std::mutex> lock(g.mutex);

    switch (key) {
        case proto::kParamEnabled: {
            int32_t v = 0;
            if (!readValue(p, &v)) return -EINVAL;
            g.passthrough.store(v == 0);
            ALOGI("session %d: processing %s", e->session, v ? "on" : "off (pass-through)");
            return 0;
        }
        case proto::kParamMuteMask: {
            uint32_t v = 0;
            if (!readValue(p, &v)) return -EINVAL;
            g.settings.muteMask = v;
            break;
        }
        case proto::kParamSwap: {
            int32_t v = 0;
            if (!readValue(p, &v)) return -EINVAL;
            g.settings.swapLR = v != 0;
            break;
        }
        case proto::kParamMasterGainDb: {
            float v = 0.0f;
            if (!readValue(p, &v) || !(v >= -60.0f && v <= 24.0f)) return -EINVAL;
            g.settings.masterGainDb = v;
            break;
        }
        case proto::kParamReloadFiles:
            loadFilesLocked(g);
            break;
        default:
            return -EINVAL;
    }
    pushSettingsLocked(g);
    return 0;
}

// Fills the value part of `reply` (whose header and key are already copied).
int32_t getParam(Instance* e, effect_param_t* reply, uint32_t maxValueSize) {
    int32_t key = 0;
    memcpy(&key, reply->data, sizeof(key));
    char* value = reply->data + paddedSize(reply->psize);
    Global& g = global();
    std::lock_guard<std::mutex> lock(g.mutex);

    auto put = [&](const void* src, uint32_t size) -> int32_t {
        if (size > maxValueSize) return -EINVAL;
        memcpy(value, src, size);
        reply->vsize = size;
        return 0;
    };

    switch (key) {
        case proto::kParamVersion: {
            const int32_t v = proto::kVersion;
            return put(&v, sizeof(v));
        }
        case proto::kParamStatus: {
            Instance* a = mostRecentLocked(g, e);
            proto::Status s{};
            s.sampleRate = a->config.inputCfg.samplingRate;
            s.channelMask = a->config.inputCfg.channels;
            s.format = a->format;
            s.enabled = a->enabled.load() ? 1 : 0;
            s.framesProcessed = a->frames.load();
            s.sessionId = a->session;
            s.instanceCount = static_cast<int32_t>(g.instances.size());
            return put(&s, sizeof(s));
        }
        case proto::kParamMeters: {
            Instance* a = mostRecentLocked(g, e);
            const gdm::MeterSnapshot m = a->engine.readMeters();
            g.lastMeterReadNs.store(nowNs());
            proto::Meters out{};
            out.numChannels = static_cast<uint32_t>(m.numChannels);
            for (int ch = 0; ch < gdm::kMaxChannelBits; ++ch) {
                out.channelBits[ch] = m.channelBits[ch];
                out.inPeak[ch] = m.input[ch].peak;
                out.inRms[ch] = m.input[ch].rms;
            }
            for (int i = 0; i < 2; ++i) {
                out.mixPeak[i] = m.mix[i].peak;
                out.mixRms[i] = m.mix[i].rms;
                out.outPeak[i] = m.out[i].peak;
                out.outRms[i] = m.out[i].rms;
            }
            out.clippedSamples = m.clippedSamples;
            out.frames = m.frames;
            return put(&out, sizeof(out));
        }
        case proto::kParamEnabled: {
            const int32_t v = g.passthrough.load() ? 0 : 1;
            return put(&v, sizeof(v));
        }
        case proto::kParamMuteMask:
            return put(&g.settings.muteMask, sizeof(uint32_t));
        case proto::kParamSwap: {
            const int32_t v = g.settings.swapLR ? 1 : 0;
            return put(&v, sizeof(v));
        }
        case proto::kParamMasterGainDb: {
            const float v = static_cast<float>(g.settings.masterGainDb);
            return put(&v, sizeof(v));
        }
        default:
            return -EINVAL;
    }
}

void dump(Instance* e, int fd) {
    dprintf(fd,
            "Galaxy Downmixer: session %d io %d, %u Hz, mask 0x%X (%d ch: %s), format %u, "
            "enabled %d, pass-through %d, frames %llu\n",
            e->session, e->io, e->config.inputCfg.samplingRate, e->config.inputCfg.channels,
            e->engine.numChannels(), maskNames(e->config.inputCfg.channels).c_str(), e->format,
            e->enabled.load() ? 1 : 0, global().passthrough.load() ? 1 : 0,
            static_cast<unsigned long long>(e->frames.load()));
}

bool replyInt(uint32_t* replySize, void* pReply, int32_t v) {
    if (replySize == nullptr || pReply == nullptr || *replySize < sizeof(int32_t)) return false;
    *static_cast<int32_t*>(pReply) = v;
    *replySize = sizeof(int32_t);
    return true;
}

int32_t Command(effect_handle_t self, uint32_t cmd, uint32_t cmdSize, void* pCmd,
                uint32_t* replySize, void* pReply) {
    auto* e = reinterpret_cast<Instance*>(self);
    if (e == nullptr) return -EINVAL;

    switch (cmd) {
        case EFFECT_CMD_INIT:
            return replyInt(replySize, pReply, 0) ? 0 : -EINVAL;

        case EFFECT_CMD_SET_CONFIG:
            if (pCmd == nullptr || cmdSize < sizeof(effect_config_t)) return -EINVAL;
            return replyInt(replySize, pReply, setConfig(e, *static_cast<effect_config_t*>(pCmd)))
                       ? 0 : -EINVAL;

        case EFFECT_CMD_GET_CONFIG:
            if (replySize == nullptr || pReply == nullptr || *replySize < sizeof(effect_config_t))
                return -EINVAL;
            memcpy(pReply, &e->config, sizeof(effect_config_t));
            *replySize = sizeof(effect_config_t);
            return 0;

        case EFFECT_CMD_RESET:
            lockBusy(e);
            e->engine.reset();
            unlockBusy(e);
            replyInt(replySize, pReply, 0);
            return 0;

        case EFFECT_CMD_ENABLE:
        case EFFECT_CMD_DISABLE:
            if (!replyInt(replySize, pReply, 0)) return -EINVAL;
            e->enabled.store(cmd == EFFECT_CMD_ENABLE);
            ALOGI("session %d: %s", e->session, cmd == EFFECT_CMD_ENABLE ? "enabled" : "disabled");
            return 0;

        case EFFECT_CMD_SET_PARAM: {
            if (pCmd == nullptr || cmdSize < sizeof(effect_param_t) + sizeof(int32_t)) return -EINVAL;
            const auto* p = static_cast<const effect_param_t*>(pCmd);
            if (sizeof(effect_param_t) + paddedSize(p->psize) + p->vsize > cmdSize) return -EINVAL;
            return replyInt(replySize, pReply, setParam(e, p)) ? 0 : -EINVAL;
        }

        case EFFECT_CMD_GET_PARAM: {
            if (pCmd == nullptr || cmdSize < sizeof(effect_param_t) + sizeof(int32_t) ||
                replySize == nullptr || pReply == nullptr)
                return -EINVAL;
            const auto* p = static_cast<const effect_param_t*>(pCmd);
            const size_t header = sizeof(effect_param_t) + paddedSize(p->psize);
            if (p->psize < sizeof(int32_t) || header > cmdSize || *replySize < header) return -EINVAL;
            auto* r = static_cast<effect_param_t*>(pReply);
            memcpy(r, p, sizeof(effect_param_t) + p->psize);
            r->vsize = 0;
            r->status = getParam(e, r, static_cast<uint32_t>(*replySize - header));
            *replySize = static_cast<uint32_t>(header + r->vsize);
            return 0;
        }

        case EFFECT_CMD_DUMP:
            if (pCmd != nullptr && cmdSize >= sizeof(int)) dump(e, *static_cast<int*>(pCmd));
            return 0;

        case EFFECT_CMD_SET_DEVICE:
        case EFFECT_CMD_SET_VOLUME:
        case EFFECT_CMD_SET_AUDIO_MODE:
        case EFFECT_CMD_SET_INPUT_DEVICE:
        case EFFECT_CMD_SET_AUDIO_SOURCE:
        case EFFECT_CMD_OFFLOAD:
        case EFFECT_CMD_SET_PARAM_DEFERRED:
        case EFFECT_CMD_SET_PARAM_COMMIT:
            replyInt(replySize, pReply, 0);
            return 0;

        default:
            return -EINVAL;
    }
}

int32_t GetDescriptorItf(effect_handle_t self, effect_descriptor_t* desc) {
    if (self == nullptr || desc == nullptr) return -EINVAL;
    *desc = kDescriptor;
    return 0;
}

const effect_interface_s kInterface = {Process, Command, GetDescriptorItf, nullptr};

// --- library entry points ------------------------------------------------------------

bool isOurUuid(const effect_uuid_t* uuid) {
    return uuid != nullptr && memcmp(uuid, &kDescriptor.uuid, sizeof(effect_uuid_t)) == 0;
}

int32_t CreateEffect(const effect_uuid_t* uuid, int32_t session, int32_t io, effect_handle_t* pHandle) {
    if (pHandle == nullptr || !isOurUuid(uuid)) return -EINVAL;
    auto* e = new (std::nothrow) Instance();
    if (e == nullptr) return -ENOMEM;
    e->itfe = &kInterface;
    e->session = session;
    e->io = io;

    Global& g = global();
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        if (!g.loaded) loadFilesLocked(g);
        e->engine.setSettings(g.settings);
        g.instances.push_back(e);
        count = g.instances.size();
    }
    *pHandle = reinterpret_cast<effect_handle_t>(e);
    ALOGI("created for session %d io %d (%zu instances)", session, io, count);
    return 0;
}

int32_t ReleaseEffect(effect_handle_t handle) {
    auto* e = reinterpret_cast<Instance*>(handle);
    if (e == nullptr) return -EINVAL;
    Global& g = global();
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        g.instances.erase(std::remove(g.instances.begin(), g.instances.end(), e), g.instances.end());
        count = g.instances.size();
    }
    ALOGI("released session %d after %llu frames (%zu instances)", e->session,
          static_cast<unsigned long long>(e->frames.load()), count);
    delete e;
    return 0;
}

int32_t GetDescriptor(const effect_uuid_t* uuid, effect_descriptor_t* desc) {
    if (desc == nullptr || !isOurUuid(uuid)) return -EINVAL;
    *desc = kDescriptor;
    return 0;
}

}  // namespace

extern "C" {
__attribute__((visibility("default"))) audio_effect_library_t AELI = {
    AUDIO_EFFECT_LIBRARY_TAG,
    EFFECT_LIBRARY_API_VERSION,
    "Galaxy Downmixer",
    "Galaxy Downmixer project",
    CreateEffect,
    ReleaseEffect,
    GetDescriptor,
};
}
