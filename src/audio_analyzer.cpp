#include "audio_analyzer.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <json/json.h>

namespace OneStopRadio {

AudioAnalyzer::AudioAnalyzer(const AnalysisConfig& config)
    : config_(config)
    , fft_input_(nullptr)
    , fft_output_(nullptr)
    , fft_plan_(nullptr)
    , fft_size_(0)
{
    // Initialize with default window size
    initialize_fft(config_.min_window_size);
}

AudioAnalyzer::~AudioAnalyzer() {
    cleanup_fft();
}

void AudioAnalyzer::initialize_fft(uint32_t window_size) {
    if (fft_size_ == window_size && fft_input_ != nullptr) {
        return; // Already initialized with correct size
    }
    
    cleanup_fft();
    
    fft_size_ = window_size;
    fft_input_ = fftwf_alloc_real(fft_size_);
    fft_output_ = fftwf_alloc_complex(fft_size_ / 2 + 1);
    
    fft_plan_ = fftwf_plan_dft_r2c_1d(
        fft_size_,
        fft_input_,
        fft_output_,
        FFTW_ESTIMATE
    );
    
    generate_window(fft_size_);
}

void AudioAnalyzer::cleanup_fft() {
    if (fft_plan_) {
        fftwf_destroy_plan(fft_plan_);
        fft_plan_ = nullptr;
    }
    
    if (fft_input_) {
        fftwf_free(fft_input_);
        fft_input_ = nullptr;
    }
    
    if (fft_output_) {
        fftwf_free(fft_output_);
        fft_output_ = nullptr;
    }
    
    fft_size_ = 0;
}

void AudioAnalyzer::generate_window(uint32_t size) {
    window_.resize(size);
    
    // Generate Hanning window
    for (uint32_t i = 0; i < size; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (size - 1)));
    }
}

uint32_t AudioAnalyzer::calculate_window_size(uint32_t total_samples, uint32_t target_points) const {
    // Calculate window size to achieve target number of points
    uint32_t hop_size = total_samples / target_points;
    
    // Window size should be at least 2x hop size for good overlap
    uint32_t window_size = hop_size * 2;
    
    // Clamp to valid range
    window_size = std::max(window_size, config_.min_window_size);
    window_size = std::min(window_size, config_.max_window_size);
    
    // Round to nearest power of 2 for FFT efficiency
    uint32_t power_of_2 = 1;
    while (power_of_2 < window_size) {
        power_of_2 <<= 1;
    }
    
    return power_of_2;
}

std::unique_ptr<WaveformData> AudioAnalyzer::analyze_file(
    const std::string& file_path,
    std::function<void(float)> progress_callback
) {
    // Open audio file using libsndfile
    SF_INFO sf_info;
    std::memset(&sf_info, 0, sizeof(sf_info));
    
    SNDFILE* sf_file = sf_open(file_path.c_str(), SFM_READ, &sf_info);
    if (!sf_file) {
        std::cerr << "Failed to open audio file: " << file_path << std::endl;
        std::cerr << "libsndfile error: " << sf_strerror(nullptr) << std::endl;
        return nullptr;
    }
    
    // Read entire file into memory (for offline analysis)
    std::vector<float> samples(sf_info.frames * sf_info.channels);
    sf_count_t frames_read = sf_readf_float(sf_file, samples.data(), sf_info.frames);
    sf_close(sf_file);
    
    if (frames_read != sf_info.frames) {
        std::cerr << "Warning: Only read " << frames_read << " of " << sf_info.frames << " frames" << std::endl;
    }
    
    // Convert to mono by averaging channels
    std::vector<float> mono_samples;
    if (sf_info.channels == 1) {
        mono_samples = std::move(samples);
    } else {
        mono_samples.resize(sf_info.frames);
        for (sf_count_t i = 0; i < sf_info.frames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < sf_info.channels; ++ch) {
                sum += samples[i * sf_info.channels + ch];
            }
            mono_samples[i] = sum / sf_info.channels;
        }
    }
    
    // Analyze the mono samples
    auto result = analyze_samples(
        mono_samples.data(),
        mono_samples.size(),
        sf_info.samplerate,
        1,
        progress_callback
    );
    
    if (result) {
        result->file_path = file_path;
        
        // Get file size
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (file.good()) {
            result->file_size = file.tellg();
        }
    }
    
    return result;
}

