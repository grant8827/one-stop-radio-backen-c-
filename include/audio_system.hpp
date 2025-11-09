#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <sndfile.h>
}

// Forward declarations
class AudioMixer;
class AudioEffect;
class AudioCompressor;
class AudioEqualizer;

/**
 * Audio format configuration
 */
struct AudioFormat {
    int sample_rate = 48000;    // Hz
    int channels = 2;           // Stereo
    int bit_depth = 16;         // bits per sample
    int bitrate = 128000;       // bps for encoding
    std::string codec = "mp3";  // mp3, aac, opus, ogg
};

/**
 * Audio level meters data
 */
struct AudioLevels {
    float left_peak = 0.0f;      // Peak level (0.0-1.0)
    float right_peak = 0.0f;     // Peak level (0.0-1.0)
    float left_rms = 0.0f;       // RMS level (0.0-1.0)
    float right_rms = 0.0f;      // RMS level (0.0-1.0)
    float left_db = -60.0f;      // dB level
    float right_db = -60.0f;     // dB level
    bool clipping = false;       // Clipping detection
    uint64_t timestamp = 0;      // Timestamp in ms
};

/**
 * Microphone configuration
 */
struct MicrophoneConfig {
    bool enabled = false;
    float gain = 1.0f;           // Linear gain (0.0-2.0)
    float gate_threshold = -40.0f; // Noise gate threshold in dB
    bool noise_suppression = true;
    bool echo_cancellation = true;
    bool auto_gain_control = false;
    int device_id = 0;           // Audio device index
};

/**
 * EQ Band configuration
 */
struct EQBand {
    float frequency = 1000.0f;   // Hz
    float gain = 0.0f;          // dB (-20 to +20)
    float q_factor = 1.0f;      // Q factor (0.1 to 10.0)
    std::string type = "peak";   // peak, lowpass, highpass, lowshelf, highshelf
};

/**
 * Audio channel configuration
 */
struct AudioChannelConfig {
    std::string id;
    float volume = 1.0f;        // Linear volume (0.0-1.0)
    float pan = 0.0f;           // Pan (-1.0 left, 0.0 center, 1.0 right)
    bool muted = false;
    bool solo = false;
    std::vector<EQBand> eq_bands;
    bool compressor_enabled = false;
    float compressor_threshold = -12.0f; // dB
    float compressor_ratio = 4.0f;
    float compressor_attack = 10.0f;     // ms
    float compressor_release = 100.0f;   // ms
};

/**
 * Audio streaming configuration
 */
struct StreamingConfig {
    std::string server_url;
    std::string stream_key;
    std::string title;
    std::string description;
    AudioFormat format;
    bool enabled = false;
};

