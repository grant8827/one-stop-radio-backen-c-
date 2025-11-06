#include "audio_stream_encoder.hpp"
#include "utils/logger.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

extern "C" {
#include <ogg/ogg.h>
// Note: The following audio codec libraries need to be installed:
// #include <lame/lame.h>          // LAME MP3 encoder
// #include <vorbis/vorbisenc.h>   // Vorbis OGG encoder  
// #include <opus/opus.h>          // Opus audio encoder
}

/**
 * Internal implementation details
 */
struct AudioStreamEncoder::Impl {
    // libshout connection
    shout_t* shout = nullptr;
    
    // Audio encoders (disabled until libraries are installed)
    // lame_global_flags* lame = nullptr;      // MP3
    // vorbis_info vorbis_info_obj;            // OGG Vorbis
    // vorbis_comment vorbis_comment_obj;
    // vorbis_dsp_state vorbis_dsp;
    // vorbis_block vorbis_block_obj;
    // OpusEncoder* opus_encoder = nullptr;    // OGG Opus
    
    // OGG container support
    ogg_stream_state ogg_stream_obj;
    uint64_t opus_granule_pos = 0;
    uint64_t opus_packet_count = 0;
    uint64_t bytes_sent = 0;
    
    // FFmpeg for AAC/advanced encoding
    AVCodecContext* codec_context = nullptr;
    SwrContext* resampler = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    
    // Audio buffers
    std::vector<uint8_t> encoded_buffer;
    std::vector<float> resample_buffer;
    std::vector<int16_t> pcm_buffer;
    
    // Statistics
    StreamStats stats;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point connect_time;
    
    // Audio processing
    float gain = 1.0f;
    bool limiter_enabled = false;
    float limiter_threshold = -1.0f;
    bool noise_gate_enabled = false;
    float noise_gate_threshold = -40.0f;
    
    Impl() {
        // Initialize libshout
        shout_init();
        
        // Initialize statistics
        stats.status = StreamStatus::DISCONNECTED;
        start_time = std::chrono::steady_clock::now();
    }
    
    ~Impl() {
        cleanup();
        shout_shutdown();
    }
    
    void cleanup() {
        // Cleanup encoders (disabled until libraries are installed)
        // if (lame) {
        //     lame_close(lame);
        //     lame = nullptr;
        // }
        
        // if (opus_encoder) {
        //     opus_encoder_destroy(opus_encoder);
        //     opus_encoder = nullptr;
        // }
        
        // Cleanup Vorbis (more complex)
        // vorbis_block_clear(&vorbis_block_obj);
        // vorbis_dsp_clear(&vorbis_dsp);
        // vorbis_comment_clear(&vorbis_comment_obj);
        // vorbis_info_clear(&vorbis_info_obj);
        
        // Cleanup OGG stream
        ogg_stream_clear(&ogg_stream_obj);
        
        // Cleanup FFmpeg
        if (resampler) {
            swr_free(&resampler);
        }
        if (codec_context) {
            avcodec_free_context(&codec_context);
        }
        if (frame) {
            av_frame_free(&frame);
        }
        if (packet) {
            av_packet_free(&packet);
        }
        
        // Cleanup libshout
        if (shout) {
            if (shout_get_connected(shout) == SHOUTERR_CONNECTED) {
                shout_close(shout);
            }
            shout_free(shout);
            shout = nullptr;
        }
    }
};

AudioStreamEncoder::AudioStreamEncoder() : impl_(std::make_unique<Impl>()) {
    Logger::info("AudioStreamEncoder created");
}

AudioStreamEncoder::~AudioStreamEncoder() {
    stop_streaming();
    disconnect();
    Logger::info("AudioStreamEncoder destroyed");
}

bool AudioStreamEncoder::configure(const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (!config.is_valid()) {
        Logger::error("Invalid stream configuration");
        return false;
    }
    
    if (is_streaming()) {
        Logger::error("Cannot reconfigure while streaming");
        return false;
    }
    
    config_ = config;
    Logger::info("Stream configured: " + protocol_to_string(config.protocol) + 
                " -> " + config.server_host + ":" + std::to_string(config.server_port));
    
    return true;
}

