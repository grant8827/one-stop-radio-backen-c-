#include "config_manager.hpp"
#include <fstream>
#include <iostream>

ConfigManager::ConfigManager() {
    // Set default configuration values
    config_["server"] = {
        {"http_port", 8080},
        {"webrtc_port", 8081},
        {"host", "0.0.0.0"},
        {"max_connections", 100}
    };
    
    config_["audio"] = {
        {"sample_rate", 44100},
        {"channels", 2},
        {"bitrate", 128000},
        {"buffer_size", 1024}
    };
    
    config_["streaming"] = {
        {"max_streams", 10},
        {"default_format", "mp3"},
        {"reconnect_attempts", 3},
        {"reconnect_delay", 5000}
    };
    
    config_["logging"] = {
        {"level", "info"},
        {"file", "radio_server.log"},
        {"max_size", 10485760}, // 10MB
        {"rotate", true}
    };
}

bool ConfigManager::load_from_file(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open config file: " << filename 
                     << ". Using default configuration." << std::endl;
            return false;
        }
        
        file >> config_;
        file.close();
        
        std::cout << "Configuration loaded from: " << filename << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config file: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::save_to_file(const std::string& filename) const {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Could not create config file: " << filename << std::endl;
            return false;
        }
        
        file << config_.dump(4); // Pretty print with 4-space indentation
        file.close();
        
        std::cout << "Configuration saved to: " << filename << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving config file: " << e.what() << std::endl;
        return false;
    }
}

json ConfigManager::get_section(const std::string& section) const {
    auto it = config_.find(section);
    if (it != config_.end()) {
        return *it;
    }
    return json::object(); // Return empty object if section not found
}

void ConfigManager::set_section(const std::string& section, const json& data) {
    config_[section] = data;
}

bool ConfigManager::get_bool(const std::string& section, const std::string& key, bool default_value) const {
    try {
        if (config_.contains(section) && config_[section].contains(key)) {
            return config_[section][key].get<bool>();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading bool config " << section << "." << key 
                 << ": " << e.what() << std::endl;
    }
    return default_value;
}

int ConfigManager::get_int(const std::string& section, const std::string& key, int default_value) const {
    try {
        if (config_.contains(section) && config_[section].contains(key)) {
            return config_[section][key].get<int>();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading int config " << section << "." << key 
                 << ": " << e.what() << std::endl;
    }
    return default_value;
}

std::string ConfigManager::get_string(const std::string& section, const std::string& key, 
                                    const std::string& default_value) const {
    try {
        if (config_.contains(section) && config_[section].contains(key)) {
            return config_[section][key].get<std::string>();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading string config " << section << "." << key 
                 << ": " << e.what() << std::endl;
    }
    return default_value;
}

void ConfigManager::set_bool(const std::string& section, const std::string& key, bool value) {
    config_[section][key] = value;
}

void ConfigManager::set_int(const std::string& section, const std::string& key, int value) {
    config_[section][key] = value;
}

void ConfigManager::set_string(const std::string& section, const std::string& key, const std::string& value) {
    config_[section][key] = value;
}

json ConfigManager::get_full_config() const {
    return config_;
}

void ConfigManager::print_config() const {
    std::cout << "Current Configuration:" << std::endl;
    std::cout << config_.dump(4) << std::endl;
}

bool ConfigManager::validate_config() const {
    bool valid = true;
    
    // Validate required sections
    std::vector<std::string> required_sections = {"server", "audio", "streaming", "logging"};
    for (const auto& section : required_sections) {
        if (!config_.contains(section)) {
            std::cerr << "Missing required config section: " << section << std::endl;
            valid = false;
        }
    }
    
    // Validate server configuration
    if (config_.contains("server")) {
        const auto& server = config_["server"];
        
        int http_port = server.value("http_port", 8080);
        int webrtc_port = server.value("webrtc_port", 8081);
        
        if (http_port <= 0 || http_port > 65535) {
            std::cerr << "Invalid HTTP port: " << http_port << std::endl;
            valid = false;
        }
        
        if (webrtc_port <= 0 || webrtc_port > 65535) {
            std::cerr << "Invalid WebRTC port: " << webrtc_port << std::endl;
            valid = false;
        }
        
        if (http_port == webrtc_port) {
            std::cerr << "HTTP and WebRTC ports cannot be the same" << std::endl;
            valid = false;
        }
    }
    
    // Validate audio configuration
    if (config_.contains("audio")) {
        const auto& audio = config_["audio"];
        
        int sample_rate = audio.value("sample_rate", 44100);
        int channels = audio.value("channels", 2);
        int bitrate = audio.value("bitrate", 128000);
        
        if (sample_rate != 44100 && sample_rate != 48000 && sample_rate != 22050) {
            std::cerr << "Unsupported sample rate: " << sample_rate << std::endl;
            valid = false;
        }
        
        if (channels != 1 && channels != 2) {
            std::cerr << "Unsupported channel count: " << channels << std::endl;
            valid = false;
        }
        
        if (bitrate < 32000 || bitrate > 320000) {
            std::cerr << "Invalid bitrate: " << bitrate << std::endl;
            valid = false;
        }
    }
    
    return valid;
}