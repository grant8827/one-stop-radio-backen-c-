#pragma once
#include <string>
#include <vector>
#include <memory>

struct StreamConfig {
    std::string server_type;     // "icecast" or "shoutcast"
    std::string host;
    int port;
    std::string username;
    std::string password;
    std::string mountpoint;
    int bitrate;
    std::string format;          // "MP3", "OGG", "AAC"
    int sample_rate;
    int channels;
};

struct StreamStats {
    bool is_connected;
    int listeners;
    int64_t bytes_sent;
    double uptime;
    std::string status;
};

class StreamManager {
public:
    StreamManager();
    ~StreamManager();
    
    bool initialize(const std::vector<StreamConfig>& configs);
    bool start_stream(const std::string& stream_id, const StreamConfig& config);
    bool stop_stream(const std::string& stream_id);
    void stop_all_streams();
    
    StreamStats get_stream_stats(const std::string& stream_id);
    std::vector<std::string> get_active_streams();
    
    // Audio data input
    void send_audio_data(const uint8_t* data, size_t size);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};