/**
 * Complete Audio Processing System
 * Handles all audio duties for the radio platform
 */
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    // System lifecycle
    bool initialize(const AudioFormat& format = {});
    bool start();
    void stop();
    bool is_running() const { return running_; }

    // Audio input/output
    bool set_input_device(int device_id);
    bool set_output_device(int device_id);
    std::vector<std::string> get_input_devices();
    std::vector<std::string> get_output_devices();

    // Microphone control
    bool enable_microphone(const MicrophoneConfig& config);
    bool disable_microphone();
    bool set_microphone_gain(float gain);
    bool set_microphone_gate_threshold(float threshold_db);
    MicrophoneConfig get_microphone_config() const;

    // Channel management
    std::string create_audio_channel();
    bool destroy_audio_channel(const std::string& channel_id);
    bool configure_channel(const std::string& channel_id, const AudioChannelConfig& config);
    AudioChannelConfig get_channel_config(const std::string& channel_id);
    std::vector<std::string> get_active_channels();

    // Audio file playback
    bool load_audio_file(const std::string& channel_id, const std::string& file_path);
    bool play_channel(const std::string& channel_id);
    bool pause_channel(const std::string& channel_id);
    bool stop_channel(const std::string& channel_id);
    bool set_channel_position(const std::string& channel_id, double position_seconds);
    double get_channel_position(const std::string& channel_id);
    double get_channel_duration(const std::string& channel_id);

    // Real-time audio mixing
    bool set_crossfader_position(float position); // -1.0 to 1.0
    bool set_master_volume(float volume);         // 0.0 to 1.0
    bool set_headphone_cue(const std::string& channel_id, bool enabled);
    bool set_channel_volume(const std::string& channel_id, float volume);
    bool set_channel_eq(const std::string& channel_id, const std::vector<EQBand>& bands);

    // Audio effects
    bool enable_channel_compressor(const std::string& channel_id, bool enabled);
    bool set_compressor_settings(const std::string& channel_id, float threshold, float ratio, float attack, float release);
    bool enable_reverb(bool enabled, float room_size = 0.5f, float damping = 0.5f, float wet_level = 0.3f);
    bool enable_delay(bool enabled, float delay_time = 250.0f, float feedback = 0.3f, float wet_level = 0.3f);

    // Audio levels and monitoring
    AudioLevels get_master_levels();
    AudioLevels get_channel_levels(const std::string& channel_id);
    AudioLevels get_microphone_levels();
    AudioLevels get_headphone_levels();
    
    // Enhanced microphone control for talkover
    bool enable_microphone_input(bool enabled);
    bool set_microphone_mute(bool muted);
    bool is_microphone_enabled() const;
    float get_microphone_level();
    AudioLevels get_master_audio_levels(); // Returns complete master level info
    
    // Volume fading for talkover
    bool fade_master_volume(float target_volume, float fade_time_ms);
    
    // Audio monitoring control
    bool enable_level_monitoring(bool enabled);
    
    // Waveform generation
    bool generate_waveform(const std::string& file_path, int width_pixels, 
                          std::vector<float>& peaks, std::vector<float>& rms);
    
    // Channel control methods for DJ mixing
    bool set_channel_playback(const std::string& channel_id, bool play);
    bool set_channel_eq(const std::string& channel_id, float bass, float mid, float treble);

    // Recording
    bool start_recording(const std::string& output_file, const AudioFormat& format = {});
    bool stop_recording();
    bool is_recording() const { return recording_; }

    // Live streaming
    bool add_stream_target(const std::string& name, const StreamingConfig& config);
    bool remove_stream_target(const std::string& name);
    bool start_streaming();
    bool stop_streaming();
    bool is_streaming() const { return streaming_; }
    std::map<std::string, StreamingConfig> get_stream_targets();

    // Advanced features
    bool enable_auto_duck(bool enabled, float threshold = -20.0f, float duck_amount = 0.3f);
    bool set_limiter(bool enabled, float threshold = -1.0f, float release = 50.0f);
    bool enable_spectral_analyzer(bool enabled);
    std::vector<float> get_spectrum_data(int bins = 256); // FFT spectrum data

    // BPM detection and sync
    float detect_bpm(const std::string& channel_id);
    bool enable_bpm_sync(const std::string& channel_a, const std::string& channel_b);
    bool disable_bpm_sync();

    // Audio callback for external processing
    using AudioCallback = std::function<void(const float* input, float* output, int frames, int channels)>;
    void set_audio_callback(AudioCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> recording_{false};
    std::atomic<bool> streaming_{false};
    
    AudioFormat format_;
    MicrophoneConfig mic_config_;
    
    std::mutex channels_mutex_;
    std::map<std::string, AudioChannelConfig> channels_;
    
    std::mutex streaming_mutex_;
    std::map<std::string, StreamingConfig> stream_targets_;
    
    // Audio processing thread
    std::thread audio_thread_;
    std::mutex audio_mutex_;
    std::condition_variable audio_cv_;
    
    // Audio callback
    AudioCallback audio_callback_;
    std::mutex callback_mutex_;
    
    // Channel control variables
    SNDFILE* channel_a_file_ = nullptr;
    SNDFILE* channel_b_file_ = nullptr;
    SF_INFO channel_a_info_;
    SF_INFO channel_b_info_;
    std::vector<float> channel_a_buffer_;
    std::vector<float> channel_b_buffer_;
    bool channel_a_loaded_ = false;
    bool channel_b_loaded_ = false;
    bool channel_a_playing_ = false;
    bool channel_b_playing_ = false;
    sf_count_t channel_a_position_ = 0;
    sf_count_t channel_b_position_ = 0;
    float channel_a_volume_ = 0.75f;
    float channel_b_volume_ = 0.75f;
    
    // EQ settings for channels
    float channel_a_eq_bass_ = 0.0f;
    float channel_a_eq_mid_ = 0.0f;
    float channel_a_eq_treble_ = 0.0f;
    float channel_b_eq_bass_ = 0.0f;
    float channel_b_eq_mid_ = 0.0f;
    float channel_b_eq_treble_ = 0.0f;
    
    // Audio level monitoring
    bool level_monitoring_enabled_ = false;
    std::atomic<float> master_peak_left_{0.0f};
    std::atomic<float> master_peak_right_{0.0f};
    std::atomic<float> master_rms_left_{0.0f};
    std::atomic<float> master_rms_right_{0.0f};
    std::atomic<float> microphone_level_{0.0f};
};

