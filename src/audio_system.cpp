#include "audio_system.hpp"
#include "utils/logger.hpp"
#include <portaudio.h>
#include <samplerate.h>
#include <fftw3.h>
#include <sndfile.h>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>
#include <cstring>
#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

/**
 * AudioSystem Implementation Class
 */
class AudioSystem::Impl {
public:
    Impl(AudioSystem* parent) 
        : parent_(parent)
        , pa_stream_(nullptr)
        , input_device_(-1)
        , output_device_(-1)
        , crossfader_position_(0.0f)
        , master_volume_(0.8f)
        , sample_rate_(48000)
        , channels_(2)
        , frames_per_buffer_(512) {
        
        // Initialize PortAudio
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            Logger::error("Failed to initialize PortAudio: " + std::string(Pa_GetErrorText(err)));
            return;
        }
        
        // Initialize FFmpeg (functions deprecated in newer versions)
        // av_register_all(); // Deprecated in FFmpeg 4.0+
        // avcodec_register_all(); // Deprecated in FFmpeg 4.0+
        
        Logger::info("AudioSystem implementation initialized");
    }
    
    ~Impl() {
        stop_audio_stream();
        Pa_Terminate();
        
        // Cleanup FFmpeg resources
        cleanup_encoders();
    }
    
    bool initialize(const AudioFormat& format) {
        format_ = format;
        sample_rate_ = format.sample_rate;
        channels_ = format.channels;
        
        // Initialize audio buffers
        input_buffer_.resize(frames_per_buffer_ * channels_);
        output_buffer_.resize(frames_per_buffer_ * channels_);
        mix_buffer_.resize(frames_per_buffer_ * channels_);
        
        // Initialize level meters
        reset_level_meters();
        
        // Initialize master effect chain
        master_effects_ = std::make_unique<AudioEffectChain>();
        
        // Add default master limiter
        auto limiter = std::make_unique<AudioCompressor>("master_limiter");
        limiter->set_threshold(-1.0f);
        limiter->set_ratio(10.0f);
        limiter->set_attack(1.0f);
        limiter->set_release(50.0f);
        master_effects_->add_effect(std::move(limiter));
        
        Logger::info("AudioSystem initialized with " + std::to_string(sample_rate_) + " Hz, " + std::to_string(channels_) + " channels");
        return true;
    }
    
    bool start() {
        if (running_) {
            Logger::warn("AudioSystem is already running");
            return true;
        }
        
        // Setup PortAudio stream
        PaStreamParameters input_params;
        PaStreamParameters output_params;
        
        // Configure input
        input_params.device = (input_device_ >= 0) ? input_device_ : Pa_GetDefaultInputDevice();
        input_params.channelCount = channels_;
        input_params.sampleFormat = paFloat32;
        input_params.suggestedLatency = Pa_GetDeviceInfo(input_params.device)->defaultLowInputLatency;
        input_params.hostApiSpecificStreamInfo = nullptr;
        
        // Configure output
        output_params.device = (output_device_ >= 0) ? output_device_ : Pa_GetDefaultOutputDevice();
        output_params.channelCount = channels_;
        output_params.sampleFormat = paFloat32;
        output_params.suggestedLatency = Pa_GetDeviceInfo(output_params.device)->defaultLowOutputLatency;
        output_params.hostApiSpecificStreamInfo = nullptr;
        
        // Open audio stream
        PaError err = Pa_OpenStream(
            &pa_stream_,
            &input_params,
            &output_params,
            sample_rate_,
            frames_per_buffer_,
            paClipOff,
            audio_callback_static,
            this
        );
        
        if (err != paNoError) {
            Logger::error("Failed to open audio stream: " + std::string(Pa_GetErrorText(err)));
            return false;
        }
        
        // Start the stream
        err = Pa_StartStream(pa_stream_);
        if (err != paNoError) {
            Logger::error("Failed to start audio stream: " + std::string(Pa_GetErrorText(err)));
            Pa_CloseStream(pa_stream_);
            pa_stream_ = nullptr;
            return false;
        }
        
        running_ = true;
        
        // Start processing thread
        processing_thread_ = std::thread(&Impl::processing_loop, this);
        
        Logger::info("AudioSystem started successfully");
        return true;
    }
    
    void stop() {
        if (!running_) return;
        
        running_ = false;
        
        // Stop processing thread
        if (processing_thread_.joinable()) {
            processing_cv_.notify_all();
            processing_thread_.join();
        }
        
        stop_audio_stream();
        
        Logger::info("AudioSystem stopped");
    }
    
    // Audio callback function
    static int audio_callback_static(const void* input_buffer, void* output_buffer,
                                   unsigned long frames_per_buffer,
                                   const PaStreamCallbackTimeInfo* time_info,
                                   PaStreamCallbackFlags status_flags,
                                   void* user_data) {
        Impl* impl = static_cast<Impl*>(user_data);
        return impl->audio_callback(input_buffer, output_buffer, frames_per_buffer, time_info, status_flags);
    }
    
    int audio_callback(const void* input_buffer, void* output_buffer,
                      unsigned long frames_per_buffer,
                      const PaStreamCallbackTimeInfo* time_info,
                      PaStreamCallbackFlags status_flags) {
        
        const float* input = static_cast<const float*>(input_buffer);
        float* output = static_cast<float*>(output_buffer);
        
        // Clear output buffer
        std::fill(output, output + frames_per_buffer * channels_, 0.0f);
        
        // Process microphone input
        if (mic_enabled_ && input) {
            process_microphone_input(input, frames_per_buffer);
        }
        
        // Mix all active audio channels
        mix_audio_channels(output, frames_per_buffer);
        
        // Apply master effects
        if (master_effects_) {
            master_effects_->process(output, frames_per_buffer, channels_);
        }
        
        // Apply master volume
        for (unsigned long i = 0; i < frames_per_buffer * channels_; ++i) {
            output[i] *= master_volume_;
        }
        
        // Update level meters
        update_level_meters(output, frames_per_buffer);
        
        // Call external audio callback if set
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (parent_->audio_callback_) {
                parent_->audio_callback_(input, output, frames_per_buffer, channels_);
            }
        }
        
        return paContinue;
    }
    
    void process_microphone_input(const float* input, unsigned long frames) {
        if (!mic_enabled_) return;
        
        std::lock_guard<std::mutex> lock(mic_mutex_);
        
        // Apply microphone gain
        for (unsigned long i = 0; i < frames * channels_; ++i) {
            mic_buffer_[i] = input[i] * mic_config_.gain;
        }
        
        // Apply noise gate
        if (mic_config_.gate_threshold > -60.0f) {
            apply_noise_gate(mic_buffer_.data(), frames);
        }
        
        // Update microphone levels
        update_microphone_levels(mic_buffer_.data(), frames);
        
        // Mix microphone into master output (this will be done in mix_audio_channels)
    }
    
    void mix_audio_channels(float* output, unsigned long frames) {
        // Clear mix buffer
        std::fill(mix_buffer_.begin(), mix_buffer_.end(), 0.0f);
        
        // Mix all active channels
        {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            for (auto& [channel_id, channel] : active_channels_) {
                if (channel && channel->is_playing()) {
                    // Get audio from channel
                    channel->process_audio(channel_buffer_.data(), frames, channels_);
                    
                    // Apply crossfader
                    float channel_gain = calculate_crossfader_gain(channel_id);
                    
                    // Mix into buffer
                    for (unsigned long i = 0; i < frames * channels_; ++i) {
                        mix_buffer_[i] += channel_buffer_[i] * channel_gain;
                    }
                }
            }
        }
        
        // Add microphone if enabled
        if (mic_enabled_) {
            std::lock_guard<std::mutex> lock(mic_mutex_);
            for (unsigned long i = 0; i < frames * channels_; ++i) {
                mix_buffer_[i] += mic_buffer_[i];
            }
        }
        
        // Copy to output
        std::copy(mix_buffer_.begin(), mix_buffer_.begin() + frames * channels_, output);
    }
    
    float calculate_crossfader_gain(const std::string& channel_id) {
        // Simple A/B crossfader implementation
        if (channel_id.find("A") != std::string::npos) {
            return (crossfader_position_ <= 0.0f) ? 1.0f : (1.0f - crossfader_position_);
        } else if (channel_id.find("B") != std::string::npos) {
            return (crossfader_position_ >= 0.0f) ? 1.0f : (1.0f + crossfader_position_);
        }
        return 1.0f; // Other channels not affected by crossfader
    }
    
    void apply_noise_gate(float* samples, unsigned long frames) {
        const float threshold_linear = std::pow(10.0f, mic_config_.gate_threshold / 20.0f);
        
        for (unsigned long i = 0; i < frames; ++i) {
            float sample_level = 0.0f;
            
            // Calculate RMS for this frame (stereo)
            for (int ch = 0; ch < channels_; ++ch) {
                float sample = samples[i * channels_ + ch];
                sample_level += sample * sample;
            }
            sample_level = std::sqrt(sample_level / channels_);
            
            // Apply gate
            if (sample_level < threshold_linear) {
                for (int ch = 0; ch < channels_; ++ch) {
                    samples[i * channels_ + ch] = 0.0f;
                }
            }
        }
    }
    
    void update_level_meters(const float* samples, unsigned long frames) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        // Calculate levels for master output
        float left_peak = 0.0f, right_peak = 0.0f;
        float left_rms = 0.0f, right_rms = 0.0f;
        
        for (unsigned long i = 0; i < frames; ++i) {
            float left = std::abs(samples[i * channels_]);
            float right = (channels_ > 1) ? std::abs(samples[i * channels_ + 1]) : left;
            
            left_peak = std::max(left_peak, left);
            right_peak = std::max(right_peak, right);
            
            left_rms += left * left;
            right_rms += right * right;
        }
        
        left_rms = std::sqrt(left_rms / frames);
        right_rms = std::sqrt(right_rms / frames);
        
        // Update master levels
        {
            std::lock_guard<std::mutex> lock(levels_mutex_);
            master_levels_.left_peak = left_peak;
            master_levels_.right_peak = right_peak;
            master_levels_.left_rms = left_rms;
            master_levels_.right_rms = right_rms;
            master_levels_.left_db = 20.0f * std::log10(std::max(left_rms, 1e-6f));
            master_levels_.right_db = 20.0f * std::log10(std::max(right_rms, 1e-6f));
            master_levels_.clipping = (left_peak > 0.95f) || (right_peak > 0.95f);
            master_levels_.timestamp = now;
        }
    }
    
    void update_microphone_levels(const float* samples, unsigned long frames) {
        if (!samples) return;
        
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        float left_peak = 0.0f, right_peak = 0.0f;
        float left_rms = 0.0f, right_rms = 0.0f;
        
        for (unsigned long i = 0; i < frames; ++i) {
            float left = std::abs(samples[i * channels_]);
            float right = (channels_ > 1) ? std::abs(samples[i * channels_ + 1]) : left;
            
            left_peak = std::max(left_peak, left);
            right_peak = std::max(right_peak, right);
            
            left_rms += left * left;
            right_rms += right * right;
        }
        
        left_rms = std::sqrt(left_rms / frames);
        right_rms = std::sqrt(right_rms / frames);
        
        // Update microphone levels
        {
            std::lock_guard<std::mutex> lock(levels_mutex_);
            mic_levels_.left_peak = left_peak;
            mic_levels_.right_peak = right_peak;
            mic_levels_.left_rms = left_rms;
            mic_levels_.right_rms = right_rms;
            mic_levels_.left_db = 20.0f * std::log10(std::max(left_rms, 1e-6f));
            mic_levels_.right_db = 20.0f * std::log10(std::max(right_rms, 1e-6f));
            mic_levels_.clipping = (left_peak > 0.95f) || (right_peak > 0.95f);
            mic_levels_.timestamp = now;
        }
    }
    
    void reset_level_meters() {
        std::lock_guard<std::mutex> lock(levels_mutex_);
        master_levels_ = AudioLevels();
        mic_levels_ = AudioLevels();
    }
    
    void stop_audio_stream() {
        if (pa_stream_) {
            Pa_StopStream(pa_stream_);
            Pa_CloseStream(pa_stream_);
            pa_stream_ = nullptr;
        }
    }
    
    void processing_loop() {
        Logger::info("Audio processing thread started");
        
        while (running_) {
            std::unique_lock<std::mutex> lock(processing_mutex_);
            processing_cv_.wait_for(lock, std::chrono::milliseconds(10));
            
            // Process streaming if enabled
            if (parent_->streaming_) {
                process_streaming();
            }
            
            // Process recording if enabled
            if (parent_->recording_) {
                process_recording();
            }
            
            // Update BPM detection
            update_bpm_detection();
        }
        
        Logger::info("Audio processing thread stopped");
    }
    
    void process_streaming() {
        // Encode and stream audio to configured targets
        // This would be implemented with actual streaming protocols
        // For now, just log activity
        static auto last_log = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 10) {
            Logger::info("Streaming audio to " + std::to_string(parent_->stream_targets_.size()) + " targets");
            last_log = now;
        }
    }
    
    void process_recording() {
        // Record mixed audio to file
        // This would write the mixed output to an audio file
        static auto last_log = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 5) {
            Logger::info("Recording audio...");
            last_log = now;
        }
    }
    
    void update_bpm_detection() {
        // Implement BPM detection algorithm
        // This would analyze audio for beat detection
    }
    
    void cleanup_encoders() {
        // Cleanup any FFmpeg encoder contexts
        if (recording_context_) {
            avcodec_free_context(&recording_context_);
        }
        
        for (auto& [name, context] : streaming_contexts_) {
            if (context) {
                avcodec_free_context(&context);
            }
        }
        streaming_contexts_.clear();
    }
    
    bool initialize_microphone() {
        Logger::info("AudioSystem::Impl: Initializing microphone");
        
        // Initialize microphone buffer
        mic_buffer_.resize(frames_per_buffer_ * channels_);
        std::fill(mic_buffer_.begin(), mic_buffer_.end(), 0.0f);
        
        // Set default microphone configuration
        mic_config_.enabled = true;
        mic_config_.gain = 1.0f;
        mic_config_.gate_threshold = -40.0f;
        mic_config_.noise_suppression = true;
        mic_config_.echo_cancellation = true;
        mic_config_.auto_gain_control = false;
        
        Logger::info("AudioSystem::Impl: Microphone initialized successfully");
        return true;
    }
    