std::unique_ptr<WaveformData> AudioAnalyzer::analyze_samples(
    const float* samples,
    uint32_t num_samples,
    uint32_t sample_rate,
    uint32_t channels,
    std::function<void(float)> progress_callback
) {
    if (!samples || num_samples == 0 || sample_rate == 0) {
        return nullptr;
    }
    
    auto waveform = std::make_unique<WaveformData>();
    waveform->duration = static_cast<double>(num_samples) / sample_rate;
    waveform->sample_rate = sample_rate;
    waveform->channels = channels;
    waveform->total_samples = num_samples;
    
    // Calculate optimal window and hop sizes
    uint32_t window_size = calculate_window_size(num_samples, config_.target_points);
    uint32_t hop_size = window_size / 4; // 75% overlap
    
    waveform->window_size = window_size;
    waveform->hop_size = hop_size;
    waveform->resolution = static_cast<double>(hop_size) / sample_rate;
    
    // Initialize FFT with calculated window size
    initialize_fft(window_size);
    
    // Reserve space for waveform points
    uint32_t estimated_points = (num_samples - window_size) / hop_size + 1;
    waveform->points.reserve(estimated_points);
    
    // Process audio in overlapping windows
    float global_peak = 0.0f;
    uint32_t processed_samples = 0;
    
    for (uint32_t i = 0; i + window_size <= num_samples; i += hop_size) {
        // Calculate timestamp and sample index
        double timestamp = static_cast<double>(i) / sample_rate;
        
        // Process this window
        WaveformPoint point = process_window(
            &samples[i],
            window_size,
            sample_rate,
            timestamp,
            i
        );
        
        waveform->points.push_back(point);
        
        // Track global peak
        global_peak = std::max(global_peak, point.peak_amplitude);
        
        // Update progress
        processed_samples = i + window_size;
        if (progress_callback && (waveform->points.size() % 100) == 0) {
            float progress = static_cast<float>(processed_samples) / num_samples;
            progress_callback(std::min(progress, 1.0f));
        }
    }
    
    waveform->global_peak = global_peak;
    
    // Normalize waveform if requested
    if (config_.normalize_amplitude) {
        normalize_waveform(*waveform);
    }
    
    // Calculate dynamic range
    waveform->dynamic_range = calculate_dynamic_range(*waveform);
    
    // Final progress update
    if (progress_callback) {
        progress_callback(1.0f);
    }
    
    return waveform;
}

WaveformPoint AudioAnalyzer::process_window(
    const float* samples,
    uint32_t window_size,
    uint32_t sample_rate,
    double timestamp,
    uint32_t sample_index
) {
    WaveformPoint point;
    point.timestamp = timestamp;
    point.sample_index = sample_index;
    
    // Calculate amplitude metrics
    point.amplitude = calculate_rms(samples, window_size);
    point.peak_amplitude = calculate_peak(samples, window_size);
    
    // Perform frequency analysis if enabled
    if (config_.enable_frequency_analysis) {
        analyze_frequency_content(
            samples,
            window_size,
            sample_rate,
            point.low_freq,
            point.mid_freq,
            point.high_freq,
            point.frequency_energy
        );
    } else {
        point.low_freq = 0.0f;
        point.mid_freq = 0.0f;
        point.high_freq = 0.0f;
        point.frequency_energy = 0.0f;
    }
    
    return point;
}

float AudioAnalyzer::calculate_rms(const float* samples, uint32_t size) const {
    double sum_squares = 0.0;
    
    for (uint32_t i = 0; i < size; ++i) {
        double sample = static_cast<double>(samples[i]);
        sum_squares += sample * sample;
    }
    
    return static_cast<float>(std::sqrt(sum_squares / size));
}

float AudioAnalyzer::calculate_peak(const float* samples, uint32_t size) const {
    float peak = 0.0f;
    
    for (uint32_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(samples[i]));
    }
    
    return peak;
}

