#include "stream_controller.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <regex>
#include <iostream>
#include <thread>

namespace onestopradio {

StreamController::StreamController() 
    : initialized_(false), running_(false) {
    
    // Initialize libshout
    if (shout_init() != SHOUTERR_SUCCESS) {
        std::cerr << "Failed to initialize libshout" << std::endl;
    }
}

StreamController::~StreamController() {
    Shutdown();
    shout_shutdown();
}

bool StreamController::Initialize(const std::string& config_file) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    if (initialized_) {
        return true;
    }
    
    config_file_path_ = config_file;
    
    // Set default paths (can be configured via config file)
    icecast_config_path_ = "/etc/icecast2/icecast.xml";
    icecast_binary_path_ = "/usr/bin/icecast2";
    log_directory_ = "/var/log/icecast2";
    
    // Initialize server configuration with defaults
    server_config_.config_path = icecast_config_path_;
    server_config_.log_dir = log_directory_;
    server_config_.admin_password = "hackme123";  // Should be configurable
    server_config_.source_password = "hackme";     // Should be configurable
    server_config_.port = 8000;
    server_config_.max_clients = 1000;
    server_config_.max_sources = 10;
    
    // Create log directory if it doesn't exist
    std::filesystem::create_directories(log_directory_);
    
    initialized_ = true;
    running_ = true;
    
    std::cout << "StreamController initialized successfully" << std::endl;
    return true;
}

void StreamController::Shutdown() {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    if (!running_) {
        return;
    }
    
    // Close all active streams
    for (auto& [stream_id, stream] : streams_) {
        if (stream->shout_connection) {
            shout_close(stream->shout_connection);
            shout_free(stream->shout_connection);
            stream->shout_connection = nullptr;
        }
    }
    
    streams_.clear();
    running_ = false;
    
    std::cout << "StreamController shutdown completed" << std::endl;
}

bool StreamController::CreateMountPoint(const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    if (!running_) {
        std::cerr << "StreamController not running" << std::endl;
        return false;
    }
    
    if (streams_.find(config.stream_id) != streams_.end()) {
        std::cerr << "Stream already exists: " << config.stream_id << std::endl;
        return false;
    }
    
    // Validate configuration
    if (!ValidateConfig(config)) {
        std::cerr << "Invalid stream configuration for: " << config.stream_id << std::endl;
        return false;
    }
    
    // Create new stream
    auto stream = std::make_unique<Stream>();
    stream->config = config;
    stream->status = StreamStatus::READY;
    stream->shout_connection = nullptr;
    stream->start_time = std::chrono::system_clock::now();
    stream->bytes_sent = 0;
    stream->current_listeners = 0;
    stream->peak_listeners = 0;
    
    streams_[config.stream_id] = std::move(stream);
    
    // Add to server configuration
    server_config_.mount_points.push_back(config);
    
    // Update Icecast configuration
    if (!WriteIcecastConfig(server_config_)) {
        std::cerr << "Failed to update Icecast configuration" << std::endl;
        streams_.erase(config.stream_id);
        return false;
    }
    
    std::cout << "Mount point created: " << config.mount_point << " for stream: " << config.stream_id << std::endl;
    return true;
}

bool StreamController::ActivateStream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        std::cerr << "Stream not found: " << stream_id << std::endl;
        return false;
    }
    
    auto& stream = it->second;
    
    if (stream->status == StreamStatus::ACTIVE) {
        std::cout << "Stream already active: " << stream_id << std::endl;
        return true;
    }
    
    // Create shout connection
    if (!CreateShoutConnection(stream_id)) {
        stream->status = StreamStatus::ERROR;
        stream->error_message = "Failed to create shout connection";
        return false;
    }
    
    stream->status = StreamStatus::ACTIVE;
    stream->start_time = std::chrono::system_clock::now();
    stream->error_message.clear();
    
    std::cout << "Stream activated: " << stream_id << std::endl;
    return true;
}

bool StreamController::DeactivateStream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        std::cerr << "Stream not found: " << stream_id << std::endl;
        return false;
    }
    
    auto& stream = it->second;
    
    if (stream->status != StreamStatus::ACTIVE) {
        std::cout << "Stream not active: " << stream_id << std::endl;
        return true;
    }
    
    CloseShoutConnection(stream_id);
    stream->status = StreamStatus::INACTIVE;
    
    std::cout << "Stream deactivated: " << stream_id << std::endl;
    return true;
}

