#include "stream_manager.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>

StreamManager::StreamManager() : running_(false) {
    Logger::info("StreamManager initialized");
}

StreamManager::~StreamManager() {
    stop_all_streams();
}

bool StreamManager::create_stream(const std::string& stream_id, const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    if (streams_.find(stream_id) != streams_.end()) {
        Logger::warn("Stream already exists: " + stream_id);
        return false;
    }
    
    auto stream = std::make_unique<Stream>();
    stream->id = stream_id;
    stream->config = config;
    stream->status = StreamStatus::STOPPED;
    stream->shout_connection = nullptr;
    
    streams_[stream_id] = std::move(stream);
    
    Logger::info("Stream created: " + stream_id);
    return true;
}

bool StreamManager::start_stream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        Logger::error("Stream not found: " + stream_id);
        return false;
    }
    
    auto& stream = it->second;
    if (stream->status == StreamStatus::STREAMING) {
        Logger::warn("Stream already running: " + stream_id);
        return true;
    }
    
    // Initialize libshout connection
    shout_t* shout = shout_new();
    if (!shout) {
        Logger::error("Failed to create shout object for stream: " + stream_id);
        return false;
    }
    
    // Configure libshout
    if (shout_set_host(shout, stream->config.server_host.c_str()) != SHOUTERR_SUCCESS ||
        shout_set_port(shout, stream->config.server_port) != SHOUTERR_SUCCESS ||
        shout_set_password(shout, stream->config.server_password.c_str()) != SHOUTERR_SUCCESS ||
        shout_set_mount(shout, stream->config.mount_point.c_str()) != SHOUTERR_SUCCESS ||
        shout_set_user(shout, stream->config.username.c_str()) != SHOUTERR_SUCCESS) {
        
        Logger::error("Failed to configure shout for stream: " + stream_id);
        shout_free(shout);
        return false;
    }
    
    // Set protocol (Icecast vs SHOUTcast)
    if (stream->config.protocol == "icecast") {
        shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP);
        shout_set_format(shout, SHOUT_FORMAT_MP3);
    } else {
        shout_set_protocol(shout, SHOUT_PROTOCOL_ICY);
        shout_set_format(shout, SHOUT_FORMAT_MP3);
    }
    
    // Set metadata
    shout_set_name(shout, stream->config.station_name.c_str());
    shout_set_description(shout, stream->config.description.c_str());
    shout_set_genre(shout, stream->config.genre.c_str());
    
    // Connect to server
    if (shout_open(shout) == SHOUTERR_SUCCESS) {
        stream->shout_connection = shout;
        stream->status = StreamStatus::STREAMING;
        
        Logger::info("Stream started: " + stream_id);
        return true;
    } else {
        Logger::error("Failed to connect to streaming server for: " + stream_id + 
                     " - " + shout_get_error(shout));
        shout_free(shout);
        return false;
    }
}

bool StreamManager::stop_stream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        Logger::error("Stream not found: " + stream_id);
        return false;
    }
    
    auto& stream = it->second;
    if (stream->status == StreamStatus::STOPPED) {
        Logger::warn("Stream already stopped: " + stream_id);
        return true;
    }
    
    if (stream->shout_connection) {
        shout_close(stream->shout_connection);
        shout_free(stream->shout_connection);
        stream->shout_connection = nullptr;
    }
    
    stream->status = StreamStatus::STOPPED;
    
    Logger::info("Stream stopped: " + stream_id);
    return true;
}

bool StreamManager::send_audio_data(const std::string& stream_id, 
                                  const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }
    
    auto& stream = it->second;
    if (stream->status != StreamStatus::STREAMING || !stream->shout_connection) {
        return false;
    }
    
    // Send data to streaming server
    int result = shout_send(stream->shout_connection, data, size);
    if (result != SHOUTERR_SUCCESS) {
        Logger::error("Failed to send audio data for stream: " + stream_id + 
                     " - " + shout_get_error(stream->shout_connection));
        return false;
    }
    
    // Update statistics
    stream->bytes_sent += size;
    
    return true;
}

StreamStatus StreamManager::get_stream_status(const std::string& stream_id) const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return StreamStatus::ERROR;
    }
    
    return it->second->status;
}

std::vector<std::string> StreamManager::get_active_streams() const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    std::vector<std::string> active_streams;
    for (const auto& pair : streams_) {
        if (pair.second->status == StreamStatus::STREAMING) {
            active_streams.push_back(pair.first);
        }
    }
    
    return active_streams;
}

void StreamManager::stop_all_streams() {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    for (auto& pair : streams_) {
        auto& stream = pair.second;
        if (stream->shout_connection) {
            shout_close(stream->shout_connection);
            shout_free(stream->shout_connection);
            stream->shout_connection = nullptr;
        }
        stream->status = StreamStatus::STOPPED;
    }
    
    streams_.clear();
    Logger::info("All streams stopped");
}

StreamStats StreamManager::get_stream_stats(const std::string& stream_id) const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    StreamStats stats = {};
    
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        const auto& stream = it->second;
        stats.bytes_sent = stream->bytes_sent;
        stats.is_connected = (stream->status == StreamStatus::STREAMING);
        stats.uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - stream->start_time).count();
    }
    
    return stats;
}