bool AudioStreamEncoder::connect() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    if (is_connected()) {
        Logger::warning("Already connected to stream server");
        return true;
    }
    
    status_ = StreamStatus::CONNECTING;
    status_message_ = "Connecting to server...";
    
    // Setup libshout connection
    if (!setup_connection()) {
        status_ = StreamStatus::ERROR;
        return false;
    }
    
    // Setup audio encoder
    if (!setup_encoder()) {
        status_ = StreamStatus::ERROR;
        return false;
    }
    
    // Connect to server
    int result = shout_open(impl_->shout);
    if (result != SHOUTERR_SUCCESS) {
        status_ = StreamStatus::ERROR;
        status_message_ = "Connection failed: " + std::string(shout_get_error(impl_->shout));
        Logger::error("Failed to connect to stream server: " + status_message_);
        return false;
    }
    
    status_ = StreamStatus::CONNECTED;
    status_message_ = "Connected to server";
    impl_->connect_time = std::chrono::steady_clock::now();
    impl_->stats.status = StreamStatus::CONNECTED;
    
    Logger::info("Successfully connected to stream server");
    return true;
}

bool AudioStreamEncoder::disconnect() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    if (is_streaming()) {
        stop_streaming();
    }
    
    if (impl_->shout && shout_get_connected(impl_->shout) == SHOUTERR_CONNECTED) {
        shout_close(impl_->shout);
    }
    
    status_ = StreamStatus::DISCONNECTED;
    status_message_ = "Disconnected";
    impl_->stats.status = StreamStatus::DISCONNECTED;
    
    Logger::info("Disconnected from stream server");
    return true;
}

bool AudioStreamEncoder::start_streaming(AudioStreamCallback* callback) {
    if (!is_connected()) {
        Logger::error("Must be connected before starting stream");
        return false;
    }
    
    if (is_streaming()) {
        Logger::warning("Already streaming");
        return true;
    }
    
    audio_callback_ = callback;
    should_stop_ = false;
    
    // Start streaming thread
    streaming_thread_ = std::thread(&AudioStreamEncoder::streaming_worker, this);
    
    status_ = StreamStatus::STREAMING;
    status_message_ = "Streaming active";
    impl_->stats.status = StreamStatus::STREAMING;
    
    Logger::info("Audio streaming started");
    return true;
}

bool AudioStreamEncoder::stop_streaming() {
    if (!is_streaming()) {
        return true;
    }
    
    // Signal thread to stop
    should_stop_ = true;
    
    // Wait for thread to finish
    if (streaming_thread_.joinable()) {
        streaming_thread_.join();
    }
    
    status_ = StreamStatus::CONNECTED;
    status_message_ = "Streaming stopped";
    audio_callback_ = nullptr;
    
    Logger::info("Audio streaming stopped");
    return true;
}

bool AudioStreamEncoder::send_audio_data(const float* samples, size_t frames) {
    if (!is_streaming()) {
        return false;
    }
    
    return encode_and_send(samples, frames);
}

bool AudioStreamEncoder::update_metadata(const std::string& title, const std::string& artist) {
    if (!impl_->shout) {
        return false;
    }
    
    std::string metadata;
    if (!artist.empty()) {
        metadata = artist + " - " + title;
    } else {
        metadata = title;
    }
    
    shout_metadata_t* shout_meta = shout_metadata_new();
    shout_metadata_add(shout_meta, "song", metadata.c_str());
    
    int result = shout_set_metadata(impl_->shout, shout_meta);
    shout_metadata_free(shout_meta);
    
    if (result != SHOUTERR_SUCCESS) {
        Logger::warning("Failed to update metadata: " + std::string(shout_get_error(impl_->shout)));
        return false;
    }
    
    Logger::info("Updated stream metadata: " + metadata);
    return true;
}