bool StreamController::DeleteMountPoint(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        std::cerr << "Stream not found: " << stream_id << std::endl;
        return false;
    }
    
    // Deactivate if active
    auto& stream = it->second;
    if (stream->status == StreamStatus::ACTIVE) {
        CloseShoutConnection(stream_id);
    }
    
    // Remove from server configuration
    auto& mount_points = server_config_.mount_points;
    mount_points.erase(
        std::remove_if(mount_points.begin(), mount_points.end(),
            [&stream_id](const StreamConfig& config) {
                return config.stream_id == stream_id;
            }),
        mount_points.end()
    );
    
    // Remove from streams map
    streams_.erase(it);
    
    // Update Icecast configuration
    WriteIcecastConfig(server_config_);
    
    std::cout << "Mount point deleted: " << stream_id << std::endl;
    return true;
}

bool StreamController::UpdateStreamConfig(const std::string& stream_id, const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        std::cerr << "Stream not found: " << stream_id << std::endl;
        return false;
    }
    
    if (!ValidateConfig(config)) {
        std::cerr << "Invalid stream configuration" << std::endl;
        return false;
    }
    
    auto& stream = it->second;
    bool was_active = (stream->status == StreamStatus::ACTIVE);
    
    // Deactivate if currently active
    if (was_active) {
        CloseShoutConnection(stream_id);
    }
    
    // Update configuration
    stream->config = config;
    
    // Update in server configuration
    for (auto& mount_point : server_config_.mount_points) {
        if (mount_point.stream_id == stream_id) {
            mount_point = config;
            break;
        }
    }
    
    // Update Icecast configuration
    WriteIcecastConfig(server_config_);
    
    // Reactivate if it was active
    if (was_active) {
        CreateShoutConnection(stream_id);
        stream->status = StreamStatus::ACTIVE;
    }
    
    std::cout << "Stream configuration updated: " << stream_id << std::endl;
    return true;
}

StreamStats StreamController::GetStreamStatus(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    StreamStats stats{};
    stats.stream_id = stream_id;
    stats.status = StreamStatus::ERROR;
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        stats.error_message = "Stream not found";
        return stats;
    }
    
    const auto& stream = it->second;
    stats.status = stream->status;
    stats.is_connected = (stream->shout_connection != nullptr);
    stats.current_listeners = stream->current_listeners;
    stats.peak_listeners = stream->peak_listeners;
    stats.bytes_sent = stream->bytes_sent;
    stats.start_time = stream->start_time;
    stats.last_update = std::chrono::system_clock::now();
    stats.error_message = stream->error_message;
    
    // Calculate uptime
    if (stream->status == StreamStatus::ACTIVE) {
        auto now = std::chrono::system_clock::now();
        stats.uptime_seconds = std::chrono::duration<double>(now - stream->start_time).count();
    } else {
        stats.uptime_seconds = 0.0;
    }
    
    return stats;
}

std::vector<StreamStats> StreamController::GetAllStreamStats() {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    std::vector<StreamStats> all_stats;
    for (const auto& [stream_id, stream] : streams_) {
        StreamStats stats{};
        stats.stream_id = stream_id;
        stats.status = stream->status;
        stats.is_connected = (stream->shout_connection != nullptr);
        stats.current_listeners = stream->current_listeners;
        stats.peak_listeners = stream->peak_listeners;
        stats.bytes_sent = stream->bytes_sent;
        stats.start_time = stream->start_time;
        stats.last_update = std::chrono::system_clock::now();
        stats.error_message = stream->error_message;
        
        if (stream->status == StreamStatus::ACTIVE) {
            auto now = std::chrono::system_clock::now();
            stats.uptime_seconds = std::chrono::duration<double>(now - stream->start_time).count();
        } else {
            stats.uptime_seconds = 0.0;
        }
        
        all_stats.push_back(stats);
    }
    
    return all_stats;
}

bool StreamController::IsStreamActive(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }
    
    return it->second->status == StreamStatus::ACTIVE;
}

bool StreamController::ValidateConfig(const StreamConfig& config) {
    // Check required fields
    if (config.stream_id.empty() || config.mount_point.empty()) {
        return false;
    }
    
    // Validate mount point format (should start with /)
    if (config.mount_point[0] != '/') {
        return false;
    }
    
    // Validate quality settings
    int bitrate = static_cast<int>(config.quality);
    if (bitrate < 32 || bitrate > 320) {
        return false;
    }
    
    // Validate listener limits
    if (config.max_listeners < 1 || config.max_listeners > 10000) {
        return false;
    }
    
    return true;
}