public:
    // Member variables
    AudioSystem* parent_;
    AudioFormat format_;
    
    // PortAudio
    PaStream* pa_stream_;
    int input_device_;
    int output_device_;
    int sample_rate_;
    int channels_;
    int frames_per_buffer_;
    std::atomic<bool> running_{false};
    
    // Audio buffers
    std::vector<float> input_buffer_;
    std::vector<float> output_buffer_;
    std::vector<float> mix_buffer_;
    std::vector<float> channel_buffer_;
    
    // Microphone
    std::atomic<bool> mic_enabled_{false};
    MicrophoneConfig mic_config_;
    std::vector<float> mic_buffer_;
    std::mutex mic_mutex_;
    
    // Enhanced microphone control for talkover
    std::atomic<bool> microphone_enabled_{false};
    std::atomic<bool> microphone_muted_{false};
    std::atomic<bool> mic_active_{false};
    std::atomic<bool> mic_initialized_{false};
    
    // Audio channels
    std::map<std::string, std::unique_ptr<AudioChannel>> active_channels_;
    std::mutex channels_mutex_;
    
    // Mixing
    std::atomic<float> crossfader_position_{0.0f};
    std::atomic<float> master_volume_{0.8f};
    
    // Effects
    std::unique_ptr<AudioEffectChain> master_effects_;
    
    // Level meters
    AudioLevels master_levels_;
    AudioLevels mic_levels_;
    std::mutex levels_mutex_;
    
    // Audio monitoring
    std::atomic<bool> level_monitoring_enabled_{false};
    
    // Processing thread
    std::thread processing_thread_;
    std::mutex processing_mutex_;
    std::condition_variable processing_cv_;
    
    // External callback
    std::mutex callback_mutex_;
    
    // Encoding contexts
    AVCodecContext* recording_context_ = nullptr;
    std::map<std::string, AVCodecContext*> streaming_contexts_;
};

