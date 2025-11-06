#include "audio_encoder.hpp"
#include "logger.hpp"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

class AudioEncoder::Impl {
public:
    Impl() : codec_context_(nullptr), frame_(nullptr), packet_(nullptr), 
             swr_context_(nullptr), initialized_(false) {}
    
    ~Impl() {
        cleanup();
    }
    
    bool initialize(const AudioFormat& input_format, const AudioFormat& output_format) {
        input_format_ = input_format;
        output_format_ = output_format;
        
        // Find MP3 encoder
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
        if (!codec) {
            Logger::error("MP3 encoder not found");
            return false;
        }
        
        // Allocate codec context
        codec_context_ = avcodec_alloc_context3(codec);
        if (!codec_context_) {
            Logger::error("Failed to allocate codec context");
            return false;
        }
        
        // Set codec parameters
        codec_context_->bit_rate = output_format.bitrate;
        codec_context_->sample_rate = output_format.sample_rate;
        codec_context_->channels = output_format.channels;
        codec_context_->channel_layout = (output_format.channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
        codec_context_->sample_fmt = AV_SAMPLE_FMT_S16P; // MP3 encoder expects planar 16-bit
        
        // Open codec
        if (avcodec_open2(codec_context_, codec, nullptr) < 0) {
            Logger::error("Failed to open codec");
            cleanup();
            return false;
        }
        
        // Allocate frame
        frame_ = av_frame_alloc();
        if (!frame_) {
            Logger::error("Failed to allocate frame");
            cleanup();
            return false;
        }
        
        frame_->nb_samples = codec_context_->frame_size;
        frame_->format = codec_context_->sample_fmt;
        frame_->channels = codec_context_->channels;
        frame_->channel_layout = codec_context_->channel_layout;
        
        if (av_frame_get_buffer(frame_, 0) < 0) {
            Logger::error("Failed to allocate frame buffer");
            cleanup();
            return false;
        }
        
        // Allocate packet
        packet_ = av_packet_alloc();
        if (!packet_) {
            Logger::error("Failed to allocate packet");
            cleanup();
            return false;
        }
        
        // Initialize resampler if formats differ
        if (input_format.sample_rate != output_format.sample_rate ||
            input_format.channels != output_format.channels) {
            
            swr_context_ = swr_alloc();
            if (!swr_context_) {
                Logger::error("Failed to allocate resampler");
                cleanup();
                return false;
            }
            
            // Set resampler options
            av_opt_set_int(swr_context_, "in_channel_count", input_format.channels, 0);
            av_opt_set_int(swr_context_, "in_sample_rate", input_format.sample_rate, 0);
            av_opt_set_sample_fmt(swr_context_, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
            
            av_opt_set_int(swr_context_, "out_channel_count", output_format.channels, 0);
            av_opt_set_int(swr_context_, "out_sample_rate", output_format.sample_rate, 0);
            av_opt_set_sample_fmt(swr_context_, "out_sample_fmt", AV_SAMPLE_FMT_S16P, 0);
            
            if (swr_init(swr_context_) < 0) {
                Logger::error("Failed to initialize resampler");
                cleanup();
                return false;
            }
        }
        
        initialized_ = true;
        Logger::info("Audio encoder initialized");
        return true;
    }
    
    std::vector<uint8_t> encode(const int16_t* input_samples, int num_samples) {
        std::vector<uint8_t> encoded_data;
        
        if (!initialized_) {
            Logger::error("Encoder not initialized");
            return encoded_data;
        }
        
        // Prepare input data
        const uint8_t* input_data[1] = { reinterpret_cast<const uint8_t*>(input_samples) };
        
        if (swr_context_) {
            // Resample if needed
            int output_samples = swr_convert(swr_context_,
                                           frame_->data, frame_->nb_samples,
                                           input_data, num_samples);
            
            if (output_samples < 0) {
                Logger::error("Resampling failed");
                return encoded_data;
            }
            
            frame_->nb_samples = output_samples;
        } else {
            // Direct copy (convert interleaved to planar)
            int16_t* left_channel = reinterpret_cast<int16_t*>(frame_->data[0]);
            int16_t* right_channel = (output_format_.channels == 2) ? 
                reinterpret_cast<int16_t*>(frame_->data[1]) : nullptr;
            
            for (int i = 0; i < num_samples; ++i) {
                if (input_format_.channels == 1) {
                    // Mono input
                    left_channel[i] = input_samples[i];
                    if (right_channel) {
                        right_channel[i] = input_samples[i]; // Duplicate for stereo output
                    }
                } else {
                    // Stereo input
                    left_channel[i] = input_samples[i * 2];
                    if (right_channel) {
                        right_channel[i] = input_samples[i * 2 + 1];
                    }
                }
            }
            
            frame_->nb_samples = num_samples;
        }
        
        // Encode frame
        int ret = avcodec_send_frame(codec_context_, frame_);
        if (ret < 0) {
            Logger::error("Error sending frame to encoder");
            return encoded_data;
        }
        
        // Receive encoded packets
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_context_, packet_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                Logger::error("Error receiving packet from encoder");
                break;
            }
            
            // Copy encoded data
            encoded_data.insert(encoded_data.end(), 
                              packet_->data, 
                              packet_->data + packet_->size);
            
            av_packet_unref(packet_);
        }
        
        return encoded_data;
    }
    
    void cleanup() {
        if (packet_) {
            av_packet_free(&packet_);
        }
        
        if (frame_) {
            av_frame_free(&frame_);
        }
        
        if (codec_context_) {
            avcodec_free_context(&codec_context_);
        }
        
        if (swr_context_) {
            swr_free(&swr_context_);
        }
        
        initialized_ = false;
    }

private:
    AVCodecContext* codec_context_;
    AVFrame* frame_;
    AVPacket* packet_;
    SwrContext* swr_context_;
    AudioFormat input_format_;
    AudioFormat output_format_;
    bool initialized_;
};

AudioEncoder::AudioEncoder() : impl_(std::make_unique<Impl>()) {}

AudioEncoder::~AudioEncoder() = default;

bool AudioEncoder::initialize(const AudioFormat& input_format, const AudioFormat& output_format) {
    return impl_->initialize(input_format, output_format);
}

std::vector<uint8_t> AudioEncoder::encode(const int16_t* input_samples, int num_samples) {
    return impl_->encode(input_samples, num_samples);
}

void AudioEncoder::reset() {
    impl_->cleanup();
}