void AudioAnalyzer::analyze_frequency_content(
    const float* samples,
    uint32_t size,
    uint32_t sample_rate,
    float& low_freq,
    float& mid_freq,
    float& high_freq,
    float& dominant_freq_energy
) {
    // Apply window function and copy to FFT input
    for (uint32_t i = 0; i < size; ++i) {
        fft_input_[i] = samples[i] * window_[i];
    }
    
    // Perform FFT
    fftwf_execute(fft_plan_);
    
    // Calculate frequency bin size
    float bin_size = static_cast<float>(sample_rate) / size;
    uint32_t num_bins = size / 2 + 1;
    
    // Calculate frequency band boundaries
    uint32_t low_bin = static_cast<uint32_t>(config_.low_freq_cutoff / bin_size);
    uint32_t mid_bin = static_cast<uint32_t>(config_.mid_freq_cutoff / bin_size);
    
    low_bin = std::min(low_bin, num_bins - 1);
    mid_bin = std::min(mid_bin, num_bins - 1);
    
    // Calculate energy in each frequency band
    double low_energy = 0.0, mid_energy = 0.0, high_energy = 0.0;
    double total_energy = 0.0;
    double max_bin_energy = 0.0;
    
    for (uint32_t i = 1; i < num_bins; ++i) { // Skip DC component
        double real = fft_output_[i][0];
        double imag = fft_output_[i][1];
        double magnitude = std::sqrt(real * real + imag * imag);
        double energy = magnitude * magnitude;
        
        total_energy += energy;
        max_bin_energy = std::max(max_bin_energy, energy);
        
        if (i < low_bin) {
            low_energy += energy;
        } else if (i < mid_bin) {
            mid_energy += energy;
        } else {
            high_energy += energy;
        }
    }
    
    // Normalize energies
    if (total_energy > 0.0) {
        low_freq = static_cast<float>(low_energy / total_energy);
        mid_freq = static_cast<float>(mid_energy / total_energy);
        high_freq = static_cast<float>(high_energy / total_energy);
        dominant_freq_energy = static_cast<float>(max_bin_energy / total_energy);
    } else {
        low_freq = mid_freq = high_freq = dominant_freq_energy = 0.0f;
    }
}

void AudioAnalyzer::normalize_waveform(WaveformData& waveform) const {
    if (waveform.points.empty() || waveform.global_peak <= 0.0f) {
        return;
    }
    
    float scale_factor = 1.0f / waveform.global_peak;
    
    for (auto& point : waveform.points) {
        point.amplitude *= scale_factor;
        point.peak_amplitude *= scale_factor;
    }
    
    waveform.global_peak = 1.0f;
}

float AudioAnalyzer::calculate_dynamic_range(const WaveformData& waveform) const {
    if (waveform.points.empty()) {
        return 0.0f;
    }
    
    // Find minimum and maximum RMS values
    float min_rms = std::numeric_limits<float>::max();
    float max_rms = 0.0f;
    
    for (const auto& point : waveform.points) {
        if (point.amplitude > 0.0f) {
            min_rms = std::min(min_rms, point.amplitude);
            max_rms = std::max(max_rms, point.amplitude);
        }
    }
    
    if (min_rms == std::numeric_limits<float>::max() || max_rms <= 0.0f) {
        return 0.0f;
    }
    
    // Convert to dB
    float max_db = amplitude_to_db(max_rms);
    float min_db = amplitude_to_db(min_rms);
    
    return max_db - min_db;
}

float AudioAnalyzer::amplitude_to_db(float amplitude) const {
    if (amplitude <= 0.0f) {
        return config_.noise_floor;
    }
    return 20.0f * std::log10(amplitude);
}

float AudioAnalyzer::db_to_amplitude(float db) const {
    return std::pow(10.0f, db / 20.0f);
}

