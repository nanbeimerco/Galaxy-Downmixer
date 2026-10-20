#include "gdm/AudioWriter.h"

#include <chrono>
#include <cstring>

namespace gdm {

namespace {

#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t riffSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};

    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 18;
    uint16_t formatTag = 3;  // WAVE_FORMAT_IEEE_FLOAT
    uint16_t channels = 2;
    uint32_t sampleRate = 48000;
    uint32_t bytesPerSec = 48000 * 2 * 4;
    uint16_t blockAlign = 8;
    uint16_t bitsPerSample = 32;
    uint16_t cbSize = 0;

    char fact[4] = {'f', 'a', 'c', 't'};
    uint32_t factSize = 4;
    uint32_t sampleLength = 0;

    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};
#pragma pack(pop)

static_assert(sizeof(WavHeader) == 58, "WavHeader size");

}  // namespace

AudioWriter::AudioWriter(size_t ringBufferCapacity)
    : ringBuffer_(ringBufferCapacity) {}

AudioWriter::~AudioWriter() {
    stop();
}

void AudioWriter::writeWavHeader(FILE* fp, uint32_t sampleRate, uint32_t dataBytes) {
    WavHeader hdr;
    hdr.sampleRate = sampleRate;
    hdr.bytesPerSec = sampleRate * 2 * 4;
    hdr.dataSize = dataBytes;
    hdr.sampleLength = dataBytes / 4;  // total float samples
    hdr.riffSize = sizeof(WavHeader) - 8 + dataBytes;

    std::fseek(fp, 0, SEEK_SET);
    std::fwrite(&hdr, sizeof(hdr), 1, fp);
}

bool AudioWriter::start(const std::string& filePath, uint32_t sampleRate) {
    stop();

    filePath_ = filePath;
    sampleRate_ = sampleRate;
    recordedFrames_.store(0, std::memory_order_relaxed);
    droppedFrames_.store(0, std::memory_order_relaxed);
    ringBuffer_.reset();

    file_ = std::fopen(filePath.c_str(), "wb");
    if (!file_) {
        return false;
    }

    // Write placeholder header
    writeWavHeader(file_, sampleRate_, 0);
    std::fflush(file_);

    running_.store(true, std::memory_order_release);
    recording_.store(true, std::memory_order_release);

    workerThread_ = std::thread(&AudioWriter::threadLoop, this);
    return true;
}

void AudioWriter::stop() {
    if (!recording_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    running_.store(false, std::memory_order_release);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    if (file_) {
        const uint64_t frames = recordedFrames_.load(std::memory_order_relaxed);
        const uint32_t dataBytes = static_cast<uint32_t>(frames * 8);
        writeWavHeader(file_, sampleRate_, dataBytes);
        std::fclose(file_);
        file_ = nullptr;
    }
}

void AudioWriter::push(float l, float r) {
    if (!recording_.load(std::memory_order_relaxed)) return;
    if (!ringBuffer_.push(l, r)) {
        droppedFrames_.fetch_add(1, std::memory_order_relaxed);
    }
}

void AudioWriter::threadLoop() {
    constexpr size_t kChunkSize = 1024;
    std::vector<StereoSample> chunk(kChunkSize);

    while (running_.load(std::memory_order_relaxed) || ringBuffer_.availableRead() > 0) {
        size_t n = ringBuffer_.pop(chunk.data(), kChunkSize);
        if (n > 0) {
            if (file_) {
                std::fwrite(chunk.data(), sizeof(StereoSample), n, file_);
            }
            recordedFrames_.fetch_add(n, std::memory_order_relaxed);
        } else {
            // Buffer empty, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

}  // namespace gdm
