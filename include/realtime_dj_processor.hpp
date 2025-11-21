#ifndef REALTIME_DJ_PROCESSOR_HPP
#define REALTIME_DJ_PROCESSOR_HPP

#include <iostream>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <map>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>

// Real-time DJ audio processing system
// Handles dual-deck mixing, audio level analysis, and beat detection

namespace OneStopRadio {

struct AudioSample {
    float left;
    float right;
    
    AudioSample() : left(0.0f), right(0.0f) {}
    AudioSample(float l, float r) : left(l), right(r) {}
    
    AudioSample& operator+=(const AudioSample& other) {
        left += other.left;
        right += other.right;
        return *this;
    }
    
    AudioSample operator*(float gain) const {
        return AudioSample(left * gain, right * gain);
    }
};

struct AudioBuffer {
    std::vector<AudioSample> samples;
    int sample_rate;
    int channels;
    
    AudioBuffer(size_t size, int sr = 48000, int ch = 2) 
        : samples(size), sample_rate(sr), channels(ch) {}
    
    void clear() {
        std::fill(samples.begin(), samples.end(), AudioSample());
    }
    
    size_t size() const { return samples.size(); }
    float duration() const { 
        return static_cast<float>(samples.size()) / sample_rate; 
    }
};

struct AudioLevels {
    float peak_left = 0.0f;
    float peak_right = 0.0f;
    float rms_left = 0.0f;
    float rms_right = 0.0f;
    std::chrono::steady_clock::time_point timestamp;
    
    AudioLevels() : timestamp(std::chrono::steady_clock::now()) {}
};

struct EQSettings {
    float low = 0.0f;      // -1.0 to 1.0
    float mid = 0.0f;      // -1.0 to 1.0 
    float high = 0.0f;     // -1.0 to 1.0
    float filter = 0.0f;   // -1.0 to 1.0 (low-pass to high-pass)
};

struct DeckState {
    std::string track_id;
    std::string track_title;
    std::string track_artist;
    
    bool is_playing = false;
    bool is_looping = false;
    bool is_synced = false;
    
    float position = 0.0f;          // Current position in seconds
    float volume = 0.8f;            // 0.0 to 1.0
    float pitch = 1.0f;             // 0.5 to 2.0 (playback speed multiplier)
    
    EQSettings eq;
    AudioLevels levels;
    
    // Beat detection and BPM
    float detected_bpm = 0.0f;
    float manual_bpm = 0.0f;
    float beat_position = 0.0f;     // 0.0 to 1.0 within current beat
    
    // Cue points
    std::map<std::string, float> cue_points;
    std::map<std::string, float> hot_cues;
    
    std::mutex state_mutex;
};

struct MixerState {
    float crossfader = 0.0f;        // -1.0 (A) to 1.0 (B)
    float master_volume = 0.8f;     // 0.0 to 1.0
    
    float channel_a_volume = 0.8f;  // 0.0 to 1.0
    float channel_b_volume = 0.8f;  // 0.0 to 1.0
    
    EQSettings channel_a_eq;
    EQSettings channel_b_eq;
    
    bool sync_enabled = false;
    float master_bpm = 128.0f;
    
    AudioLevels master_levels;
    
    std::mutex mixer_mutex;
};

// Simple 3-band EQ filter
class ThreeBandEQ {
private:
    struct FilterState {
        float low_freq = 0.0f;
        float mid_freq = 0.0f; 
        float high_freq = 0.0f;
    };
    
    FilterState left_state;
    FilterState right_state;
    
    int sample_rate;
    
public:
    ThreeBandEQ(int sr = 48000) : sample_rate(sr) {}
    
    AudioSample process(const AudioSample& input, const EQSettings& eq);
    void reset();
};

// Beat detection and BPM analysis
class BeatDetector {
private:
    struct BeatAnalysis {
        std::vector<float> energy_history;
        std::vector<float> onset_times;
        float current_bpm = 0.0f;
        float confidence = 0.0f;
        size_t analysis_window = 0;
    };
    
    BeatAnalysis analysis;
    int sample_rate;
    size_t buffer_size;
    
    float calculateEnergy(const AudioBuffer& buffer);
    bool detectOnset(float current_energy);
    float estimateBPM();
    
public:
    BeatDetector(int sr = 48000, size_t bs = 1024) 
        : sample_rate(sr), buffer_size(bs) {}
    
    void process(const AudioBuffer& buffer);
    float getCurrentBPM() const { return analysis.current_bpm; }
    float getBeatPosition() const;
    void reset();
};

// Real-time level meter with peak hold and decay
class LevelMeter {
private:
    float peak_left = 0.0f;
    float peak_right = 0.0f;
    float rms_left = 0.0f;
    float rms_right = 0.0f;
    
    float peak_decay_rate = 0.99f;   // Per sample decay
    size_t rms_window_size = 1024;
    
    std::queue<float> rms_buffer_left;
    std::queue<float> rms_buffer_right;
    float rms_sum_left = 0.0f;
    float rms_sum_right = 0.0f;
    
public:
    void process(const AudioBuffer& buffer);
    AudioLevels getLevels() const;
    void reset();
    void setPeakDecayRate(float rate) { peak_decay_rate = rate; }
};

// Audio crossfader with curve options
class Crossfader {
public:
    enum CurveType {
        LINEAR,
        SMOOTH,
        SHARP,
        SCRATCH
    };
    
private:
    CurveType curve_type = SMOOTH;
    
    float applyCurve(float position, float input_gain);
    
public:
    void setCurveType(CurveType type) { curve_type = type; }
    