/**
 * AudioSystem public interface implementation
 */

AudioSystem::AudioSystem() : impl_(std::make_unique<Impl>(this)) {}

AudioSystem::~AudioSystem() = default;

bool AudioSystem::initialize(const AudioFormat& format) {
    format_ = format;
    return impl_->initialize(format);
}

bool AudioSystem::start() {
    return impl_->start();
}

void AudioSystem::stop() {
    impl_->stop();
}

std::vector<std::string> AudioSystem::get_input_devices() {
    std::vector<std::string> devices;
    int device_count = Pa_GetDeviceCount();
    
    for (int i = 0; i < device_count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            devices.push_back(info->name);
        }
    }
    
    return devices;
}

std::vector<std::string> AudioSystem::get_output_devices() {
    std::vector<std::string> devices;
    int device_count = Pa_GetDeviceCount();
    
    for (int i = 0; i < device_count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            devices.push_back(info->name);
        }
    }
    
    return devices;
}

bool AudioSystem::enable_microphone(const MicrophoneConfig& config) {
    impl_->mic_config_ = config;
    impl_->mic_enabled_ = config.enabled;
    
    if (config.enabled) {
        // Resize microphone buffer
        impl_->mic_buffer_.resize(impl_->frames_per_buffer_ * impl_->channels_);
        Logger::info("Microphone enabled with gain: " + std::to_string(config.gain));
    } else {
        Logger::info("Microphone disabled");
    }
    
    return true;
}

