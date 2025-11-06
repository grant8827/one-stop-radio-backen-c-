#include "webrtc_server.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>

WebRTCServer::WebRTCServer(int port) : port_(port), running_(false) {
    Logger::info("WebRTCServer", "Initialized on port " + std::to_string(port));
}

WebRTCServer::~WebRTCServer() {
    stop();
}

bool WebRTCServer::start() {
    if (running_) {
        Logger::warn("WebRTCServer", "Server already running");
        return true;
    }
    
    try {
        // Initialize WebSocket server for signaling
        signaling_server_.set_access_channels(websocketpp::log::alevel::all);
        signaling_server_.clear_access_channels(websocketpp::log::alevel::frame_payload);
        
        signaling_server_.init_asio();
        signaling_server_.set_reuse_addr(true);
        
        // Set message handlers
        signaling_server_.set_message_handler(
            [this](websocketpp::connection_hdl hdl, websocketpp::server<websocketpp::config::asio>::message_ptr msg) {
                handle_message(hdl, msg);
            });
        
        signaling_server_.set_open_handler(
            [this](websocketpp::connection_hdl hdl) {
                handle_connection(hdl);
            });
        
        signaling_server_.set_close_handler(
            [this](websocketpp::connection_hdl hdl) {
                handle_disconnect(hdl);
            });
        
        // Start listening
        signaling_server_.listen(port_);
        signaling_server_.start_accept();
        
        // Run server in separate thread
        server_thread_ = std::thread([this]() {
            signaling_server_.run();
        });
        
        running_ = true;
        Logger::info("WebRTCServer", "Started successfully on port " + std::to_string(port_));
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("WebRTCServer", "Failed to start: " + std::string(e.what()));
        return false;
    }
}

void WebRTCServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    try {
        signaling_server_.stop();
        
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        // Clean up all connections
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.clear();
        
        Logger::info("WebRTCServer", "Stopped successfully");
        
    } catch (const std::exception& e) {
        Logger::error("WebRTCServer", "Error during stop: " + std::string(e.what()));
    }
}

void WebRTCServer::handle_connection(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    // Create new client connection
    auto client = std::make_shared<WebRTCClient>();
    client->connection = hdl;
    client->connected_at = std::chrono::steady_clock::now();
    
    connections_[hdl] = client;
    
    Logger::info("WebRTCServer", "New client connected. Total connections: " + 
                std::to_string(connections_.size()));
}

void WebRTCServer::handle_disconnect(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(hdl);
    if (it != connections_.end()) {
        connections_.erase(it);
        
        Logger::info("WebRTCServer", "Client disconnected. Total connections: " + 
                    std::to_string(connections_.size()));
    }
}

void WebRTCServer::handle_message(websocketpp::connection_hdl hdl, 
                                websocketpp::server<websocketpp::config::asio>::message_ptr msg) {
    try {
        // Parse JSON message
        json message = json::parse(msg->get_payload());
        std::string type = message.value("type", "");
        
        Logger::debug("WebRTCServer", "Received message type: " + type);
        
        if (type == "offer") {
            handle_offer(hdl, message);
        } else if (type == "answer") {
            handle_answer(hdl, message);
        } else if (type == "ice-candidate") {
            handle_ice_candidate(hdl, message);
        } else if (type == "start-stream") {
            handle_start_stream(hdl, message);
        } else if (type == "stop-stream") {
            handle_stop_stream(hdl, message);
        } else {
            Logger::warn("WebRTCServer", "Unknown message type: " + type);
        }
        
    } catch (const std::exception& e) {
        Logger::error("WebRTCServer", "Error handling message: " + std::string(e.what()));
    }
}

void WebRTCServer::handle_offer(websocketpp::connection_hdl hdl, const json& message) {
    // In a real WebRTC implementation, this would:
    // 1. Create a peer connection
    // 2. Set remote description (offer)
    // 3. Create answer
    // 4. Set local description
    // 5. Send answer back to client
    
    // For now, send a mock answer
    json response = {
        {"type", "answer"},
        {"sdp", "mock-answer-sdp"},
        {"success", true}
    };
    
    send_message(hdl, response);
    Logger::info("WebRTCServer", "Processed offer and sent answer");
}

void WebRTCServer::handle_answer(websocketpp::connection_hdl hdl, const json& message) {
    // Handle WebRTC answer (typically from client in response to our offer)
    Logger::info("WebRTCServer", "Received answer from client");
    
    // Update client state
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(hdl);
    if (it != connections_.end()) {
        it->second->has_webrtc_connection = true;
    }
}

void WebRTCServer::handle_ice_candidate(websocketpp::connection_hdl hdl, const json& message) {
    // Handle ICE candidate for NAT traversal
    Logger::debug("WebRTCServer", "Received ICE candidate");
    
    // In a real implementation, this would be added to the peer connection
    // For now, just acknowledge
    json response = {
        {"type", "ice-candidate-ack"},
        {"success", true}
    };
    
    send_message(hdl, response);
}

void WebRTCServer::handle_start_stream(websocketpp::connection_hdl hdl, const json& message) {
    std::string stream_id = message.value("stream_id", "default");
    
    // Update client state
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(hdl);
    if (it != connections_.end()) {
        it->second->is_streaming = true;
        it->second->stream_id = stream_id;
        
        // Register audio data callback
        if (audio_callback_) {
            it->second->has_audio_callback = true;
        }
    }
    
    json response = {
        {"type", "stream-started"},
        {"stream_id", stream_id},
        {"success", true}
    };
    
    send_message(hdl, response);
    Logger::info("WebRTCServer", "Started streaming for client: " + stream_id);
}

void WebRTCServer::handle_stop_stream(websocketpp::connection_hdl hdl, const json& message) {
    // Update client state
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(hdl);
    if (it != connections_.end()) {
        std::string stream_id = it->second->stream_id;
        it->second->is_streaming = false;
        it->second->stream_id = "";
        it->second->has_audio_callback = false;
        
        json response = {
            {"type", "stream-stopped"},
            {"stream_id", stream_id},
            {"success", true}
        };
        
        send_message(hdl, response);
        Logger::info("WebRTCServer", "Stopped streaming for client: " + stream_id);
    }
}

void WebRTCServer::send_message(websocketpp::connection_hdl hdl, const json& message) {
    try {
        std::string msg_str = message.dump();
        signaling_server_.send(hdl, msg_str, websocketpp::frame::opcode::text);
    } catch (const std::exception& e) {
        Logger::error("WebRTCServer", "Failed to send message: " + std::string(e.what()));
    }
}

void WebRTCServer::broadcast_message(const json& message) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    std::string msg_str = message.dump();
    for (const auto& pair : connections_) {
        try {
            signaling_server_.send(pair.first, msg_str, websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            Logger::error("WebRTCServer", "Failed to broadcast to client: " + std::string(e.what()));
        }
    }
}

void WebRTCServer::set_audio_data_callback(AudioDataCallback callback) {
    audio_callback_ = std::move(callback);
    Logger::info("WebRTCServer", "Audio data callback registered");
}

size_t WebRTCServer::get_connection_count() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

size_t WebRTCServer::get_streaming_count() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    size_t streaming_count = 0;
    for (const auto& pair : connections_) {
        if (pair.second->is_streaming) {
            streaming_count++;
        }
    }
    
    return streaming_count;
}

std::vector<std::string> WebRTCServer::get_active_streams() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    std::vector<std::string> streams;
    for (const auto& pair : connections_) {
        if (pair.second->is_streaming && !pair.second->stream_id.empty()) {
            streams.push_back(pair.second->stream_id);
        }
    }
    
    return streams;
}