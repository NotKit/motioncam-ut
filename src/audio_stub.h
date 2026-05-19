#pragma once
#include <motioncam/AudioInterface.h>
#include <vector>
#include <cstdint>

// No-op AudioInterface — replaces the Oboe-based AudioRecorder from the Android build.
// Audio recording can be added later via Qt Multimedia or PulseAudio.
namespace motioncam {
class AudioStub : public AudioInterface {
public:
    bool start(const int /*sampleRateHz*/, const int /*channels*/) override { return true; }
    void stop() override {}
    const std::vector<int16_t>& getAudioData(uint32_t& outNumFrames) const override {
        outNumFrames = 0;
        return empty_;
    }
    int getSampleRate() const override { return 44100; }
    int getChannels()   const override { return 1; }
private:
    std::vector<int16_t> empty_;
};
} // namespace motioncam