bool AudioSystem::disable_microphone() {
    impl_->mic_enabled_ = false;
    impl_->mic_config_.enabled = false;
    Logger::info("Microphone disabled");
    return true;
}

bool AudioSystem::set_microphone_gain(float gain) {
    impl_->mic_config_.gain = std::clamp(gain, 0.0f, 2.0f);
    Logger::info("Microphone gain set to: " + std::to_string(gain));
    return true;
}

std::string AudioSystem::create_audio_channel() {
    static int channel_counter = 0;
    std::string channel_id = "channel_" + std::to_string(++channel_counter);
    
    auto channel = std::make_unique<AudioChannel>(channel_id);
    
    {
        std::lock_guard<std::mutex> lock(impl_->channels_mutex_);
        impl_->active_channels_[channel_id] = std::move(channel);
    }
    
    Logger::info("Created audio channel: " + channel_id);
    return channel_id;
}

bool AudioSystem::set_crossfader_position(float position) {
    impl_->crossfader_position_ = std::clamp(position, -1.0f, 1.0f);
    return true;
}

bool AudioSystem::set_master_volume(float volume) {
    impl_->master_volume_ = std::clamp(volume, 0.0f, 1.0f);
    return true;
}

AudioLevels AudioSystem::get_master_levels() {
    std::lock_guard<std::mutex> lock(impl_->levels_mutex_);
    return impl_->master_levels_;
}

