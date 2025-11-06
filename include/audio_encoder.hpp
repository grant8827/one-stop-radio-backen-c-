#pragma once
#include <string>
#include <memory>

enum class CodecType {
    MP3,
    OGG_VORBIS,
    AAC,
    OPUS
};

struct AudioFormat {
    CodecType codec;
    int sample_rate;
    int channels;
    int bitrate;
};

class AudioEncoder {
public:
    AudioEncoder();
    ~AudioEncoder();
    
    bool initialize(const AudioFormat& format);
    bool encode_frame(const float* samples, size_t sample_count, std::vector<uint8_t>& output);
    void flush(std::vector<uint8_t>& output);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};