    AudioSample mix(const AudioSample& channel_a, const AudioSample& channel_b, 
                   float crossfader_position);
};

// Main real-time DJ processor
class RealtimeDJProcessor {
private:
    // Audio configuration
    int sample_rate = 48000;
    size_t buffer_size = 1024;
    int channels = 2;
    
    // Deck states
    DeckState deck_a;
    DeckState deck_b;
    MixerState mixer;
    
    // Audio processing components
    std::unique_ptr<ThreeBandEQ> eq_a;
    std::unique_ptr<ThreeBandEQ> eq_b;
    std::unique_ptr<BeatDetector> beat_detector_a;
    std::unique_ptr<BeatDetector> beat_detector_b;
    std::unique_ptr<LevelMeter> level_meter_a;
    std::unique_ptr<LevelMeter> level_meter_b;
    std::unique_ptr<LevelMeter> master_meter;
    std::unique_ptr<Crossfader> crossfader;
    
    // Real-time processing thread
    std::atomic<bool> processing_active{false};
    std::thread processing_thread;
    
    // Audio buffers for processing
    AudioBuffer deck_a_buffer;
    AudioBuffer deck_b_buffer;
    AudioBuffer master_buffer;
    
    // Sync and timing
    std::chrono::steady_clock::time_point last_process_time;
    float sync_offset_a = 0.0f;
    float sync_offset_b = 0.0f;
    
    // WebSocket communication for real-time updates
    std::function<void(const std::string&)> websocket_callback;
    
    void processingLoop();
    void processAudioBuffer();
    void updateSyncAndBPM();
    void sendRealtimeUpdate();
    
    AudioSample processEQ(const AudioSample& input, const EQSettings& eq, 
                         ThreeBandEQ& eq_filter);
    
public:
    RealtimeDJProcessor(int sr = 48000, size_t bs = 1024);
    ~RealtimeDJProcessor();
    
    // Lifecycle management
    void start();
    void stop();
    bool isRunning() const { return processing_active.load(); }
    
    // Deck control
    void loadTrack(const std::string& deck, const std::string& track_id,
                   const std::string& title, const std::string& artist);
    void playDeck(const std::string& deck);
    void pauseDeck(const std::string& deck);
    void stopDeck(const std::string& deck);
    void cueDeck(const std::string& deck, float position = 0.0f);
    void setDeckVolume(const std::string& deck, float volume);
    void setDeckPitch(const std::string& deck, float pitch);
    void setDeckEQ(const std::string& deck, const EQSettings& eq);
    void toggleDeckLoop(const std::string& deck);
    void syncDeck(const std::string& deck, bool enable);
    
    // Cue point management
    void setCuePoint(const std::string& deck, const std::string& cue_id, float position);
    void triggerCuePoint(const std::string& deck, const std::string& cue_id);
    void setHotCue(const std::string& deck, const std::string& hot_cue_id, float position);
    void triggerHotCue(const std::string& deck, const std::string& hot_cue_id);
    
    // Mixer control
    void setCrossfader(float position);
    void setMasterVolume(float volume);
    void setChannelVolume(const std::string& channel, float volume);
    void setChannelEQ(const std::string& channel, const EQSettings& eq);
    void setMasterBPM(float bpm);
    void enableSync(bool enable);
    
    // BPM and beat matching
    void setManualBPM(const std::string& deck, float bpm);
    void beatMatchDecks();
    void nudgeDeck(const std::string& deck, float offset_ms);
    
    // Real-time data access
    DeckState getDeckState(const std::string& deck) const;
    MixerState getMixerState() const;
    AudioLevels getDeckLevels(const std::string& deck) const;
    AudioLevels getMasterLevels() const;
    
    // Configuration
    void setSampleRate(int sr);
    void setBufferSize(size_t bs);
    void setCrossfaderCurve(Crossfader::CurveType curve);
    
    // WebSocket integration for real-time updates
    void setWebSocketCallback(std::function<void(const std::string&)> callback);
    
    // Audio input processing (from live sources)
    void processAudioInput(const AudioBuffer& input_a, const AudioBuffer& input_b);
    AudioBuffer getMasterOutput() const;
    
private:
    DeckState& getDeckRef(const std::string& deck);
    const DeckState& getDeckRef(const std::string& deck) const;
};

// Utility functions for audio processing
namespace AudioUtils {
    
    // Convert dB to linear gain
    float dbToLinear(float db);
    
    // Convert linear gain to dB
    float linearToDb(float linear);
    
    // Apply smooth gain ramping to prevent clicks
    void applyGainRamp(AudioBuffer& buffer, float start_gain, float end_gain);
    
    // Mix two audio buffers
    void mixBuffers(AudioBuffer& dest, const AudioBuffer& src, float gain = 1.0f);
    
    // Apply soft limiter to prevent clipping
    void applySoftLimiter(AudioBuffer& buffer, float threshold = 0.95f);
    
    // Calculate audio correlation for beat matching
    float calculateCorrelation(const AudioBuffer& a, const AudioBuffer& b);
    
    // Generate silence buffer
    AudioBuffer generateSilence(size_t samples, int sample_rate = 48000);
    
    // Convert between audio formats
    std::vector<int16_t> floatToInt16(const AudioBuffer& buffer);
    AudioBuffer int16ToFloat(const std::vector<int16_t>& data, int sample_rate = 48000);
}

} // namespace OneStopRadio

#endif // REALTIME_DJ_PROCESSOR_HPP