AudioLevels AudioSystem::get_microphone_levels() {
    std::lock_guard<std::mutex> lock(impl_->levels_mutex_);
    return impl_->mic_levels_;
}

bool AudioSystem::start_streaming() {
    streaming_ = true;
    Logger::info("Audio streaming started");
    return true;
}

bool AudioSystem::stop_streaming() {
    streaming_ = false;
    Logger::info("Audio streaming stopped");
    return true;
}

bool AudioSystem::start_recording(const std::string& output_file, const AudioFormat& format) {
    recording_ = true;
    Logger::info("Audio recording started: " + output_file);
    return true;
}

bool AudioSystem::stop_recording() {
    recording_ = false;
    Logger::info("Audio recording stopped");
    return true;
}

void AudioSystem::set_audio_callback(AudioCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex_);
    audio_callback_ = callback;
}

// Placeholder implementations for remaining methods
bool AudioSystem::set_input_device(int device_id) { return true; }
bool AudioSystem::set_output_device(int device_id) { return true; }
bool AudioSystem::destroy_audio_channel(const std::string& channel_id) { return true; }

bool AudioSystem::play_channel(const std::string& channel_id) { return true; }
MicrophoneConfig AudioSystem::get_microphone_config() const { return mic_config_; }
AudioChannelConfig AudioSystem::get_channel_config(const std::string& channel_id) { return {}; }
std::vector<std::string> AudioSystem::get_active_channels() { return {}; }
bool AudioSystem::configure_channel(const std::string& channel_id, const AudioChannelConfig& config) { return true; }
bool AudioSystem::pause_channel(const std::string& channel_id) { return true; }
bool AudioSystem::stop_channel(const std::string& channel_id) { return true; }
bool AudioSystem::set_channel_position(const std::string& channel_id, double position_seconds) { return true; }
double AudioSystem::get_channel_position(const std::string& channel_id) { return 0.0; }
double AudioSystem::get_channel_duration(const std::string& channel_id) { return 0.0; }
bool AudioSystem::set_headphone_cue(const std::string& channel_id, bool enabled) { return true; }

