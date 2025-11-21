#include "../include/realtime_dj_processor.hpp"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <json/json.h>

namespace OneStopRadio {

// ThreeBandEQ Implementation
AudioSample ThreeBandEQ::process(const AudioSample& input, const EQSettings& eq) {
    AudioSample output = input;
    
    // Simple shelving EQ implementation
    // Low frequency boost/cut (around 100Hz)
    float low_gain = 1.0f + (eq.low * 0.5f); // ±50% gain
    
    // Mid frequency boost/cut (around 1kHz)  
    float mid_gain = 1.0f + (eq.mid * 0.5f);
    
    // High frequency boost/cut (around 8kHz)
    float high_gain = 1.0f + (eq.high * 0.5f);
    
    // Apply simple frequency weighting (basic approximation)
    output.left = output.left * low_gain * 0.3f + 
                  output.left * mid_gain * 0.4f + 
                  output.left * high_gain * 0.3f;
    
    output.right = output.right * low_gain * 0.3f + 
                   output.right * mid_gain * 0.4f + 
                   output.right * high_gain * 0.3f;
    
    return output;
}

void ThreeBandEQ::reset() {
    left_state = FilterState();
    right_state = FilterState();
}

// BeatDetector Implementation
void BeatDetector::process(const AudioBuffer& buffer) {
    float energy = calculateEnergy(buffer);
    
    analysis.energy_history.push_back(energy);
    
    // Keep only recent history (about 10 seconds worth)
    size_t max_history = (sample_rate / buffer_size) * 10;
    if (analysis.energy_history.size() > max_history) {
        analysis.energy_history.erase(analysis.energy_history.begin());
    }
    
    // Detect onset
    if (detectOnset(energy)) {
        auto now = std::chrono::steady_clock::now();
        float time_seconds = std::chrono::duration<float>(now.time_since_epoch()).count();
        analysis.onset_times.push_back(time_seconds);
        
        // Keep only recent onsets
        if (analysis.onset_times.size() > 32) {
            analysis.onset_times.erase(analysis.onset_times.begin());
        }
        
        // Update BPM estimate
        analysis.current_bpm = estimateBPM();
    }
}

float BeatDetector::calculateEnergy(const AudioBuffer& buffer) {
    float energy = 0.0f;
    for (const auto& sample : buffer.samples) {
        float mono = (sample.left + sample.right) * 0.5f;
        energy += mono * mono;
    }
    return energy / buffer.samples.size();
}

bool BeatDetector::detectOnset(float current_energy) {
    if (analysis.energy_history.size() < 10) return false;
    
    // Simple onset detection: current energy significantly higher than recent average
    float recent_avg = 0.0f;
    size_t start_idx = std::max(0, (int)analysis.energy_history.size() - 10);
    
    for (size_t i = start_idx; i < analysis.energy_history.size() - 1; ++i) {
        recent_avg += analysis.energy_history[i];
    }
    recent_avg /= (analysis.energy_history.size() - 1 - start_idx);
    
    return current_energy > recent_avg * 1.5f; // 50% increase threshold
}

float BeatDetector::estimateBPM() {
    if (analysis.onset_times.size() < 4) return analysis.current_bpm;
    
    std::vector<float> intervals;
    for (size_t i = 1; i < analysis.onset_times.size(); ++i) {
        intervals.push_back(analysis.onset_times[i] - analysis.onset_times[i-1]);
    }
    
    if (intervals.empty()) return analysis.current_bpm;
    
    // Calculate median interval
    std::sort(intervals.begin(), intervals.end());
    float median_interval = intervals[intervals.size() / 2];
    
    // Convert to BPM
    float bpm = 60.0f / median_interval;
    
    // Filter unrealistic BPM values
    if (bpm >= 60.0f && bpm <= 200.0f) {
        return bpm;
    }
    
    return analysis.current_bpm;
}

float BeatDetector::getBeatPosition() const {
    // Simplified beat position calculation
    return std::fmod(std::chrono::duration<float>(
        std::chrono::steady_clock::now().time_since_epoch()).count() * 
        (analysis.current_bpm / 60.0f), 1.0f);
}

void BeatDetector::reset() {
    analysis = BeatAnalysis();
}

// LevelMeter Implementation
void LevelMeter::process(const AudioBuffer& buffer) {
    for (const auto& sample : buffer.samples) {
        // Update peaks
        float abs_left = std::abs(sample.left);
        float abs_right = std::abs(sample.right);
        
        if (abs_left > peak_left) peak_left = abs_left;
        if (abs_right > peak_right) peak_right = abs_right;
        
        // Apply peak decay
        peak_left *= peak_decay_rate;
        peak_right *= peak_decay_rate;
        
        // Update RMS calculation
        rms_buffer_left.push(abs_left * abs_left);
        rms_buffer_right.push(abs_right * abs_right);
        
        rms_sum_left += abs_left * abs_left;
        rms_sum_right += abs_right * abs_right;
        
        // Remove old samples from RMS window
        if (rms_buffer_left.size() > rms_window_size) {
            rms_sum_left -= rms_buffer_left.front();
            rms_sum_right -= rms_buffer_right.front();
            rms_buffer_left.pop();
            rms_buffer_right.pop();
        }
        
        // Calculate RMS
        if (!rms_buffer_left.empty()) {
            rms_left = std::sqrt(rms_sum_left / rms_buffer_left.size());
            rms_right = std::sqrt(rms_sum_right / rms_buffer_right.size());
        }
    }
}

AudioLevels LevelMeter::getLevels() const {
    AudioLevels levels;
    levels.peak_left = peak_left;
    levels.peak_right = peak_right;
    levels.rms_left = rms_left;
    levels.rms_right = rms_right;
    levels.timestamp = std::chrono::steady_clock::now();
    return levels;
}

void LevelMeter::reset() {
    peak_left = peak_right = 0.0f;
    rms_left = rms_right = 0.0f;
    rms_sum_left = rms_sum_right = 0.0f;
    while (!rms_buffer_left.empty()) rms_buffer_left.pop();
    while (!rms_buffer_right.empty()) rms_buffer_right.pop();
}

// Crossfader Implementation  
AudioSample Crossfader::mix(const AudioSample& channel_a, const AudioSample& channel_b, 
                           float crossfader_position) {
    // Normalize position to 0.0-1.0 range (0=A, 1=B)
    float normalized_pos = (crossfader_position + 1.0f) * 0.5f;
    
    float gain_a = applyCurve(1.0f - normalized_pos, 1.0f);
    float gain_b = applyCurve(normalized_pos, 1.0f);
    
    return AudioSample(
        channel_a.left * gain_a + channel_b.left * gain_b,
        channel_a.right * gain_a + channel_b.right * gain_b
    );
}

float Crossfader::applyCurve(float position, float input_gain) {
    switch (curve_type) {
        case LINEAR:
            return position * input_gain;
            
        case SMOOTH:
            return std::sin(position * M_PI * 0.5f) * input_gain;
            
        case SHARP:
            return (position > 0.5f ? 1.0f : 0.0f) * input_gain;
            
        case SCRATCH:
            return (position * position) * input_gain;
            
        default:
            return position * input_gain;
    }
}

// RealtimeDJProcessor Implementation
RealtimeDJProcessor::RealtimeDJProcessor(int sr, size_t bs) 
    : sample_rate(sr), buffer_size(bs),
      deck_a_buffer(bs, sr), deck_b_buffer(bs, sr), master_buffer(bs, sr) {
    
    // Initialize audio processing components
    eq_a = std::make_unique<ThreeBandEQ>(sample_rate);
    eq_b = std::make_unique<ThreeBandEQ>(sample_rate);
    beat_detector_a = std::make_unique<BeatDetector>(sample_rate, buffer_size);
    beat_detector_b = std::make_unique<BeatDetector>(sample_rate, buffer_size);
    level_meter_a = std::make_unique<LevelMeter>();
    level_meter_b = std::make_unique<LevelMeter>();
    master_meter = std::make_unique<LevelMeter>();
    crossfader = std::make_unique<Crossfader>();
    
    std::cout << "🎧 Real-time DJ Processor initialized" << std::endl;
    std::cout << "   Sample Rate: " << sample_rate << " Hz" << std::endl;
    std::cout << "   Buffer Size: " << buffer_size << " samples" << std::endl;
    std::cout << "   Latency: " << (float(buffer_size) / sample_rate * 1000.0f) << " ms" << std::endl;
}

RealtimeDJProcessor::~RealtimeDJProcessor() {
    stop();
}

void RealtimeDJProcessor::start() {
    if (processing_active.load()) return;
    
    processing_active.store(true);
    last_process_time = std::chrono::steady_clock::now();
    
    processing_thread = std::thread(&RealtimeDJProcessor::processingLoop, this);
    
    std::cout << "🎵 Real-time DJ processing started" << std::endl;
}

void RealtimeDJProcessor::stop() {
    processing_active.store(false);
    
    if (processing_thread.joinable()) {
        processing_thread.join();
    }
    
    std::cout << "⏹️ Real-time DJ processing stopped" << std::endl;
}

void RealtimeDJProcessor::processingLoop() {
    while (processing_active.load()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - last_process_time).count();
        
        // Process at consistent intervals based on buffer size
        int target_interval_us = (buffer_size * 1000000) / sample_rate;
        
        if (elapsed >= target_interval_us) {
            processAudioBuffer();
            updateSyncAndBPM();
            sendRealtimeUpdate();
            
            last_process_time = now;
        }
        
        // Small sleep to prevent CPU spinning
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void RealtimeDJProcessor::processAudioBuffer() {
    // Clear output buffer
    master_buffer.clear();
    
    // Process Deck A
    AudioBuffer processed_a = deck_a_buffer;
    if (deck_a.is_playing) {
        // Apply EQ
        for (auto& sample : processed_a.samples) {
            sample = processEQ(sample, deck_a.eq, *eq_a);
        }
        
        // Apply volume
        for (auto& sample : processed_a.samples) {
            sample = sample * deck_a.volume;
        }
        
        // Update levels and beat detection
        level_meter_a->process(processed_a);
        beat_detector_a->process(processed_a);
        
        // Update deck state
        {
            std::lock_guard<std::mutex> lock(deck_a.state_mutex);
            deck_a.levels = level_meter_a->getLevels();
            deck_a.detected_bpm = beat_detector_a->getCurrentBPM();
            deck_a.beat_position = beat_detector_a->getBeatPosition();
        }
    }
    
    // Process Deck B
    AudioBuffer processed_b = deck_b_buffer;
    if (deck_b.is_playing) {
        // Apply EQ
        for (auto& sample : processed_b.samples) {
            sample = processEQ(sample, deck_b.eq, *eq_b);
        }
        
        // Apply volume
        for (auto& sample : processed_b.samples) {
            sample = sample * deck_b.volume;
        }
        
        // Update levels and beat detection
        level_meter_b->process(processed_b);
        beat_detector_b->process(processed_b);
        
        // Update deck state
        {
            std::lock_guard<std::mutex> lock(deck_b.state_mutex);
            deck_b.levels = level_meter_b->getLevels();
            deck_b.detected_bpm = beat_detector_b->getCurrentBPM();
            deck_b.beat_position = beat_detector_b->getBeatPosition();
        }
    }
    
    // Mix channels through crossfader
    for (size_t i = 0; i < master_buffer.size(); ++i) {
        AudioSample deck_a_sample = deck_a.is_playing ? processed_a.samples[i] : AudioSample();
        AudioSample deck_b_sample = deck_b.is_playing ? processed_b.samples[i] : AudioSample();
        
        // Apply channel volumes
        {
            std::lock_guard<std::mutex> lock(mixer.mixer_mutex);
            deck_a_sample = deck_a_sample * mixer.channel_a_volume;
            deck_b_sample = deck_b_sample * mixer.channel_b_volume;
        }
        
        // Crossfade
        master_buffer.samples[i] = crossfader->mix(deck_a_sample, deck_b_sample, 
                                                  mixer.crossfader);
        
        // Apply master volume
        master_buffer.samples[i] = master_buffer.samples[i] * mixer.master_volume;
    }
    
    // Update master levels
    master_meter->process(master_buffer);
    
    {
        std::lock_guard<std::mutex> lock(mixer.mixer_mutex);
        mixer.master_levels = master_meter->getLevels();
    }
    
    // Apply soft limiting to prevent clipping
    AudioUtils::applySoftLimiter(master_buffer);
}

AudioSample RealtimeDJProcessor::processEQ(const AudioSample& input, const EQSettings& eq, 
                                          ThreeBandEQ& eq_filter) {
    return eq_filter.process(input, eq);
}

void RealtimeDJProcessor::updateSyncAndBPM() {
    if (!mixer.sync_enabled) return;
    
    // Simple sync implementation
    float bpm_a = deck_a.detected_bpm > 0 ? deck_a.detected_bpm : deck_a.manual_bpm;
    float bpm_b = deck_b.detected_bpm > 0 ? deck_b.detected_bpm : deck_b.manual_bpm;
    
    if (bpm_a > 0 && bpm_b > 0) {
        float avg_bpm = (bpm_a + bpm_b) * 0.5f;
        
        std::lock_guard<std::mutex> lock(mixer.mixer_mutex);
        mixer.master_bpm = avg_bpm;
    }
}

void RealtimeDJProcessor::sendRealtimeUpdate() {
    if (!websocket_callback) return;
    
    // Create JSON update with current state
    Json::Value update;
    update["type"] = "realtime_update";
    update["timestamp"] = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Deck A state
    {
        std::lock_guard<std::mutex> lock(deck_a.state_mutex);
        update["deck_a"]["playing"] = deck_a.is_playing;
        update["deck_a"]["position"] = deck_a.position;
        update["deck_a"]["bpm"] = deck_a.detected_bpm;
        update["deck_a"]["beat_position"] = deck_a.beat_position;
        update["deck_a"]["levels"]["peak_left"] = deck_a.levels.peak_left;
        update["deck_a"]["levels"]["peak_right"] = deck_a.levels.peak_right;
    }
    
    // Deck B state  
    {
        std::lock_guard<std::mutex> lock(deck_b.state_mutex);
        update["deck_b"]["playing"] = deck_b.is_playing;
        update["deck_b"]["position"] = deck_b.position;
        update["deck_b"]["bpm"] = deck_b.detected_bpm;
        update["deck_b"]["beat_position"] = deck_b.beat_position;
        update["deck_b"]["levels"]["peak_left"] = deck_b.levels.peak_left;
        update["deck_b"]["levels"]["peak_right"] = deck_b.levels.peak_right;
    }
    
    // Mixer state
    {
        std::lock_guard<std::mutex> lock(mixer.mixer_mutex);
        update["mixer"]["crossfader"] = mixer.crossfader;
        update["mixer"]["master_volume"] = mixer.master_volume;
        update["mixer"]["master_bpm"] = mixer.master_bpm;
        update["mixer"]["levels"]["peak_left"] = mixer.master_levels.peak_left;
        update["mixer"]["levels"]["peak_right"] = mixer.master_levels.peak_right;
    }
    
    Json::StreamWriterBuilder builder;
    std::string json_string = Json::writeString(builder, update);
    
    websocket_callback(json_string);
}

// Deck control methods
void RealtimeDJProcessor::loadTrack(const std::string& deck, const std::string& track_id,
                                   const std::string& title, const std::string& artist) {
    DeckState& deck_ref = getDeckRef(deck);
    std::lock_guard<std::mutex> lock(deck_ref.state_mutex);
    
    deck_ref.track_id = track_id;
    deck_ref.track_title = title;
    deck_ref.track_artist = artist;
    deck_ref.position = 0.0f;
    deck_ref.is_playing = false;
    
    std::cout << "🎵 Loaded track on deck " << deck << ": " << title << " - " << artist << std::endl;
}

void RealtimeDJProcessor::playDeck(const std::string& deck) {
    DeckState& deck_ref = getDeckRef(deck);
    std::lock_guard<std::mutex> lock(deck_ref.state_mutex);
    
    deck_ref.is_playing = true;
    std::cout << "▶️ Playing deck " << deck << std::endl;
}

void RealtimeDJProcessor::pauseDeck(const std::string& deck) {
    DeckState& deck_ref = getDeckRef(deck);
    std::lock_guard<std::mutex> lock(deck_ref.state_mutex);
    
    deck_ref.is_playing = false;
    std::cout << "⏸️ Paused deck " << deck << std::endl;
}

void RealtimeDJProcessor::stopDeck(const std::string& deck) {
    DeckState& deck_ref = getDeckRef(deck);
    std::lock_guard<std::mutex> lock(deck_ref.state_mutex);
    
    deck_ref.is_playing = false;
    deck_ref.position = 0.0f;
    std::cout << "⏹️ Stopped deck " << deck << std::endl;
}

void RealtimeDJProcessor::setCrossfader(float position) {
    std::lock_guard<std::mutex> lock(mixer.mixer_mutex);
    mixer.crossfader = std::clamp(position, -1.0f, 1.0f);
}

void RealtimeDJProcessor::setMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(mixer.mixer_mutex);
    mixer.master_volume = std::clamp(volume, 0.0f, 1.0f);
}

DeckState& RealtimeDJProcessor::getDeckRef(const std::string& deck) {
    return (deck == "A" || deck == "a") ? deck_a : deck_b;
}

const DeckState& RealtimeDJProcessor::getDeckRef(const std::string& deck) const {
    return (deck == "A" || deck == "a") ? deck_a : deck_b;
}

void RealtimeDJProcessor::setWebSocketCallback(std::function<void(const std::string&)> callback) {
    websocket_callback = callback;
}

// AudioUtils Implementation
namespace AudioUtils {
    