StreamStats AudioStreamEncoder::get_statistics() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    StreamStats stats = impl_->stats;
    stats.status = status_;
    stats.status_message = status_message_;
    
    // Calculate connection time
    if (status_ == StreamStatus::CONNECTED || status_ == StreamStatus::STREAMING) {
        auto now = std::chrono::steady_clock::now();
        stats.connected_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - impl_->connect_time).count();
    }
    
    return stats;
}

bool AudioStreamEncoder::setup_connection() {
    impl_->shout = shout_new();
    if (!impl_->shout) {
        status_message_ = "Failed to create libshout object";
        return false;
    }
    
    // Set protocol-specific parameters
    switch (config_.protocol) {
        case StreamProtocol::ICECAST2:
            shout_set_protocol(impl_->shout, SHOUT_PROTOCOL_HTTP);
            shout_set_format(impl_->shout, SHOUT_FORMAT_MP3); // Will be set based on codec
            break;
            
        case StreamProtocol::SHOUTCAST:
            shout_set_protocol(impl_->shout, SHOUT_PROTOCOL_ICY);
            shout_set_format(impl_->shout, SHOUT_FORMAT_MP3);
            break;
            
        default:
            status_message_ = "Unsupported streaming protocol";
            return false;
    }
    
    // Set codec format
    switch (config_.codec) {
        case StreamCodec::MP3:
            shout_set_format(impl_->shout, SHOUT_FORMAT_MP3);
            break;
        case StreamCodec::OGG_VORBIS:
            shout_set_format(impl_->shout, SHOUT_FORMAT_OGG);
            break;
        case StreamCodec::OGG_OPUS:
            shout_set_format(impl_->shout, SHOUT_FORMAT_OGG);
            break;
        default:
            status_message_ = "Unsupported audio codec for this protocol";
            return false;
    }
    
    // Set connection parameters
    shout_set_host(impl_->shout, config_.server_host.c_str());
    shout_set_port(impl_->shout, config_.server_port);
    shout_set_password(impl_->shout, config_.password.c_str());
    shout_set_mount(impl_->shout, config_.mount_point.c_str());
    
    if (!config_.username.empty()) {
        shout_set_user(impl_->shout, config_.username.c_str());
    }
    
    // Set stream metadata
    shout_set_name(impl_->shout, config_.stream_name.c_str());
    shout_set_description(impl_->shout, config_.stream_description.c_str());
    shout_set_genre(impl_->shout, config_.stream_genre.c_str());
    shout_set_url(impl_->shout, config_.stream_url.c_str());
    
    // Set audio parameters
    shout_set_audio_info(impl_->shout, SHOUT_AI_BITRATE, std::to_string(config_.bitrate).c_str());
    shout_set_audio_info(impl_->shout, SHOUT_AI_SAMPLERATE, std::to_string(config_.sample_rate).c_str());
    shout_set_audio_info(impl_->shout, SHOUT_AI_CHANNELS, std::to_string(config_.channels).c_str());
    
    // Set additional options
    shout_set_public(impl_->shout, config_.public_stream ? 1 : 0);
    shout_set_agent(impl_->shout, config_.user_agent.c_str());
    
    return true;
}

bool AudioStreamEncoder::setup_encoder() {
    switch (config_.codec) {
        case StreamCodec::MP3:
            return setup_mp3_encoder();
        case StreamCodec::OGG_VORBIS:
            return setup_vorbis_encoder();
        case StreamCodec::OGG_OPUS:
            return setup_opus_encoder();
        case StreamCodec::AAC:
            return setup_aac_encoder();
        default:
            status_message_ = "Unsupported codec";
            return false;
    }
}

bool AudioStreamEncoder::setup_mp3_encoder() {
    status_message_ = "MP3 encoder not available - LAME library not installed";
    Logger::error("MP3 encoding requires LAME library installation");
    return false;
}

bool AudioStreamEncoder::setup_vorbis_encoder() {
    status_message_ = "Vorbis encoder not available - libvorbis library not installed";
    Logger::error("Vorbis encoding requires libvorbis library installation");
    return false;
}