bool AudioSystem::set_channel_eq(const std::string& channel_id, const std::vector<EQBand>& bands) { return true; }
AudioLevels AudioSystem::get_channel_levels(const std::string& channel_id) { return {}; }
AudioLevels AudioSystem::get_headphone_levels() { return {}; }
bool AudioSystem::add_stream_target(const std::string& name, const StreamingConfig& config) { return true; }
bool AudioSystem::remove_stream_target(const std::string& name) { return true; }
std::map<std::string, StreamingConfig> AudioSystem::get_stream_targets() { return {}; }
bool AudioSystem::enable_channel_compressor(const std::string& channel_id, bool enabled) { return true; }
bool AudioSystem::set_compressor_settings(const std::string& channel_id, float threshold, float ratio, float attack, float release) { return true; }
bool AudioSystem::enable_reverb(bool enabled, float room_size, float damping, float wet_level) { return true; }
bool AudioSystem::enable_delay(bool enabled, float delay_time, float feedback, float wet_level) { return true; }
bool AudioSystem::enable_auto_duck(bool enabled, float threshold, float duck_amount) { return true; }
bool AudioSystem::set_limiter(bool enabled, float threshold, float release) { return true; }
bool AudioSystem::enable_spectral_analyzer(bool enabled) { return true; }
std::vector<float> AudioSystem::get_spectrum_data(int bins) { return std::vector<float>(bins, 0.0f); }
float AudioSystem::detect_bpm(const std::string& channel_id) { return 120.0f; }
bool AudioSystem::enable_bpm_sync(const std::string& channel_a, const std::string& channel_b) { return true; }
bool AudioSystem::disable_bpm_sync() { return true; }
bool AudioSystem::set_microphone_gate_threshold(float threshold_db) { return true; }

// ===== ENHANCED MICROPHONE AND TALKOVER SUPPORT =====

bool AudioSystem::enable_microphone_input(bool enabled) {
    Logger::info("AudioSystem: " + std::string(enabled ? "Enabling" : "Disabling") + " microphone input");
    
    std::lock_guard<std::mutex> lock(impl_->mic_mutex_);
    impl_->microphone_enabled_ = enabled;
    
    if (enabled) {
        // Initialize microphone stream if not already done
        if (!impl_->mic_initialized_) {
            if (!impl_->initialize_microphone()) {
                Logger::error("AudioSystem: Failed to initialize microphone");
                return false;
            }
            impl_->mic_initialized_ = true;
        }
        
        // Start microphone processing
        impl_->mic_active_ = true;
        Logger::info("AudioSystem: Microphone input enabled");
    } else {
        impl_->mic_active_ = false;
        Logger::info("AudioSystem: Microphone input disabled");
    }
    
    return true;
}

bool AudioSystem::set_microphone_mute(bool muted) {
    std::lock_guard<std::mutex> lock(impl_->mic_mutex_);
    impl_->microphone_muted_ = muted;
    
    Logger::info("AudioSystem: Microphone " + std::string(muted ? "muted" : "unmuted"));
    return true;
}





