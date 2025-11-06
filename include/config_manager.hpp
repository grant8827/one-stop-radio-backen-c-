#pragma once
#include <string>
#include <vector>
#include "stream_manager.hpp"

class ConfigManager {
public:
    ConfigManager() = default;
    
    bool load(const std::string& config_file);
    
    // Getters
    int get_http_port() const { return http_port_; }
    int get_webrtc_port() const { return webrtc_port_; }
    std::string get_log_level() const { return log_level_; }
    std::vector<StreamConfig> get_stream_config() const { return stream_configs_; }
    
private:
    int http_port_ = 8080;
    int webrtc_port_ = 9090;
    std::string log_level_ = "info";
    std::vector<StreamConfig> stream_configs_;
};