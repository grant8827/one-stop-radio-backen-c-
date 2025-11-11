#include "stream_controller_api.hpp"
#include <iostream>
#include <sstream>
#include <regex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace onestopradio {

StreamControllerAPI::StreamControllerAPI(int port)
    : port_(port), running_(false) {
    stream_controller_ = std::make_unique<StreamController>();
    http_server_ = std::make_unique<HttpServer>(port);
}

StreamControllerAPI::~StreamControllerAPI() {
    Stop();
}

bool StreamControllerAPI::Initialize(const std::string& config_file) {
    if (!stream_controller_->Initialize(config_file)) {
        std::cerr << "Failed to initialize StreamController" << std::endl;
        return false;
    }

    // Set up HTTP routes
    http_server_->add_route("/api/v1/streams", [this](const HttpRequest& req) {
        if (req.method == "POST") {
            return HandleCreateStream(req);
        } else if (req.method == "GET") {
            return HandleGetAllStreams(req);
        }
        return CreateErrorResponse("Method not allowed", 405);
    });

    http_server_->add_route("/api/v1/streams/([^/]+)/activate", [this](const HttpRequest& req) {
        if (req.method == "POST") {
            return HandleActivateStream(req);
        }
        return CreateErrorResponse("Method not allowed", 405);
    });

    http_server_->add_route("/api/v1/streams/([^/]+)/deactivate", [this](const HttpRequest& req) {
        if (req.method == "POST") {
            return HandleDeactivateStream(req);
        }
        return CreateErrorResponse("Method not allowed", 405);
    });

    http_server_->add_route("/api/v1/streams/([^/]+)/status", [this](const HttpRequest& req) {
        if (req.method == "GET") {
            return HandleGetStreamStatus(req);
        }
        return CreateErrorResponse("Method not allowed", 405);
    });

    http_server_->add_route("/api/v1/streams/([^/]+)/metadata", [this](const HttpRequest& req) {
        if (req.method == "POST") {
            return HandleUpdateMetadata(req);
        }
        return CreateErrorResponse("Method not allowed", 405);
    });

    http_server_->add_route("/api/v1/streams/([^/]+)", [this](const HttpRequest& req) {
        if (req.method == "PUT") {
            return HandleUpdateStream(req);
        } else if (req.method == "DELETE") {
            return HandleDeleteStream(req);
        } else if (req.method == "GET") {
            return HandleGetStreamStatus(req);
        }
        return CreateErrorResponse("Method not allowed", 405);
    });

    http_server_->add_route("/health", [this](const HttpRequest& req) {
        return HandleHealthCheck(req);
    });

    http_server_->add_route("/api/v1/reload", [this](const HttpRequest& req) {
        if (req.method == "POST") {
            return HandleReloadConfig(req);
        }
        return CreateErrorResponse("Method not allowed", 405);
    });

    std::cout << "StreamController API initialized on port " << port_ << std::endl;
    return true;
}

void StreamControllerAPI::Run() {
    running_ = true;
    std::cout << "Starting StreamController API server on port " << port_ << std::endl;
    http_server_->run();
}

void StreamControllerAPI::Stop() {
    if (running_) {
        running_ = false;
        http_server_->stop();
        stream_controller_->Shutdown();
        std::cout << "StreamController API server stopped" << std::endl;
    }
}

