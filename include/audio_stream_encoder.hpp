#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

extern "C" {
#include <shout/shout.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

/**
 * Streaming protocol types
 */
enum class StreamProtocol {
    ICECAST2,
    SHOUTCAST,
    HTTP,
    RTMP
};

/**
 * Audio codec types for streaming
 */
enum class StreamCodec {
    MP3,
    OGG_VORBIS,
    OGG_OPUS,
    AAC,
    FLAC
};

/**
 * Stream connection status
 */
enum class StreamStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    STREAMING,
    ERROR,
    RECONNECTING
};

/**
 * Comprehensive streaming configuration
 */
struct StreamConfig {
    // Protocol and Server
    StreamProtocol protocol = StreamProtocol::ICECAST2;
    std::string server_host = "localhost";
    int server_port = 8000;
    std::string mount_point = "/stream";
    std::string password = "hackme";
    std::string username = "source";  // For SHOUTcast v2
    
    // Stream metadata
    std::string stream_name = "OneStopRadio";
    std::string stream_description = "Professional DJ Streaming";
    std::string stream_genre = "Electronic";
    std::string stream_url = "https://onestopradio.com";
    
    // Audio encoding
    StreamCodec codec = StreamCodec::MP3;
    int bitrate = 128;              // kbps
    int sample_rate = 44100;        // Hz
    int channels = 2;               // Stereo
    int quality = 5;                // Codec-specific quality (0-10)
    
    // Connection settings
    bool auto_reconnect = true;
    int reconnect_delay = 5;        // seconds
    int max_reconnect_attempts = -1; // -1 = infinite
    int connection_timeout = 10;     // seconds
    
    // Advanced options
    bool public_stream = true;
    std::string user_agent = "OneStopRadio/1.0";
    std::map<std::string, std::string> extra_headers;
    
    // ICY metadata
    bool enable_metadata = true;
    std::string current_song = "";
    
    // Validation
    bool is_valid() const {
        return !server_host.empty() && 
               server_port > 0 && 
               bitrate > 0 && 
               sample_rate > 0 && 
               channels > 0;
    }
};

/**
 * Stream statistics and monitoring
 */
struct StreamStats {
    StreamStatus status = StreamStatus::DISCONNECTED;
    std::string status_message;
    
    // Connection info
    uint64_t connected_time = 0;     // milliseconds
    uint64_t bytes_sent = 0;
    uint64_t total_time = 0;         // Total streaming time
    int reconnect_count = 0;
    
    // Audio info
    double current_bitrate = 0.0;    // Current bitrate (kbps)
    double peak_level_left = 0.0;    // Peak audio level
    double peak_level_right = 0.0;
    double rms_level = 0.0;          // RMS level
    
    // Stream health
    int buffer_fill = 0;             // Buffer fill percentage
    int dropped_frames = 0;
    double latency = 0.0;            // Stream latency (ms)
    
    // Listeners (if supported by server)
    int current_listeners = 0;
    int peak_listeners = 0;
};

/**
 * Audio callback for streaming data
 */
class AudioStreamCallback {
public:
    virtual ~AudioStreamCallback() = default;
    
    /**
     * Called when audio data is needed for streaming
     * @param buffer Output buffer to fill
     * @param frames Number of audio frames to provide
     * @param channels Number of audio channels
     * @return Number of frames actually provided
     */
    virtual size_t on_audio_data(float* buffer, size_t frames, int channels) = 0;
    
    /**
     * Called when metadata should be updated
     * @return Current song/metadata string
     */
    virtual std::string get_current_metadata() { return ""; }
};

/**
 * Professional Audio Stream Encoder
 * Supports Icecast2, SHOUTcast, and HTTP streaming
 */
class AudioStreamEncoder {
public:
    AudioStreamEncoder();
    ~AudioStreamEncoder();

    // Configuration
    bool configure(const StreamConfig& config);
    StreamConfig get_config() const { return config_; }
    
    // Connection management
    bool connect();
    bool disconnect();
    bool is_connected() const { return status_ == StreamStatus::CONNECTED || status_ == StreamStatus::STREAMING; }
    
    // Streaming control
    bool start_streaming(AudioStreamCallback* callback);
    bool stop_streaming();
    bool is_streaming() const { return status_ == StreamStatus::STREAMING; }
    
    // Audio data submission
    bool send_audio_data(const float* samples, size_t frames);
    bool send_raw_data(const uint8_t* data, size_t size);
    
    // Metadata management
    bool update_metadata(const std::string& title, const std::string& artist = "");
    bool set_stream_title(const std::string& title);
    
    // Status and monitoring
    StreamStatus get_status() const { return status_; }
    StreamStats get_statistics() const;
    std::string get_status_message() const { return status_message_; }
    