    float dbToLinear(float db) {
        return std::pow(10.0f, db / 20.0f);
    }
    
    float linearToDb(float linear) {
        return 20.0f * std::log10(linear);
    }
    
    void applyGainRamp(AudioBuffer& buffer, float start_gain, float end_gain) {
        size_t samples = buffer.samples.size();
        for (size_t i = 0; i < samples; ++i) {
            float gain = start_gain + (end_gain - start_gain) * (float(i) / samples);
            buffer.samples[i] = buffer.samples[i] * gain;
        }
    }
    
    void mixBuffers(AudioBuffer& dest, const AudioBuffer& src, float gain) {
        size_t min_size = std::min(dest.samples.size(), src.samples.size());
        for (size_t i = 0; i < min_size; ++i) {
            dest.samples[i] += src.samples[i] * gain;
        }
    }
    
    void applySoftLimiter(AudioBuffer& buffer, float threshold) {
        for (auto& sample : buffer.samples) {
            // Soft clipping using tanh
            if (std::abs(sample.left) > threshold) {
                sample.left = std::tanh(sample.left) * threshold;
            }
            if (std::abs(sample.right) > threshold) {
                sample.right = std::tanh(sample.right) * threshold;
            }
        }
    }
    
    AudioBuffer generateSilence(size_t samples, int sample_rate) {
        return AudioBuffer(samples, sample_rate);
    }
    
} // namespace AudioUtils

} // namespace OneStopRadio