std::string StreamControllerAPI::HandleCreateStream(const HttpRequest& request) {
    try {
        StreamConfig config = JsonToStreamConfig(request.body);
        
        if (stream_controller_->CreateMountPoint(config)) {
            json response = {
                {"success", true},
                {"message", "Stream created successfully"},
                {"stream_id", config.stream_id},
                {"mount_point", config.mount_point}
            };
            return response.dump();
        } else {
            return CreateErrorResponse("Failed to create stream");
        }
    } catch (const std::exception& e) {
        return CreateErrorResponse("Invalid request: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::HandleActivateStream(const HttpRequest& request) {
    try {
        std::string stream_id = ExtractStreamId(request.path);
        
        if (stream_controller_->ActivateStream(stream_id)) {
            json response = {
                {"success", true},
                {"message", "Stream activated successfully"},
                {"stream_id", stream_id}
            };
            return response.dump();
        } else {
            return CreateErrorResponse("Failed to activate stream");
        }
    } catch (const std::exception& e) {
        return CreateErrorResponse("Error: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::HandleDeactivateStream(const HttpRequest& request) {
    try {
        std::string stream_id = ExtractStreamId(request.path);
        
        if (stream_controller_->DeactivateStream(stream_id)) {
            json response = {
                {"success", true},
                {"message", "Stream deactivated successfully"},
                {"stream_id", stream_id}
            };
            return response.dump();
        } else {
            return CreateErrorResponse("Failed to deactivate stream");
        }
    } catch (const std::exception& e) {
        return CreateErrorResponse("Error: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::HandleDeleteStream(const HttpRequest& request) {
    try {
        std::string stream_id = ExtractStreamId(request.path);
        
        if (stream_controller_->DeleteMountPoint(stream_id)) {
            json response = {
                {"success", true},
                {"message", "Stream deleted successfully"},
                {"stream_id", stream_id}
            };
            return response.dump();
        } else {
            return CreateErrorResponse("Failed to delete stream");
        }
    } catch (const std::exception& e) {
        return CreateErrorResponse("Error: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::HandleUpdateStream(const HttpRequest& request) {
    try {
        std::string stream_id = ExtractStreamId(request.path);
        StreamConfig config = JsonToStreamConfig(request.body);
        config.stream_id = stream_id;  // Ensure stream_id matches URL
        
        if (stream_controller_->UpdateStreamConfig(stream_id, config)) {
            json response = {
                {"success", true},
                {"message", "Stream updated successfully"},
                {"stream_id", stream_id}
            };
            return response.dump();
        } else {
            return CreateErrorResponse("Failed to update stream");
        }
    } catch (const std::exception& e) {
        return CreateErrorResponse("Error: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::HandleGetStreamStatus(const HttpRequest& request) {
    try {
        std::string stream_id = ExtractStreamId(request.path);
        StreamStats stats = stream_controller_->GetStreamStatus(stream_id);
        
        if (stats.status == StreamStatus::ERROR && stats.stream_id.empty()) {
            return CreateErrorResponse("Stream not found", 404);
        }
        
        return StreamStatsToJson(stats);
    } catch (const std::exception& e) {
        return CreateErrorResponse("Error: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::HandleGetAllStreams(const HttpRequest& request) {
    try {
        std::vector<StreamStats> all_stats = stream_controller_->GetAllStreamStats();
        
        json response = {
            {"success", true},
            {"count", all_stats.size()},
            {"streams", json::array()}
        };
        
        for (const auto& stats : all_stats) {
            json stream_json = json::parse(StreamStatsToJson(stats));
            response["streams"].push_back(stream_json);
        }
        
        return response.dump();
    } catch (const std::exception& e) {
        return CreateErrorResponse("Error: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::HandleUpdateMetadata(const HttpRequest& request) {
    try {
        std::string stream_id = ExtractStreamId(request.path);
        json request_json = json::parse(request.body);
        
        std::string title = request_json.value("title", "");
        std::string artist = request_json.value("artist", "");
        
        if (stream_controller_->UpdateMetadata(stream_id, title, artist)) {
            json response = {
                {"success", true},
                {"message", "Metadata updated successfully"},
                {"stream_id", stream_id}
            };
            return response.dump();
        } else {
            return CreateErrorResponse("Failed to update metadata");
        }
    } catch (const std::exception& e) {
        return CreateErrorResponse("Error: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::HandleHealthCheck(const HttpRequest& request) {
    json response = {
        {"healthy", stream_controller_->IsHealthy()},
        {"status", stream_controller_->GetHealthStatus()},
        {"service", "StreamController API"},
        {"version", "1.0.0"},
        {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    return response.dump();
}

std::string StreamControllerAPI::HandleReloadConfig(const HttpRequest& request) {
    try {
        if (stream_controller_->ReloadServerConfig()) {
            json response = {
                {"success", true},
                {"message", "Server configuration reloaded successfully"}
            };
            return response.dump();
        } else {
            return CreateErrorResponse("Failed to reload configuration");
        }
    } catch (const std::exception& e) {
        return CreateErrorResponse("Error: " + std::string(e.what()));
    }
}

std::string StreamControllerAPI::CreateErrorResponse(const std::string& error, int code) {
    json response = {
        {"success", false},
        {"error", error},
        {"code", code}
    };
    return response.dump();
}

std::string StreamControllerAPI::CreateSuccessResponse(const std::string& message, const std::string& data) {
    json response = {
        {"success", true},
        {"message", message}
    };
    
    if (!data.empty()) {
        try {
            json data_json = json::parse(data);
            response["data"] = data_json;
        } catch (...) {
            response["data"] = data;
        }
    }
    
    return response.dump();
}

StreamConfig StreamControllerAPI::JsonToStreamConfig(const std::string& json_str) {
    json j = json::parse(json_str);
    
    StreamConfig config;
    config.stream_id = j.value("stream_id", "");
    config.user_id = j.value("user_id", "");
    config.mount_point = j.value("mount_point", "");
    config.source_password = j.value("source_password", "");
    config.station_name = j.value("station_name", "");
    config.description = j.value("description", "");
    config.genre = j.value("genre", "");
    config.max_listeners = j.value("max_listeners", 100);
    config.server_host = j.value("server_host", "localhost");
    config.server_port = j.value("server_port", 8000);
    config.protocol = j.value("protocol", "icecast");
    config.format = j.value("format", "MP3");
    config.public_stream = j.value("public_stream", true);
    
    // Handle quality enum
    int quality_value = j.value("quality", 128);
    config.quality = static_cast<StreamQuality>(quality_value);
    
    return config;
}

std::string StreamControllerAPI::StreamStatsToJson(const StreamStats& stats) {
    json j = {
        {"stream_id", stats.stream_id},
        {"status", static_cast<int>(stats.status)},
        {"status_name", [](StreamStatus status) {
            switch (status) {
                case StreamStatus::PENDING: return "PENDING";
                case StreamStatus::READY: return "READY";
                case StreamStatus::ACTIVE: return "ACTIVE";
                case StreamStatus::INACTIVE: return "INACTIVE";
                case StreamStatus::ERROR: return "ERROR";
                case StreamStatus::SUSPENDED: return "SUSPENDED";
                case StreamStatus::DELETED: return "DELETED";
                default: return "UNKNOWN";
            }
        }(stats.status)},
        {"is_connected", stats.is_connected},
        {"current_listeners", stats.current_listeners},
        {"peak_listeners", stats.peak_listeners},
        {"bytes_sent", stats.bytes_sent},
        {"uptime_seconds", stats.uptime_seconds},
        {"start_time", std::chrono::duration_cast<std::chrono::seconds>(
            stats.start_time.time_since_epoch()).count()},
        {"last_update", std::chrono::duration_cast<std::chrono::seconds>(
            stats.last_update.time_since_epoch()).count()}
    };
    
    if (!stats.current_song.empty()) {
        j["current_song"] = stats.current_song;
    }
    
    if (!stats.error_message.empty()) {
        j["error_message"] = stats.error_message;
    }
    
    return j.dump();
}

std::string StreamControllerAPI::ExtractStreamId(const std::string& path) {
    // Extract stream_id from paths like "/api/v1/streams/{stream_id}/activate"
    std::regex pattern(R"(/api/v1/streams/([^/]+)(?:/[^/]+)?)");
    std::smatch matches;
    
    if (std::regex_search(path, matches, pattern) && matches.size() > 1) {
        return matches[1].str();
    }
    
    throw std::runtime_error("Could not extract stream_id from path: " + path);
}

} // namespace onestopradio