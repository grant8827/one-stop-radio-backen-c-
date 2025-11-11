#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <shout/shout.h>

namespace onestopradio {

enum class StreamStatus {
    PENDING = 0,
    READY = 1,
    ACTIVE = 2,
    INACTIVE = 3,
    ERROR = 4,
    SUSPENDED = 5,
    DELETED = 6
};

enum class StreamQuality {
    LOW = 64,       // 64 kbps - Talk radio
    STANDARD = 128, // 128 kbps - Standard music
    HIGH = 192,     // 192 kbps - High quality
    PREMIUM = 320   // 320 kbps - Premium quality
};

struct StreamConfig {
    std::string stream_id;
    std::string user_id;
    std::string mount_point;
    std::string source_password;
    std::string station_name;
    std::string description;
    std::string genre;
    StreamQuality quality;
    int max_listeners;
    std::string server_host;
    int server_port;
    std::string protocol;       // "icecast" or "shoutcast"
    std::string format;         // "MP3", "OGG", "AAC"
    bool public_stream;
    std::map<std::string, std::string> metadata;
};

struct StreamStats {
    std::string stream_id;
    StreamStatus status;
    bool is_connected;
    int current_listeners;
    int peak_listeners;
    int64_t bytes_sent;
    double uptime_seconds;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point last_update;
    std::string current_song;
    std::string error_message;
};

struct IcecastConfigData {
    std::string config_path;
    std::string log_dir;
    std::string admin_password;
    std::string source_password;
    int port;
    int max_clients;
    int max_sources;
    std::vector<StreamConfig> mount_points;
};

class StreamController {
public:
    StreamController();
    ~StreamController();

    // Service lifecycle
    bool Initialize(const std::string& config_file);
    void Shutdown();

    // Stream management
    bool CreateMountPoint(const StreamConfig& config);
    bool ActivateStream(const std::string& stream_id);
    bool DeactivateStream(const std::string& stream_id);
    bool DeleteMountPoint(const std::string& stream_id);
    bool UpdateStreamConfig(const std::string& stream_id, const StreamConfig& config);

    // Status and monitoring
    StreamStats GetStreamStatus(const std::string& stream_id);
    std::vector<StreamStats> GetAllStreamStats();
    bool IsStreamActive(const std::string& stream_id);

    // Configuration management
    bool ReloadServerConfig();
    bool ValidateConfig(const StreamConfig& config);
    std::string GenerateIcecastConfig(const IcecastConfigData& data);

    // Metadata management
    bool UpdateMetadata(const std::string& stream_id, const std::string& title, const std::string& artist = "");
    bool SetStreamTitle(const std::string& stream_id, const std::string& title);

    // Health checks
    bool IsHealthy() const;
    std::string GetHealthStatus() const;

    // Audio data handling (for future integration)
    bool SendAudioData(const std::string& stream_id, const uint8_t* data, size_t size);

private:
    struct Stream {
        StreamConfig config;
        StreamStatus status;
        shout_t* shout_connection;
        std::chrono::system_clock::time_point start_time;
        int64_t bytes_sent;
        int current_listeners;
        int peak_listeners;
        std::string error_message;
        std::mutex stream_mutex;
    };

    // Internal methods
    bool InitializeShout();
    bool CreateShoutConnection(const std::string& stream_id);
    void CloseShoutConnection(const std::string& stream_id);
    bool WriteIcecastConfig(const IcecastConfigData& data);
    bool ReloadIcecastServer();
    void UpdateStreamStats(const std::string& stream_id);
    std::string StatusToString(StreamStatus status);
    StreamStatus StringToStatus(const std::string& status);

    // Member variables
    std::map<std::string, std::unique_ptr<Stream>> streams_;
    mutable std::mutex streams_mutex_;
    
    std::string config_file_path_;
    std::string icecast_config_path_;
    std::string icecast_binary_path_;
    std::string log_directory_;
    
    bool initialized_;
    bool running_;
    
    // Configuration
    IcecastConfigData server_config_;
};

} // namespace onestopradio