bool AudioStreamEncoder::setup_opus_encoder() {
    status_message_ = "Opus encoder not available - libopus library not installed";
    Logger::error("Opus encoding requires libopus library installation");
    return false;
}

bool AudioStreamEncoder::setup_aac_encoder() {
    // Find AAC encoder
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        status_message_ = "AAC encoder not found";
        return false;
    }
    
    // Create codec context
    impl_->codec_context = avcodec_alloc_context3(codec);
    if (!impl_->codec_context) {
        status_message_ = "Failed to allocate AAC codec context";
        return false;
    }
    
    // Configure codec
    impl_->codec_context->bit_rate = config_.bitrate * 1000;
    impl_->codec_context->sample_rate = config_.sample_rate;
    impl_->codec_context->channels = config_.channels;
    impl_->codec_context->channel_layout = config_.channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    impl_->codec_context->sample_fmt = AV_SAMPLE_FMT_FLTP;
    impl_->codec_context->profile = FF_PROFILE_AAC_LOW;
    
    // Open codec
    if (avcodec_open2(impl_->codec_context, codec, nullptr) < 0) {
        status_message_ = "Failed to open AAC codec";
        return false;
    }
    
    // Allocate frame and packet
    impl_->frame = av_frame_alloc();
    impl_->packet = av_packet_alloc();
    
    if (!impl_->frame || !impl_->packet) {
        status_message_ = "Failed to allocate AAC frame/packet";
        return false;
    }
    
    Logger::info("AAC encoder initialized: " + std::to_string(config_.bitrate) + "kbps");
    return true;
}