std::string AudioAnalyzer::export_to_json(const WaveformData& waveform) const {
    Json::Value root;
    Json::Value metadata;
    Json::Value points(Json::arrayValue);
    
    // Metadata
    metadata["duration"] = waveform.duration;
    metadata["sample_rate"] = waveform.sample_rate;
    metadata["channels"] = waveform.channels;
    metadata["total_samples"] = waveform.total_samples;
    metadata["global_peak"] = waveform.global_peak;
    metadata["dynamic_range"] = waveform.dynamic_range;
    metadata["file_path"] = waveform.file_path;
    metadata["file_size"] = static_cast<Json::UInt64>(waveform.file_size);
    metadata["window_size"] = waveform.window_size;
    metadata["hop_size"] = waveform.hop_size;
    metadata["resolution"] = waveform.resolution;
    metadata["num_points"] = static_cast<Json::UInt>(waveform.points.size());
    
    // Waveform points
    for (const auto& point : waveform.points) {
        Json::Value json_point;
        json_point["amp"] = point.amplitude;
        json_point["peak"] = point.peak_amplitude;
        json_point["freq"] = point.frequency_energy;
        json_point["low"] = point.low_freq;
        json_point["mid"] = point.mid_freq;
        json_point["high"] = point.high_freq;
        json_point["time"] = point.timestamp;
        json_point["sample"] = point.sample_index;
        
        points.append(json_point);
    }
    
    root["metadata"] = metadata;
    root["waveform"] = points;
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

bool AudioAnalyzer::export_to_binary(const WaveformData& waveform, const std::string& output_path) const {
    std::ofstream file(output_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Write header
    file.write("OSRWF", 5); // OneStopRadio Waveform File
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write metadata
    file.write(reinterpret_cast<const char*>(&waveform.duration), sizeof(waveform.duration));
    file.write(reinterpret_cast<const char*>(&waveform.sample_rate), sizeof(waveform.sample_rate));
    file.write(reinterpret_cast<const char*>(&waveform.channels), sizeof(waveform.channels));
    file.write(reinterpret_cast<const char*>(&waveform.total_samples), sizeof(waveform.total_samples));
    file.write(reinterpret_cast<const char*>(&waveform.global_peak), sizeof(waveform.global_peak));
    file.write(reinterpret_cast<const char*>(&waveform.dynamic_range), sizeof(waveform.dynamic_range));
    file.write(reinterpret_cast<const char*>(&waveform.window_size), sizeof(waveform.window_size));
    file.write(reinterpret_cast<const char*>(&waveform.hop_size), sizeof(waveform.hop_size));
    file.write(reinterpret_cast<const char*>(&waveform.resolution), sizeof(waveform.resolution));
    
    // Write file path
    uint32_t path_length = waveform.file_path.length();
    file.write(reinterpret_cast<const char*>(&path_length), sizeof(path_length));
    file.write(waveform.file_path.data(), path_length);
    
    // Write waveform points
    uint32_t num_points = waveform.points.size();
    file.write(reinterpret_cast<const char*>(&num_points), sizeof(num_points));
    
    for (const auto& point : waveform.points) {
        file.write(reinterpret_cast<const char*>(&point), sizeof(WaveformPoint));
    }
    
    return file.good();
}

std::unique_ptr<WaveformData> AudioAnalyzer::load_from_binary(const std::string& file_path) const {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return nullptr;
    }
    
    // Verify header
    char header[6] = {0};
    file.read(header, 5);
    if (std::string(header) != "OSRWF") {
        return nullptr;
    }
    
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        return nullptr;
    }
    
    auto waveform = std::make_unique<WaveformData>();
    
    // Read metadata
    file.read(reinterpret_cast<char*>(&waveform->duration), sizeof(waveform->duration));
    file.read(reinterpret_cast<char*>(&waveform->sample_rate), sizeof(waveform->sample_rate));
    file.read(reinterpret_cast<char*>(&waveform->channels), sizeof(waveform->channels));
    file.read(reinterpret_cast<char*>(&waveform->total_samples), sizeof(waveform->total_samples));
    file.read(reinterpret_cast<char*>(&waveform->global_peak), sizeof(waveform->global_peak));
    file.read(reinterpret_cast<char*>(&waveform->dynamic_range), sizeof(waveform->dynamic_range));
    file.read(reinterpret_cast<char*>(&waveform->window_size), sizeof(waveform->window_size));
    file.read(reinterpret_cast<char*>(&waveform->hop_size), sizeof(waveform->hop_size));
    file.read(reinterpret_cast<char*>(&waveform->resolution), sizeof(waveform->resolution));
    
    // Read file path
    uint32_t path_length;
    file.read(reinterpret_cast<char*>(&path_length), sizeof(path_length));
    waveform->file_path.resize(path_length);
    file.read(waveform->file_path.data(), path_length);
    
    // Read waveform points
    uint32_t num_points;
    file.read(reinterpret_cast<char*>(&num_points), sizeof(num_points));
    waveform->points.resize(num_points);
    
    for (auto& point : waveform->points) {
        file.read(reinterpret_cast<char*>(&point), sizeof(WaveformPoint));
    }
    
    return file.good() ? std::move(waveform) : nullptr;
}

std::vector<std::string> get_supported_formats() {
    return {
        ".wav", ".flac", ".ogg", ".mp3", ".aac", ".m4a",
        ".aiff", ".au", ".raw", ".caf", ".wv", ".opus"
    };
}

bool is_valid_audio_file(const std::string& file_path) {
    SF_INFO sf_info;
    std::memset(&sf_info, 0, sizeof(sf_info));
    
    SNDFILE* sf_file = sf_open(file_path.c_str(), SFM_READ, &sf_info);
    if (!sf_file) {
        return false;
    }
    
    sf_close(sf_file);
    return sf_info.frames > 0 && sf_info.samplerate > 0;
}

} // namespace OneStopRadio