std::string StreamController::GenerateIcecastConfig(const IcecastConfigData& data) {
    std::ostringstream config;
    
    config << "<?xml version=\"1.0\"?>\n";
    config << "<icecast>\n";
    config << "  <location>OneStopRadio Stream Server</location>\n";
    config << "  <admin>admin@onestopradio.com</admin>\n";
    config << "  <limits>\n";
    config << "    <clients>" << data.max_clients << "</clients>\n";
    config << "    <sources>" << data.max_sources << "</sources>\n";
    config << "    <queue-size>524288</queue-size>\n";
    config << "    <client-timeout>30</client-timeout>\n";
    config << "    <header-timeout>15</header-timeout>\n";
    config << "    <source-timeout>10</source-timeout>\n";
    config << "    <burst-on-connect>1</burst-on-connect>\n";
    config << "    <burst-size>65535</burst-size>\n";
    config << "  </limits>\n";
    config << "  <authentication>\n";
    config << "    <source-password>" << data.source_password << "</source-password>\n";
    config << "    <admin-user>admin</admin-user>\n";
    config << "    <admin-password>" << data.admin_password << "</admin-password>\n";
    config << "  </authentication>\n";
    config << "  <hostname>localhost</hostname>\n";
    config << "  <listen-socket>\n";
    config << "    <port>" << data.port << "</port>\n";
    config << "  </listen-socket>\n";
    config << "  <http-headers>\n";
    config << "    <header name=\"Access-Control-Allow-Origin\" value=\"*\" />\n";
    config << "  </http-headers>\n";
    config << "  <fileserve>1</fileserve>\n";
    config << "  <paths>\n";
    config << "    <basedir>/usr/share/icecast2</basedir>\n";
    config << "    <logdir>" << data.log_dir << "</logdir>\n";
    config << "    <pidfile>/var/run/icecast2/icecast2.pid</pidfile>\n";
    config << "    <webroot>/usr/share/icecast2/web</webroot>\n";
    config << "    <adminroot>/usr/share/icecast2/admin</adminroot>\n";
    config << "    <alias source=\"/\" destination=\"/status.xsl\"/>\n";
    config << "  </paths>\n";
    config << "  <logging>\n";
    config << "    <accesslog>access.log</accesslog>\n";
    config << "    <errorlog>error.log</errorlog>\n";
    config << "    <loglevel>3</loglevel>\n";
    config << "    <logsize>10000</logsize>\n";
    config << "    <logarchive>1</logarchive>\n";
    config << "  </logging>\n";
    config << "  <security>\n";
    config << "    <chroot>0</chroot>\n";
    config << "  </security>\n";
    
    // Add mount points for each stream
    for (const auto& stream_config : data.mount_points) {
        config << "  <mount type=\"normal\">\n";
        config << "    <mount-name>" << stream_config.mount_point << "</mount-name>\n";
        config << "    <username>" << stream_config.user_id << "</username>\n";
        config << "    <password>" << stream_config.source_password << "</password>\n";
        config << "    <max-listeners>" << stream_config.max_listeners << "</max-listeners>\n";
        config << "    <dump-file>/tmp/dump-" << stream_config.stream_id << ".mp3</dump-file>\n";
        config << "    <burst-size>65536</burst-size>\n";
        config << "    <fallback-mount>/silence.mp3</fallback-mount>\n";
        config << "    <fallback-override>1</fallback-override>\n";
        config << "    <fallback-when-full>1</fallback-when-full>\n";
        config << "    <intro>/intro.mp3</intro>\n";
        config << "    <hidden>0</hidden>\n";
        config << "    <public>" << (stream_config.public_stream ? "1" : "0") << "</public>\n";
        config << "    <stream-name>" << stream_config.station_name << "</stream-name>\n";
        config << "    <stream-description>" << stream_config.description << "</stream-description>\n";
        config << "    <stream-url>https://onestopradio.com</stream-url>\n";
        config << "    <genre>" << stream_config.genre << "</genre>\n";
        config << "    <bitrate>" << static_cast<int>(stream_config.quality) << "</bitrate>\n";
        config << "    <type>application/ogg</type>\n";
        config << "    <subtype>vorbis</subtype>\n";
        config << "    <authentication type=\"htpasswd\">\n";
        config << "      <option name=\"filename\" value=\"/etc/icecast2/htpasswd\"/>\n";
        config << "      <option name=\"allow_duplicate_users\" value=\"0\"/>\n";
        config << "    </authentication>\n";
        config << "  </mount>\n";
    }
    
    config << "</icecast>\n";
    
    return config.str();
}

bool StreamController::WriteIcecastConfig(const IcecastConfigData& data) {
    try {
        std::string config_content = GenerateIcecastConfig(data);
        
        std::ofstream config_file(data.config_path);
        if (!config_file.is_open()) {
            std::cerr << "Failed to open Icecast config file: " << data.config_path << std::endl;
            return false;
        }
        
        config_file << config_content;
        config_file.close();
        
        std::cout << "Icecast configuration written to: " << data.config_path << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error writing Icecast config: " << e.what() << std::endl;
        return false;
    }
}