bool AudioSystem::fade_master_volume(float target_volume, float fade_time_ms) {
    Logger::info("AudioSystem: Fading master volume to " + std::to_string(target_volume) + 
                " over " + std::to_string(fade_time_ms) + "ms");
    
    // Start volume fade in separate thread
    std::thread fade_thread([this, target_volume, fade_time_ms]() {
        const float start_volume = impl_->master_volume_;
        const float volume_delta = target_volume - start_volume;
        const int steps = static_cast<int>(fade_time_ms / 10.0f); // 10ms per step
        const float step_delta = volume_delta / steps;
        
        for (int i = 0; i < steps; ++i) {
            float current_volume = start_volume + (step_delta * i);
            impl_->master_volume_ = std::clamp(current_volume, 0.0f, 1.0f);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Ensure final volume is exact
        impl_->master_volume_ = std::clamp(target_volume, 0.0f, 1.0f);
    });
    
    fade_thread.detach();
    return true;
}



bool AudioSystem::generate_waveform(const std::string& file_path, int width_pixels,
                                   std::vector<float>& peaks, std::vector<float>& rms) {
    Logger::info("AudioSystem: Generating waveform for " + file_path + " with " + std::to_string(width_pixels) + " pixels");
    
    // Open audio file using libsndfile
    SF_INFO sf_info;
    SNDFILE* file = sf_open(file_path.c_str(), SFM_READ, &sf_info);
    if (!file) {
        Logger::error("AudioSystem: Failed to open audio file for waveform generation: " + file_path);
        return false;
    }
    
    // Calculate samples per pixel
    const int samples_per_pixel = sf_info.frames / width_pixels;
    if (samples_per_pixel <= 0) {
        Logger::error("AudioSystem: Invalid samples per pixel calculation");
        sf_close(file);
        return false;
    }
    
    // Prepare output vectors
    peaks.clear();
    rms.clear();
    peaks.reserve(width_pixels);
    rms.reserve(width_pixels);
    
    // Read and process audio data
    std::vector<float> buffer(samples_per_pixel * sf_info.channels);
    
    for (int pixel = 0; pixel < width_pixels; ++pixel) {
        // Read chunk of audio data
        sf_count_t frames_read = sf_readf_float(file, buffer.data(), samples_per_pixel);
        if (frames_read <= 0) {
            // End of file - fill remaining with zeros
            peaks.push_back(0.0f);
            rms.push_back(0.0f);
            continue;
        }
        
        // Calculate peak and RMS for this chunk
        float max_peak = 0.0f;
        float rms_sum = 0.0f;
        int sample_count = frames_read * sf_info.channels;
        
        for (int i = 0; i < sample_count; ++i) {
            float sample = std::abs(buffer[i]);
            max_peak = std::max(max_peak, sample);
            rms_sum += sample * sample;
        }
        
        float rms_value = std::sqrt(rms_sum / sample_count);
        
        peaks.push_back(max_peak);
        rms.push_back(rms_value);
    }
    
    sf_close(file);
    
    Logger::info("AudioSystem: Generated waveform with " + std::to_string(peaks.size()) + " data points");
    return true;
}

// ===== CHANNEL CONTROL METHODS =====

bool AudioSystem::load_audio_file(const std::string& channel_id, const std::string& file_path) {
    Logger::info("AudioSystem: Loading audio file " + file_path + " into channel " + channel_id);
    
    if (!std::filesystem::exists(file_path)) {
        Logger::error("AudioSystem: File does not exist: " + file_path);
        return false;
    }
    
    // Open the audio file using libsndfile
    SF_INFO file_info;
    memset(&file_info, 0, sizeof(file_info));
    
    SNDFILE* file = sf_open(file_path.c_str(), SFM_READ, &file_info);
    if (!file) {
        Logger::error("AudioSystem: Failed to open audio file: " + file_path);
        return false;
    }
    
    // Store file information for the channel
    if (channel_id == "A") {
        channel_a_file_ = file;
        channel_a_info_ = file_info;
        channel_a_loaded_ = true;
        channel_a_position_ = 0;
    } else if (channel_id == "B") {
        channel_b_file_ = file;
        channel_b_info_ = file_info;
        channel_b_loaded_ = true;
        channel_b_position_ = 0;
    } else {
        sf_close(file);
        Logger::error("AudioSystem: Invalid channel ID: " + channel_id);
        return false;
    }
    
    Logger::info("AudioSystem: Successfully loaded audio file into channel " + channel_id + 
                " (Sample Rate: " + std::to_string(file_info.samplerate) + 
                ", Channels: " + std::to_string(file_info.channels) + 
                ", Duration: " + std::to_string(file_info.frames / file_info.samplerate) + "s)");
    
    return true;
}

bool AudioSystem::set_channel_playback(const std::string& channel_id, bool play) {
    Logger::info("AudioSystem: Setting channel " + channel_id + " playback to " + (play ? "play" : "stop"));
    
    if (channel_id == "A") {
        if (!channel_a_loaded_) {
            Logger::error("AudioSystem: No audio file loaded in channel A");
            return false;
        }
        channel_a_playing_ = play;
        if (!play) {
            channel_a_position_ = 0; // Reset position when stopping
        }
    } else if (channel_id == "B") {
        if (!channel_b_loaded_) {
            Logger::error("AudioSystem: No audio file loaded in channel B");
            return false;
        }
        channel_b_playing_ = play;
        if (!play) {
            channel_b_position_ = 0; // Reset position when stopping
        }
    } else {
        Logger::error("AudioSystem: Invalid channel ID: " + channel_id);
        return false;
    }
    
    Logger::info("AudioSystem: Channel " + channel_id + " playback set to " + (play ? "playing" : "stopped"));
    return true;
}

bool AudioSystem::set_channel_volume(const std::string& channel_id, float volume) {
    Logger::info("AudioSystem: Setting channel " + channel_id + " volume to " + std::to_string(volume));
    
    // Clamp volume to valid range (0.0 to 1.0)
    volume = std::max(0.0f, std::min(1.0f, volume));
    
    if (channel_id == "A") {
        channel_a_volume_ = volume;
    } else if (channel_id == "B") {
        channel_b_volume_ = volume;
    } else {
        Logger::error("AudioSystem: Invalid channel ID: " + channel_id);
        return false;
    }
    
    Logger::info("AudioSystem: Channel " + channel_id + " volume set to " + std::to_string(volume));
    return true;
}

bool AudioSystem::set_channel_eq(const std::string& channel_id, float bass, float mid, float treble) {
    Logger::info("AudioSystem: Setting channel " + channel_id + " EQ - Bass: " + 
                 std::to_string(bass) + ", Mid: " + std::to_string(mid) + ", Treble: " + std::to_string(treble));
    
    // Store EQ settings for the channel
    if (channel_id == "A") {
        channel_a_eq_bass_ = bass;
        channel_a_eq_mid_ = mid;
        channel_a_eq_treble_ = treble;
    } else if (channel_id == "B") {
        channel_b_eq_bass_ = bass;
        channel_b_eq_mid_ = mid;
        channel_b_eq_treble_ = treble;
    } else {
        Logger::error("AudioSystem: Invalid channel ID: " + channel_id);
        return false;
    }
    
    Logger::info("AudioSystem: Channel " + channel_id + " EQ settings updated");
    return true;
}

// ===== AUDIO LEVEL MONITORING =====

bool AudioSystem::enable_level_monitoring(bool enabled) {
    Logger::info("AudioSystem: " + std::string(enabled ? "Enabling" : "Disabling") + " audio level monitoring");
    
    level_monitoring_enabled_ = enabled;
    
    if (enabled) {
        // Reset level accumulators
        master_peak_left_ = 0.0f;
        master_peak_right_ = 0.0f;
        master_rms_left_ = 0.0f;
        master_rms_right_ = 0.0f;
        microphone_level_ = 0.0f;
        
        Logger::info("AudioSystem: Level monitoring enabled");
    } else {
        Logger::info("AudioSystem: Level monitoring disabled");
    }
    
    return true;
}

AudioLevels AudioSystem::get_master_audio_levels() {
    AudioLevels levels;
    
    if (!level_monitoring_enabled_) {
        return levels; // Return zeroed levels
    }
    
    // Get current master levels (these would be calculated during audio processing)
    levels.left_peak = master_peak_left_;
    levels.right_peak = master_peak_right_;
    levels.left_rms = master_rms_left_;
    levels.right_rms = master_rms_right_;
    levels.clipping = (master_peak_left_ > 1.0f || master_peak_right_ > 1.0f);
    
    return levels;
}

float AudioSystem::get_microphone_level() {
    if (!impl_->microphone_enabled_ || !level_monitoring_enabled_) {
        return 0.0f;
    }
    
    return microphone_level_;
}

bool AudioSystem::is_microphone_enabled() const {
    return impl_->microphone_enabled_;
}