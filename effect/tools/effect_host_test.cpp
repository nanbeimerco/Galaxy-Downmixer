// Loads libgalaxydownmixer.so the way the effects factory does and drives it
// through the legacy effect API. Runs as an ordinary shell binary from
// /data/local/tmp, so the library can be checked on the phone before it is
// installed into the audio HAL.
//
//   effect_host_test /data/local/tmp/libgalaxydownmixer.so

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../src/Protocol.h"
#include "../src/aosp_effect.h"

namespace {

int g_failures = 0;
#define EXPECT(cond)                                                     \
    do {                                                                 \
        if (!(cond)) {                                                   \
            ++g_failures;                                                \
            printf("FAIL line %d: %s\n", __LINE__, #cond);               \
        }                                                                \
    } while (0)

constexpr uint32_t kMask714 = 0x2D63F;
constexpr int kCh = 12;
constexpr int kDelay = 172;

int32_t cmdInt(effect_handle_t h, uint32_t code, uint32_t size, void* data) {
    int32_t reply = -1;
    uint32_t replySize = sizeof(reply);
    const int32_t ret = (*h)->command(h, code, size, data, &replySize, &reply);
    return ret != 0 ? ret : reply;
}

effect_config_t makeConfig(uint8_t format, uint8_t access) {
    effect_config_t c{};
    for (buffer_config_t* b : {&c.inputCfg, &c.outputCfg}) {
        b->samplingRate = 48000;
        b->channels = kMask714;
        b->format = format;
        b->accessMode = EFFECT_BUFFER_ACCESS_READ;
        b->mask = EFFECT_CONFIG_ALL;
    }
    c.outputCfg.accessMode = access;
    return c;
}

template <typename T>
int32_t setParam(effect_handle_t h, int32_t key, T value) {
    std::vector<char> buf(sizeof(effect_param_t) + 4 + sizeof(T));
    auto* p = reinterpret_cast<effect_param_t*>(buf.data());
    p->psize = 4;
    p->vsize = sizeof(T);
    memcpy(p->data, &key, 4);
    memcpy(p->data + 4, &value, sizeof(T));
    return cmdInt(h, EFFECT_CMD_SET_PARAM, static_cast<uint32_t>(buf.size()), buf.data());
}

template <typename T>
bool getParam(effect_handle_t h, int32_t key, T* out) {
    std::vector<char> cmd(sizeof(effect_param_t) + 4);
    auto* p = reinterpret_cast<effect_param_t*>(cmd.data());
    p->psize = 4;
    p->vsize = sizeof(T);
    memcpy(p->data, &key, 4);
    std::vector<char> reply(sizeof(effect_param_t) + 4 + sizeof(T));
    uint32_t replySize = static_cast<uint32_t>(reply.size());
    if ((*h)->command(h, EFFECT_CMD_GET_PARAM, static_cast<uint32_t>(cmd.size()), cmd.data(),
                      &replySize, reply.data()) != 0)
        return false;
    auto* r = reinterpret_cast<effect_param_t*>(reply.data());
    if (r->status != 0 || r->vsize != sizeof(T)) return false;
    memcpy(out, r->data + 4, sizeof(T));
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: %s /path/to/libgalaxydownmixer.so\n", argv[0]);
        return 2;
    }
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (lib == nullptr) {
        printf("dlopen failed: %s\n", dlerror());
        return 1;
    }
    auto* info = static_cast<audio_effect_library_t*>(dlsym(lib, "AELI"));
    EXPECT(info != nullptr);
    if (info == nullptr) return 1;
    EXPECT(info->tag == AUDIO_EFFECT_LIBRARY_TAG);
    EXPECT(info->version == EFFECT_LIBRARY_API_VERSION);
    printf("library: %s / %s\n", info->name, info->implementor);

    const effect_uuid_t uuid = {0xa6e3f002, 0x29bf, 0x40a2, 0xbfcf, {0xdf, 0x47, 0xef, 0xb3, 0xa2, 0x24}};
    effect_descriptor_t desc{};
    EXPECT(info->get_descriptor(&uuid, &desc) == 0);
    printf("descriptor: %s, flags 0x%X\n", desc.name, desc.flags);

    effect_handle_t h = nullptr;
    EXPECT(info->create_effect(&uuid, 4242, 21, &h) == 0 && h != nullptr);
    if (h == nullptr) return 1;
    EXPECT(cmdInt(h, EFFECT_CMD_INIT, 0, nullptr) == 0);

    // Mismatched in/out channels must be rejected (AudioFlinger then falls back).
    effect_config_t bad = makeConfig(GDM_AUDIO_FORMAT_PCM_FLOAT, EFFECT_BUFFER_ACCESS_WRITE);
    bad.outputCfg.channels = 0x3;
    EXPECT(cmdInt(h, EFFECT_CMD_SET_CONFIG, sizeof(bad), &bad) != 0);

    effect_config_t cfg = makeConfig(GDM_AUDIO_FORMAT_PCM_FLOAT, EFFECT_BUFFER_ACCESS_WRITE);
    EXPECT(cmdInt(h, EFFECT_CMD_SET_CONFIG, sizeof(cfg), &cfg) == 0);
    EXPECT(cmdInt(h, EFFECT_CMD_ENABLE, 0, nullptr) == 0);

    // Float, in place: one impulse per channel, 400 frames apart.
    const size_t frames = 400 * kCh;
    std::vector<float> buf(frames * kCh, 0.0f);
    for (int ch = 0; ch < kCh; ++ch)
        if (ch != 3) buf[(ch * 400) * kCh + ch] = 0.5f;  // LFE would ring into later checks
    audio_buffer_t io{};
    io.frameCount = frames;
    io.f32 = buf.data();
    EXPECT((*h)->process(h, &io, &io) == 0);

    const float expectL[kCh] = {1, 0, 0.70710678f, 0, 1, 0, 1, 0, 1, 0, 1, 0};
    const float expectR[kCh] = {0, 1, 0.70710678f, 0, 0, 1, 0, 1, 0, 1, 0, 1};
    for (int ch = 0; ch < kCh; ++ch) {
        if (ch == 3) continue;  // LFE goes through the low-pass
        const size_t f = ch * 400 + kDelay;
        EXPECT(fabsf(buf[f * kCh + 0] - 0.5f * expectL[ch]) < 1e-6f);
        EXPECT(fabsf(buf[f * kCh + 1] - 0.5f * expectR[ch]) < 1e-6f);
    }
    bool othersZero = true;
    for (size_t f = 0; f < frames; ++f)
        for (int ch = 2; ch < kCh; ++ch) othersZero = othersZero && buf[f * kCh + ch] == 0.0f;
    EXPECT(othersZero);

    gdm::protocol::Status st{};
    EXPECT(getParam(h, gdm::protocol::kParamStatus, &st));
    EXPECT(st.channelMask == kMask714 && st.sampleRate == 48000 && st.framesProcessed == frames);
    printf("status: %u Hz, mask 0x%X, %llu frames, session %d, %d instance(s)\n", st.sampleRate,
           st.channelMask, static_cast<unsigned long long>(st.framesProcessed), st.sessionId,
           st.instanceCount);

    gdm::protocol::Meters m{};
    EXPECT(getParam(h, gdm::protocol::kParamMeters, &m));
    EXPECT(m.numChannels == kCh);
    EXPECT(fabsf(m.inPeak[5] - 0.5f) < 1e-6f);
    printf("meters: in BR peak %.3f, out L peak %.3f, frames %u\n", m.inPeak[5], m.outPeak[0], m.frames);

    // Mute FL via SET_PARAM, then 16-bit + accumulate path.
    EXPECT(setParam<uint32_t>(h, gdm::protocol::kParamMuteMask, 0x1) == 0);
    uint32_t mute = 0;
    EXPECT(getParam(h, gdm::protocol::kParamMuteMask, &mute) && mute == 0x1);
    EXPECT(setParam<uint32_t>(h, gdm::protocol::kParamMuteMask, 0) == 0);

    cfg = makeConfig(GDM_AUDIO_FORMAT_PCM_16_BIT, EFFECT_BUFFER_ACCESS_ACCUMULATE);
    EXPECT(cmdInt(h, EFFECT_CMD_SET_CONFIG, sizeof(cfg), &cfg) == 0);
    std::vector<int16_t> in16(1000 * kCh, 0), out16(1000 * kCh, 0);
    in16[0] = 16384;            // FL impulse, 0.5
    out16[kDelay * kCh] = 100;  // existing content to accumulate onto
    audio_buffer_t bi{}, bo{};
    bi.frameCount = bo.frameCount = 1000;
    bi.s16 = in16.data();
    bo.s16 = out16.data();
    EXPECT((*h)->process(h, &bi, &bo) == 0);
    EXPECT(out16[kDelay * kCh] == 16384 + 100);

    // EQ upload in chunks (as the app does), then summary and warnings.
    {
        std::string text =
            "Preamp: -1.5 dB\n"
            "Filter 1: ON LSC Fc 23 Hz Gain 1.37 dB Q 0.758\n"
            "Filter 2: ON PK Fc 141 Hz Gain -6.31 dB Q 0.297\n"
            "Channel: L\nPreamp: -1.7 dB\nFilter 1: ON PK Fc 8978 Hz Gain -1.74 dB Q 3.18\n"
            "Channel: R\nFilter 1: ON PK Fc 8978 Hz Gain 1.74 dB Q 3.18\n"
            "GraphicEQ: 20 0\n";
        while (text.size() < 1500) text += "# padding to force several chunks\n";
        EXPECT(setParam<uint32_t>(h, gdm::protocol::kParamEqTextBegin, static_cast<uint32_t>(text.size())) == 0);
        for (size_t pos = 0; pos < text.size(); pos += gdm::protocol::kEqChunkMax) {
            const size_t len = std::min<size_t>(gdm::protocol::kEqChunkMax, text.size() - pos);
            std::vector<char> buf(sizeof(effect_param_t) + 4 + len);
            auto* p = reinterpret_cast<effect_param_t*>(buf.data());
            p->psize = 4;
            p->vsize = static_cast<uint32_t>(len);
            const int32_t key = gdm::protocol::kParamEqTextChunk;
            memcpy(p->data, &key, 4);
            memcpy(p->data + 4, text.data() + pos, len);
            EXPECT(cmdInt(h, EFFECT_CMD_SET_PARAM, static_cast<uint32_t>(buf.size()), buf.data()) == 0);
        }
        EXPECT(setParam<int32_t>(h, gdm::protocol::kParamEqTextCommit, 0) == 0);
        gdm::protocol::EqSummary eq{};
        EXPECT(getParam(h, gdm::protocol::kParamEqSummary, &eq));
        EXPECT(eq.commonFilters == 2 && eq.leftFilters == 1 && eq.rightFilters == 1);
        EXPECT(eq.warningCount == 1 && eq.textBytes == text.size());
        EXPECT(fabsf(eq.leftPreampDb + 1.7f) < 1e-5f);
        printf("eq: common %d, L %d, R %d filters, %d warning(s), %u bytes\n", eq.commonFilters,
               eq.leftFilters, eq.rightFilters, eq.warningCount, eq.textBytes);
        EXPECT(setParam<int32_t>(h, gdm::protocol::kParamEqClear, 0) == 0);
        EXPECT(getParam(h, gdm::protocol::kParamEqSummary, &eq) && eq.commonFilters == 0);
    }

    // DUMP writes a line to the given fd (stdout here).
    int fd = 1;
    fflush(stdout);
    (*h)->command(h, EFFECT_CMD_DUMP, sizeof(fd), &fd, nullptr, nullptr);

    EXPECT(cmdInt(h, EFFECT_CMD_DISABLE, 0, nullptr) == 0);
    EXPECT((*h)->process(h, &bi, &bo) == -ENODATA);
    EXPECT(info->release_effect(h) == 0);

    printf("%s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