    // Advanced features
    bool set_gain(float gain);  // Linear gain adjustment
    bool enable_limiter(bool enabled, float threshold = -1.0f);
    bool enable_noise_gate(bool enabled, float threshold = -40.0f);
    
    // Static utility functions
    static std::vector<StreamCodec> get_supported_codecs(StreamProtocol protocol);
    static std::vector<int> get_supported_bitrates(StreamCodec codec);
    static std::vector<int> get_supported_sample_rates();
    static std::string codec_to_string(StreamCodec codec);
    static std::string protocol_to_string(StreamProtocol protocol);
    static StreamCodec string_to_codec(const std::string& codec_str);
    static StreamProtocol string_to_protocol(const std::string& protocol_str);

private:
    // Internal implementation
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    // Configuration
    StreamConfig config_;
    mutable std::mutex config_mutex_;
    
    // Status tracking
    std::atomic<StreamStatus> status_{StreamStatus::DISCONNECTED};
    std::string status_message_;
    mutable std::mutex status_mutex_;
    
    // Threading
    std::thread streaming_thread_;
    std::atomic<bool> should_stop_{false};
    std::condition_variable stop_condition_;
    std::mutex stop_mutex_;
    
    // Audio callback
    AudioStreamCallback* audio_callback_{nullptr};
    
    // Internal methods
    void streaming_worker();
    bool setup_encoder();
    bool setup_connection();
    void cleanup();
    bool encode_and_send(const float* samples, size_t frames);
    void handle_connection_error(const std::string& error);
    void update_statistics();
};

/**
 * Stream configuration builder for easy setup
 */
class StreamConfigBuilder {
public:
    StreamConfigBuilder() = default;
    
    // Protocol configuration
    StreamConfigBuilder& icecast2(const std::string& host, int port, const std::string& mount, const std::string& password);
    StreamConfigBuilder& shoutcast(const std::string& host, int port, const std::string& password, const std::string& username = "");
    StreamConfigBuilder& http(const std::string& url);
    
    // Audio configuration  
    StreamConfigBuilder& mp3(int bitrate, int sample_rate = 44100);
    StreamConfigBuilder& ogg_vorbis(int bitrate, int quality = 5);
    StreamConfigBuilder& ogg_opus(int bitrate, int sample_rate = 48000);
    StreamConfigBuilder& aac(int bitrate, int sample_rate = 44100);
    
    // Metadata
    StreamConfigBuilder& metadata(const std::string& name, const std::string& description, const std::string& genre);
    StreamConfigBuilder& url(const std::string& stream_url);
    
    // Connection options
    StreamConfigBuilder& reconnect(bool enabled, int delay = 5, int max_attempts = -1);
    StreamConfigBuilder& timeout(int seconds);
    StreamConfigBuilder& public_stream(bool is_public);
    
    // Build final configuration
    StreamConfig build() const { return config_; }
    
private:
    StreamConfig config_;
};

/**
 * Multi-stream manager for simultaneous streaming to multiple targets
 */
class MultiStreamManager {
public:
    MultiStreamManager();
    ~MultiStreamManager();
    
    // Stream management
    std::string add_stream(const std::string& name, const StreamConfig& config);
    bool remove_stream(const std::string& stream_id);
    bool configure_stream(const std::string& stream_id, const StreamConfig& config);
    
    // Control all streams
    bool start_all_streams(AudioStreamCallback* callback);
    bool stop_all_streams();
    bool connect_all();
    bool disconnect_all();
    
    // Individual stream control
    bool start_stream(const std::string& stream_id, AudioStreamCallback* callback = nullptr);
    bool stop_stream(const std::string& stream_id);
    bool connect_stream(const std::string& stream_id);
    bool disconnect_stream(const std::string& stream_id);
    
    // Audio data distribution
    bool send_audio_to_all(const float* samples, size_t frames);
    bool send_audio_to_stream(const std::string& stream_id, const float* samples, size_t frames);
    
    // Metadata management
    bool update_metadata_all(const std::string& title, const std::string& artist = "");
    bool update_metadata_stream(const std::string& stream_id, const std::string& title, const std::string& artist = "");
    
    // Status monitoring
    std::map<std::string, StreamStats> get_all_statistics() const;
    StreamStats get_stream_statistics(const std::string& stream_id) const;
    std::vector<std::string> get_active_streams() const;
    std::vector<std::string> get_connected_streams() const;
    
private:
    struct StreamInfo {
        std::string name;
        std::unique_ptr<AudioStreamEncoder> encoder;
        StreamConfig config;
    };
    
    std::map<std::string, StreamInfo> streams_;
    mutable std::mutex streams_mutex_;
    AudioStreamCallback* global_callback_{nullptr};
    
    std::string generate_stream_id();
};