bool StreamController::ReloadServerConfig() {
    // Send SIGHUP to reload Icecast configuration
    std::string command = "pkill -HUP icecast2";
    int result = std::system(command.c_str());
    
    if (result == 0) {
        std::cout << "Icecast server configuration reloaded" << std::endl;
        return true;
    } else {
        std::cerr << "Failed to reload Icecast server configuration" << std::endl;
        return false;
    }
}

bool StreamController::CreateShoutConnection(const std::string& stream_id) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }
    
    auto& stream = it->second;
    const auto& config = stream->config;
    
    // Create new shout connection
    shout_t* shout = shout_new();
    if (!shout) {
        stream->error_message = "Failed to create shout object";
        return false;
    }
    
    // Configure shout connection
    if (shout_set_host(shout, config.server_host.c_str()) != SHOUTERR_SUCCESS ||
        shout_set_port(shout, config.server_port) != SHOUTERR_SUCCESS ||
        shout_set_password(shout, config.source_password.c_str()) != SHOUTERR_SUCCESS ||
        shout_set_mount(shout, config.mount_point.c_str()) != SHOUTERR_SUCCESS ||
        shout_set_user(shout, config.user_id.c_str()) != SHOUTERR_SUCCESS) {
        
        stream->error_message = "Failed to configure shout connection";
        shout_free(shout);
        return false;
    }
    
    // Set protocol and format
    if (config.protocol == "icecast") {
        shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP);
    } else {
        shout_set_protocol(shout, SHOUT_PROTOCOL_ICY);
    }
    
    if (config.format == "MP3") {
        shout_set_format(shout, SHOUT_FORMAT_MP3);
    } else if (config.format == "OGG") {
        shout_set_format(shout, SHOUT_FORMAT_OGV);
    }
    
    // Set metadata
    shout_set_name(shout, config.station_name.c_str());
    shout_set_description(shout, config.description.c_str());
    shout_set_genre(shout, config.genre.c_str());
    
    // Store connection
    stream->shout_connection = shout;
    
    std::cout << "Shout connection created for stream: " << stream_id << std::endl;
    return true;
}

void StreamController::CloseShoutConnection(const std::string& stream_id) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return;
    }
    
    auto& stream = it->second;
    if (stream->shout_connection) {
        shout_close(stream->shout_connection);
        shout_free(stream->shout_connection);
        stream->shout_connection = nullptr;
        std::cout << "Shout connection closed for stream: " << stream_id << std::endl;
    }
}

bool StreamController::UpdateMetadata(const std::string& stream_id, const std::string& title, const std::string& artist) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end() || !it->second->shout_connection) {
        return false;
    }
    
    auto& stream = it->second;
    
    std::string metadata_string;
    if (!artist.empty()) {
        metadata_string = artist + " - " + title;
    } else {
        metadata_string = title;
    }
    
    shout_metadata_t* metadata = shout_metadata_new();
    if (shout_metadata_add(metadata, "song", metadata_string.c_str()) == SHOUTERR_SUCCESS) {
        shout_set_metadata(stream->shout_connection, metadata);
    }
    shout_metadata_free(metadata);
    
    return true;
}

bool StreamController::SetStreamTitle(const std::string& stream_id, const std::string& title) {
    return UpdateMetadata(stream_id, title, "");
}

bool StreamController::SendAudioData(const std::string& stream_id, const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end() || !it->second->shout_connection) {
        return false;
    }
    
    auto& stream = it->second;
    
    if (shout_send(stream->shout_connection, data, size) != SHOUTERR_SUCCESS) {
        stream->error_message = "Failed to send audio data: " + std::string(shout_get_error(stream->shout_connection));
        return false;
    }
    
    stream->bytes_sent += size;
    return true;
}

bool StreamController::IsHealthy() const {
    return initialized_ && running_;
}

std::string StreamController::GetHealthStatus() const {
    if (!initialized_) {
        return "Not initialized";
    }
    if (!running_) {
        return "Not running";
    }
    
    std::lock_guard<std::mutex> lock(streams_mutex_);
    std::ostringstream status;
    status << "Healthy - " << streams_.size() << " streams configured";
    
    int active_count = 0;
    for (const auto& [id, stream] : streams_) {
        if (stream->status == StreamStatus::ACTIVE) {
            active_count++;
        }
    }
    
    status << ", " << active_count << " active";
    return status.str();
}

} // namespace onestopradio