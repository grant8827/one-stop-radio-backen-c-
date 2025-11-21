// Example usage of AudioAnalyzer

#include <iostream>
#include <memory>
#include "audio_analyzer.hpp"

using namespace OneStopRadio;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <audio_file>" << std::endl;
        return 1;
    }
    
    std::string audio_file = argv[1];
    std::string output_file = audio_file + ".waveform.json";
    
    // Create analyzer with high-resolution configuration
    AnalysisConfig config;
    config.target_points = 4000;  // High resolution for detailed waveform
    config.enable_frequency_analysis = true;
    config.normalize_amplitude = true;
    config.low_freq_cutoff = 250.0f;   // Bass cutoff
    config.mid_freq_cutoff = 2000.0f;  // Mid-high cutoff
    config.noise_floor = -60.0f;       // -60dB noise floor
    
    AudioAnalyzer analyzer(config);
    
    std::cout << "Analyzing audio file: " << audio_file << std::endl;
    
    // Analyze with progress callback
    auto waveform_data = analyzer.analyze_file(
        audio_file,
        [](float progress) {
            int bar_length = 50;
            int progress_length = static_cast<int>(progress * bar_length);
            
            std::cout << "\rProgress: [";
            for (int i = 0; i < bar_length; ++i) {
                if (i < progress_length) {
                    std::cout << "=";
                } else if (i == progress_length) {
                    std::cout << ">";
                } else {
                    std::cout << " ";
                }
            }
            std::cout << "] " << static_cast<int>(progress * 100) << "%";
            std::cout.flush();
        }
    );
    
    std::cout << std::endl;
    
    if (!waveform_data) {
        std::cerr << "Failed to analyze audio file!" << std::endl;
        return 1;
    }
    
    // Print analysis results
    std::cout << "\nAnalysis Results:" << std::endl;
    std::cout << "Duration: " << waveform_data->duration << " seconds" << std::endl;
    std::cout << "Sample Rate: " << waveform_data->sample_rate << " Hz" << std::endl;
    std::cout << "Channels: " << waveform_data->channels << std::endl;
    std::cout << "Total Samples: " << waveform_data->total_samples << std::endl;
    std::cout << "Waveform Points: " << waveform_data->points.size() << std::endl;
    std::cout << "Global Peak: " << waveform_data->global_peak << std::endl;
    std::cout << "Dynamic Range: " << waveform_data->dynamic_range << " dB" << std::endl;
    std::cout << "Resolution: " << waveform_data->resolution << " seconds per point" << std::endl;
    
    // Export to JSON
    std::string json_output = analyzer.export_to_json(*waveform_data);
    
    std::ofstream output_stream(output_file);
    if (output_stream.is_open()) {
        output_stream << json_output;
        output_stream.close();
        std::cout << "Waveform data exported to: " << output_file << std::endl;
    } else {
        std::cerr << "Failed to write output file: " << output_file << std::endl;
    }
    
    // Also export binary format for faster loading
    std::string binary_file = audio_file + ".waveform.osrwf";
    if (analyzer.export_to_binary(*waveform_data, binary_file)) {
        std::cout << "Binary waveform data exported to: " << binary_file << std::endl;
    } else {
        std::cerr << "Failed to write binary file: " << binary_file << std::endl;
    }
    
    // Show sample waveform points
    std::cout << "\nFirst 5 waveform points:" << std::endl;
    for (size_t i = 0; i < std::min(size_t(5), waveform_data->points.size()); ++i) {
        const auto& point = waveform_data->points[i];
        std::cout << "Point " << i << ": "
                  << "Time=" << point.timestamp << "s, "
                  << "RMS=" << point.amplitude << ", "
                  << "Peak=" << point.peak_amplitude << ", "
                  << "Low=" << point.low_freq << ", "
                  << "Mid=" << point.mid_freq << ", "
                  << "High=" << point.high_freq << std::endl;
    }
    
    return 0;
}