void AudioStreamEncoder::streaming_worker() {
    Logger::info("Streaming worker thread started");
    
    const size_t buffer_size = 1152; // Standard frame size
    std::vector<float> audio_buffer(buffer_size * config_.channels);
    
    while (!should_stop_) {
        if (!audio_callback_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // Get audio data from callback
        size_t frames_provided = audio_callback_->on_audio_data(
            audio_buffer.data(), buffer_size, config_.channels);
        
        if (frames_provided > 0) {
            // Apply audio processing
            apply_audio_processing(audio_buffer.data(), frames_provided);
            
            // Encode and send
            if (!encode_and_send(audio_buffer.data(), frames_provided)) {
                Logger::error("Failed to encode/send audio data");
                handle_connection_error("Encoding/transmission error");
                break;
            }
            
            // Update statistics
            update_statistics();
        } else {
            // No audio data available, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    Logger::info("Streaming worker thread stopped");
}

void AudioStreamEncoder::apply_audio_processing(float* samples, size_t frames) {
    const size_t total_samples = frames * config_.channels;
    
    // Apply gain
    if (impl_->gain != 1.0f) {
        for (size_t i = 0; i < total_samples; ++i) {
            samples[i] *= impl_->gain;
        }
    }
    
    // Apply noise gate
    if (impl_->noise_gate_enabled) {
        const float threshold = std::pow(10.0f, impl_->noise_gate_threshold / 20.0f);
        for (size_t i = 0; i < total_samples; i += config_.channels) {
            float level = 0.0f;
            for (int ch = 0; ch < config_.channels; ++ch) {
                level += std::abs(samples[i + ch]);
            }
            level /= config_.channels;
            
            if (level < threshold) {
                for (int ch = 0; ch < config_.channels; ++ch) {
                    samples[i + ch] = 0.0f;
                }
            }
        }
    }
    
    // Apply limiter
    if (impl_->limiter_enabled) {
        const float threshold = std::pow(10.0f, impl_->limiter_threshold / 20.0f);
        for (size_t i = 0; i < total_samples; ++i) {
            if (std::abs(samples[i]) > threshold) {
                samples[i] = samples[i] > 0 ? threshold : -threshold;
            }
        }
    }
    
    // Update level statistics
    float peak_left = 0.0f, peak_right = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        peak_left = std::max(peak_left, std::abs(samples[i * config_.channels]));
        if (config_.channels > 1) {
            peak_right = std::max(peak_right, std::abs(samples[i * config_.channels + 1]));
        }
    }
    
    impl_->stats.peak_level_left = peak_left;
    impl_->stats.peak_level_right = config_.channels > 1 ? peak_right : peak_left;
}

bool AudioStreamEncoder::encode_and_send(const float* samples, size_t frames) {
    switch (config_.codec) {
        case StreamCodec::MP3:
            return encode_and_send_mp3(samples, frames);
        case StreamCodec::OGG_VORBIS:
            return encode_and_send_vorbis(samples, frames);
        case StreamCodec::OGG_OPUS:
            return encode_and_send_opus(samples, frames);
        case StreamCodec::AAC:
            return encode_and_send_aac(samples, frames);
        default:
            return false;
    }
}

bool AudioStreamEncoder::encode_and_send_mp3(const float* samples, size_t frames) {
    Logger::error("MP3 encoding not implemented - missing LAME library");
    return false;
}

bool AudioStreamEncoder::encode_and_send_vorbis(const float* samples, size_t frames) {
    if (!impl_ || !impl_->shout) {
        Logger::error("Vorbis encoding not available - libraries not installed");
        return false;
    }
    
    // Vorbis encoding disabled until libraries are installed
    Logger::error("Vorbis encoding not implemented - missing libvorbis");
    return false;
}

bool AudioStreamEncoder::encode_and_send_opus(const float* samples, size_t frames) {
    if (!impl_ || !impl_->shout) {
        Logger::error("Opus encoding not available - libraries not installed");
        return false;
    }
    
    // Opus encoding disabled until libraries are installed
    Logger::error("Opus encoding not implemented - missing libopus");
    return false;
}

bool AudioStreamEncoder::encode_and_send_aac(const float* samples, size_t frames) {
    if (!impl_ || !impl_->codec_context || !impl_->frame || !impl_->shout) {
        Logger::error("AAC encoding not fully available - missing components");
        return false;
    }
    
    try {
        // Configure frame
        impl_->frame->nb_samples = frames;
        impl_->frame->format = impl_->codec_context->sample_fmt;
        impl_->frame->channel_layout = impl_->codec_context->channel_layout;
        
        // Get buffer for frame
        int ret = av_frame_get_buffer(impl_->frame, 0);
        if (ret < 0) {
            Logger::error("Failed to get AAC frame buffer");
            return false;
        }
        
        // Convert interleaved float samples to planar format
        float** frame_data = (float**)impl_->frame->data;
        
        if (config_.channels == 1) {
            // Mono
            memcpy(frame_data[0], samples, frames * sizeof(float));
        } else {
            // Stereo - deinterleave
            for (size_t i = 0; i < frames; i++) {
                frame_data[0][i] = samples[i * 2];     // Left
                frame_data[1][i] = samples[i * 2 + 1]; // Right
            }
        }
        
        // Send frame to encoder
        ret = avcodec_send_frame(impl_->codec_context, impl_->frame);
        if (ret < 0) {
            Logger::error("Failed to send frame to AAC encoder: " + std::to_string(ret));
            return false;
        }
        
        // Receive encoded packets
        while (ret >= 0) {
            ret = avcodec_receive_packet(impl_->codec_context, impl_->packet);
            
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break; // Need more input or end of stream
            } else if (ret < 0) {
                Logger::error("AAC encoding error: " + std::to_string(ret));
                return false;
            }
            
            // Send encoded packet to server
            int shout_ret = shout_send(impl_->shout, impl_->packet->data, impl_->packet->size);
            if (shout_ret != SHOUTERR_SUCCESS) {
                Logger::error("Failed to send AAC data: " + std::string(shout_get_error(impl_->shout)));
                av_packet_unref(impl_->packet);
                return false;
            }
            
            // Update statistics
            impl_->bytes_sent += impl_->packet->size;
            
            // Clean up packet
            av_packet_unref(impl_->packet);
        }
        
        // Sync with server
        shout_sync(impl_->shout);
        
        // Clean up frame
        av_frame_unref(impl_->frame);
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("AAC encoding error: " + std::string(e.what()));
        return false;
    }
}

void AudioStreamEncoder::handle_connection_error(const std::string& error) {
    status_ = StreamStatus::ERROR;
    status_message_ = error;
    Logger::error("Stream error: " + error);
    
    if (config_.auto_reconnect) {
        // TODO: Implement auto-reconnection logic
        Logger::info("Auto-reconnect will be attempted");
    }
}

void AudioStreamEncoder::update_statistics() {
    auto now = std::chrono::steady_clock::now();
    impl_->stats.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - impl_->start_time).count();
    
    // Calculate current bitrate based on bytes sent
    if (impl_->stats.total_time > 0) {
        impl_->stats.current_bitrate = (impl_->stats.bytes_sent * 8.0) / 
                                      (impl_->stats.total_time / 1000.0) / 1000.0;
    }
}