/**
 * Audio Channel Implementation
 */
class AudioChannel {
public:
    AudioChannel(const std::string& id);
    ~AudioChannel();

    bool load_file(const std::string& file_path);
    bool play();
    bool pause();
    bool stop();
    
    void set_volume(float volume);
    void set_pan(float pan);
    void set_eq(const std::vector<EQBand>& bands);
    void enable_compressor(bool enabled);
    
    AudioLevels get_levels();
    void process_audio(float* output, int frames, int channels);
    
    bool is_playing() const { return playing_; }
    double get_position() const { return position_; }
    double get_duration() const { return duration_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    std::string id_;
    std::atomic<bool> playing_{false};
    std::atomic<double> position_{0.0};
    std::atomic<double> duration_{0.0};
    std::atomic<float> volume_{1.0f};
    std::atomic<float> pan_{0.0f};
};

/**
 * Real-time Audio Effects
 */
class AudioEffectChain {
public:
    AudioEffectChain();
    ~AudioEffectChain();

    void add_effect(std::unique_ptr<AudioEffect> effect);
    void remove_effect(const std::string& effect_id);
    void clear_effects();
    
    void process(float* samples, int frames, int channels);
    void set_bypass(bool bypassed) { bypassed_ = bypassed; }

private:
    std::vector<std::unique_ptr<AudioEffect>> effects_;
    std::atomic<bool> bypassed_{false};
};

/**
 * Base Audio Effect Class
 */
class AudioEffect {
public:
    AudioEffect(const std::string& id) : id_(id) {}
    virtual ~AudioEffect() = default;
    
    virtual void process(float* samples, int frames, int channels) = 0;
    virtual void reset() {}
    virtual void set_parameter(const std::string& name, float value) {}
    
    const std::string& get_id() const { return id_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

protected:
    std::string id_;
    std::atomic<bool> enabled_{true};
};

/**
 * 3-Band Equalizer
 */
class AudioEqualizer : public AudioEffect {
public:
    AudioEqualizer(const std::string& id);
    ~AudioEqualizer() override;
    
    void process(float* samples, int frames, int channels) override;
    void reset() override;
    
    void set_low_gain(float gain_db);    // Low shelf at 200Hz
    void set_mid_gain(float gain_db);    // Peak at 1kHz
    void set_high_gain(float gain_db);   // High shelf at 8kHz

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Dynamic Range Compressor
 */
class AudioCompressor : public AudioEffect {
public:
    AudioCompressor(const std::string& id);
    ~AudioCompressor() override;
    
    void process(float* samples, int frames, int channels) override;
    void reset() override;
    
    void set_threshold(float threshold_db);
    void set_ratio(float ratio);
    void set_attack(float attack_ms);
    void set_release(float release_ms);
    void set_makeup_gain(float gain_db);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Audio Spectrum Analyzer
 */
class AudioAnalyzer {
public:
    AudioAnalyzer(int fft_size = 1024);
    ~AudioAnalyzer();
    
    void process_samples(const float* samples, int frames, int channels);
    std::vector<float> get_magnitude_spectrum();
    std::vector<float> get_phase_spectrum();
    float get_rms_level();
    float get_peak_level();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};