#pragma once

#include <vector>
#include <string>
#include <memory>
#include <complex>
#include <fftw3.h>
#include <sndfile.h>

namespace OneStopRadio {

/**
 * Waveform data point containing amplitude and frequency information
 */
struct WaveformPoint {
    float amplitude;          // RMS amplitude (0.0 - 1.0)
    float peak_amplitude;     // Peak amplitude (0.0 - 1.0)
    float frequency_energy;   // Dominant frequency energy
    float low_freq;          // Low frequency band energy (0-250Hz)
    float mid_freq;          // Mid frequency band energy (250Hz-4kHz)
    float high_freq;         // High frequency band energy (4kHz+)
    double timestamp;        // Time position in seconds
    uint32_t sample_index;   // Corresponding sample index
};

/**
 * Complete waveform analysis data
 */
struct WaveformData {
    std::vector<WaveformPoint> points;
    double duration;         // Track duration in seconds
    uint32_t sample_rate;    // Audio sample rate
    uint32_t channels;       // Number of audio channels
    uint32_t total_samples;  // Total samples in track
    float global_peak;       // Global peak amplitude
    float dynamic_range;     // Dynamic range (dB)
    std::string file_path;   // Source file path
    uint64_t file_size;      // File size in bytes
    
    // Analysis parameters
    uint32_t window_size;    // Analysis window size (samples)
    uint32_t hop_size;       // Hop size between windows
    double resolution;       // Time resolution per point (seconds)
};

/**
 * Audio analysis configuration
 */
struct AnalysisConfig {
    uint32_t target_points = 2048;      // Target number of waveform points
    uint32_t min_window_size = 512;     // Minimum FFT window size
    uint32_t max_window_size = 8192;    // Maximum FFT window size
    bool enable_frequency_analysis = true;
    bool normalize_amplitude = true;
    float noise_floor = -60.0f;         // Noise floor in dB
    
    // Frequency band definitions (Hz)
    float low_freq_cutoff = 250.0f;
    float mid_freq_cutoff = 4000.0f;
};

/**
 * High-performance offline audio analyzer
 */
class AudioAnalyzer {
public:
    explicit AudioAnalyzer(const AnalysisConfig& config = AnalysisConfig{});
    ~AudioAnalyzer();
    
    /**
     * Analyze audio file and generate waveform data
     * @param file_path Path to audio file (supports WAV, FLAC, MP3, etc.)
     * @param progress_callback Optional callback for progress updates (0.0 - 1.0)
     * @return Waveform analysis data or nullptr on failure
     */
    std::unique_ptr<WaveformData> analyze_file(
        const std::string& file_path,
        std::function<void(float)> progress_callback = nullptr
    );
    
    /**
     * Analyze raw audio samples
     */
    std::unique_ptr<WaveformData> analyze_samples(
        const float* samples,
        uint32_t num_samples,
        uint32_t sample_rate,
        uint32_t channels = 1,
        std::function<void(float)> progress_callback = nullptr
    );
    
    /**
     * Export waveform data to JSON format
     */
    std::string export_to_json(const WaveformData& waveform) const;
    
    /**
     * Export waveform data to binary format (more compact)
     */
    bool export_to_binary(const WaveformData& waveform, const std::string& output_path) const;
    
    /**
     * Load waveform data from binary format
     */
    std::unique_ptr<WaveformData> load_from_binary(const std::string& file_path) const;

private:
    AnalysisConfig config_;
    
    // FFTW resources
    float* fft_input_;
    fftwf_complex* fft_output_;
    fftwf_plan fft_plan_;
    uint32_t fft_size_;
    
    // Window function for FFT
    std::vector<float> window_;
    
    /**
     * Initialize FFTW resources
     */
    void initialize_fft(uint32_t window_size);
    
    /**
     * Cleanup FFTW resources
     */
    void cleanup_fft();
    
    /**
     * Generate Hanning window function
     */
    void generate_window(uint32_t size);
    
    /**
     * Calculate optimal window size based on track length and target points
     */
    uint32_t calculate_window_size(uint32_t total_samples, uint32_t target_points) const;
    
    /**
     * Process audio window and extract features
     */
    WaveformPoint process_window(
        const float* samples,
        uint32_t window_size,
        uint32_t sample_rate,
        double timestamp,
        uint32_t sample_index
    );
    
    /**
     * Calculate RMS amplitude
     */
    float calculate_rms(const float* samples, uint32_t size) const;
    
    /**
     * Calculate peak amplitude
     */
    float calculate_peak(const float* samples, uint32_t size) const;
    
    /**
     * Perform frequency analysis using FFT
     */
    void analyze_frequency_content(
        const float* samples,
        uint32_t size,
        uint32_t sample_rate,
        float& low_freq,
        float& mid_freq,
        float& high_freq,
        float& dominant_freq_energy
    );
    
    /**
     * Convert amplitude to dB
     */
    float amplitude_to_db(float amplitude) const;
    
    /**
     * Convert dB to amplitude
     */
    float db_to_amplitude(float db) const;
    
    /**
     * Normalize waveform data
     */
    void normalize_waveform(WaveformData& waveform) const;
    
    /**
     * Calculate dynamic range
     */
    float calculate_dynamic_range(const WaveformData& waveform) const;
};

/**
 * Utility function to get supported audio file formats
 */
std::vector<std::string> get_supported_formats();

/**
 * Utility function to validate audio file
 */
bool is_valid_audio_file(const std::string& file_path);

} // namespace OneStopRadio