// Static utility functions
std::vector<StreamCodec> AudioStreamEncoder::get_supported_codecs(StreamProtocol protocol) {
    switch (protocol) {
        case StreamProtocol::ICECAST2:
            return {StreamCodec::MP3, StreamCodec::OGG_VORBIS, StreamCodec::OGG_OPUS, StreamCodec::AAC};
        case StreamProtocol::SHOUTCAST:
            return {StreamCodec::MP3, StreamCodec::AAC};
        default:
            return {};
    }
}

std::vector<int> AudioStreamEncoder::get_supported_bitrates(StreamCodec codec) {
    switch (codec) {
        case StreamCodec::MP3:
            return {64, 96, 128, 160, 192, 256, 320};
        case StreamCodec::OGG_VORBIS:
        case StreamCodec::OGG_OPUS:
            return {64, 96, 128, 160, 192, 256};
        case StreamCodec::AAC:
            return {64, 96, 128, 160, 192, 256, 320};
        default:
            return {};
    }
}

std::vector<int> AudioStreamEncoder::get_supported_sample_rates() {
    return {8000, 11025, 16000, 22050, 32000, 44100, 48000};
}

std::string AudioStreamEncoder::codec_to_string(StreamCodec codec) {
    switch (codec) {
        case StreamCodec::MP3: return "MP3";
        case StreamCodec::OGG_VORBIS: return "OGG Vorbis";
        case StreamCodec::OGG_OPUS: return "OGG Opus";
        case StreamCodec::AAC: return "AAC";
        case StreamCodec::FLAC: return "FLAC";
        default: return "Unknown";
    }
}

std::string AudioStreamEncoder::protocol_to_string(StreamProtocol protocol) {
    switch (protocol) {
        case StreamProtocol::ICECAST2: return "Icecast2";
        case StreamProtocol::SHOUTCAST: return "SHOUTcast";
        case StreamProtocol::HTTP: return "HTTP";
        case StreamProtocol::RTMP: return "RTMP";
        default: return "Unknown";
    }
}

// StreamConfigBuilder implementation
StreamConfigBuilder& StreamConfigBuilder::icecast2(const std::string& host, int port, const std::string& mount, const std::string& password) {
    config_.protocol = StreamProtocol::ICECAST2;
    config_.server_host = host;
    config_.server_port = port;
    config_.mount_point = mount;
    config_.password = password;
    return *this;
}

StreamConfigBuilder& StreamConfigBuilder::shoutcast(const std::string& host, int port, const std::string& password, const std::string& username) {
    config_.protocol = StreamProtocol::SHOUTCAST;
    config_.server_host = host;
    config_.server_port = port;
    config_.password = password;
    config_.username = username;
    return *this;
}

StreamConfigBuilder& StreamConfigBuilder::mp3(int bitrate, int sample_rate) {
    config_.codec = StreamCodec::MP3;
    config_.bitrate = bitrate;
    config_.sample_rate = sample_rate;
    return *this;
}

StreamConfigBuilder& StreamConfigBuilder::metadata(const std::string& name, const std::string& description, const std::string& genre) {
    config_.stream_name = name;
    config_.stream_description = description;
    config_.stream_genre = genre;
    return *this;
}