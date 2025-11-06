#include "video_stream_manager.hpp"
#include "http_server.hpp"
#include "config_manager.hpp"
#include "utils/logger.hpp"
#include <json/json.h>
#include <memory>
#include <iostream>

using json = Json::Value;

class VideoApiServer {
public:
    VideoApiServer(int port = 8081) : server_(port) {
        video_manager_ = std::make_unique<VideoStreamManager>();
        setup_routes();
    }
    
    bool initialize() {
        // Initialize video streaming with default format
        VideoFormat default_format;
        default_format.width = 1920;
        default_format.height = 1080;
        default_format.fps = 30;
        default_format.bitrate = 2500000;
        
        if (!video_manager_->initialize(default_format)) {
            Logger::error("VideoApiServer", "Failed to initialize video manager");
            return false;
        }
        
        Logger::info("VideoApiServer", "Video API Server initialized successfully");
        return true;
    }
    
    void run() {
        Logger::info("VideoApiServer", "Starting Video API Server on port 8081...");
        server_.run();
    }
    
private:
    void setup_routes() {
        // Video status and control
        server_.add_route("/api/video/status", [this](const HttpRequest& req) {
            return handle_video_status(req);
        });
        
        // Camera controls
        server_.add_route("/api/video/camera/on", [this](const HttpRequest& req) {
            return handle_camera_on(req);
        });
        
        server_.add_route("/api/video/camera/off", [this](const HttpRequest& req) {
            return handle_camera_off(req);
        });
        
        server_.add_route("/api/video/camera/settings", [this](const HttpRequest& req) {
            return handle_camera_settings(req);
        });
        
        // Video source controls
        server_.add_route("/api/video/image", [this](const HttpRequest& req) {
            return handle_set_image(req);
        });
        
        server_.add_route("/api/video/slideshow/start", [this](const HttpRequest& req) {
            return handle_slideshow_start(req);
        });
        
        server_.add_route("/api/video/slideshow/stop", [this](const HttpRequest& req) {
            return handle_slideshow_stop(req);
        });
        
        server_.add_route("/api/video/slideshow/next", [this](const HttpRequest& req) {
            return handle_slideshow_next(req);
        });
        
        server_.add_route("/api/video/slideshow/previous", [this](const HttpRequest& req) {
            return handle_slideshow_previous(req);
        });
        
        // Social media streaming
        server_.add_route("/api/video/stream/youtube", [this](const HttpRequest& req) {
            return handle_platform_config(req, "youtube");
        });
        
        server_.add_route("/api/video/stream/twitch", [this](const HttpRequest& req) {
            return handle_platform_config(req, "twitch");
        });
        
        server_.add_route("/api/video/stream/facebook", [this](const HttpRequest& req) {
            return handle_platform_config(req, "facebook");
        });
        
        // Streaming controls
        server_.add_route("/api/video/stream/start", [this](const HttpRequest& req) {
            return handle_streaming_start(req);
        });
        
        server_.add_route("/api/video/stream/stop", [this](const HttpRequest& req) {
            return handle_streaming_stop(req);
        });
        
        // Text overlay
        server_.add_route("/api/video/overlay/text", [this](const HttpRequest& req) {
            return handle_overlay_text(req);
        });
        
        server_.add_route("/api/video/overlay/clear", [this](const HttpRequest& req) {
            return handle_overlay_clear(req);
        });
        
        // Statistics
        server_.add_route("/api/video/stats", [this](const HttpRequest& req) {
            return handle_video_stats(req);
        });
        
        // Health check
        server_.add_route("/api/health", [this](const HttpRequest& req) {
            return handle_health_check(req);
        });
    }
    
    std::string handle_video_status(const HttpRequest& req) {
        json response;
        response["success"] = true;
        response["video_source"] = video_source_to_string(video_manager_->get_composer().get_current_source());
        response["camera"] = {
            {"enabled", video_manager_->get_composer().is_camera_enabled()},
            {"resolution", {{"width", 1920}, {"height", 1080}}},
            {"fps", 30}
        };
        response["streaming"] = {
            {"is_live", video_manager_->is_live()},
            {"active_streams", video_manager_->get_streamer().get_active_streams()}
        };
        response["slideshow"] = {
            {"active", video_manager_->get_composer().is_slideshow_active()}
        };
        
        Logger::info("VideoApiServer", "Video status requested");
        return response.dump();
    }
    
    std::string handle_camera_on(const HttpRequest& req) {
        json response;
        
        if (video_manager_->switch_to_camera()) {
            response["success"] = true;
            response["action"] = "camera_enabled";
            response["video_source"] = "camera";
            Logger::info("VideoApiServer", "Camera enabled");
        } else {
            response["success"] = false;
            response["error"] = "Failed to enable camera";
            Logger::error("VideoApiServer", "Failed to enable camera");
        }
        
        return response.dump();
    }
    
    std::string handle_camera_off(const HttpRequest& req) {
        json response;
        
        if (video_manager_->switch_to_off()) {
            response["success"] = true;
            response["action"] = "camera_disabled";
            response["video_source"] = "off";
            Logger::info("VideoApiServer", "Camera disabled");
        } else {
            response["success"] = false;
            response["error"] = "Failed to disable camera";
        }
        
        return response.dump();
    }
    
    std::string handle_camera_settings(const HttpRequest& req) {
        json response;
        
        try {
            json request_data = json::parse(req.body);
            
            // In a real implementation, apply camera settings here
            // For now, just acknowledge the request
            
            response["success"] = true;
            response["action"] = "camera_settings_updated";
            response["settings"] = request_data;
            
            Logger::info("VideoApiServer", "Camera settings updated");
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = "Invalid JSON in request body";
            Logger::error("VideoApiServer", "Invalid JSON in camera settings request");
        }
        
        return response.dump();
    }
    
    std::string handle_set_image(const HttpRequest& req) {
        json response;
        
        try {
            json request_data = json::parse(req.body);
            
            if (!request_data.contains("image_path")) {
                response["success"] = false;
                response["error"] = "image_path is required";
                return response.dump();
            }
            
            std::string image_path = request_data["image_path"];
            
            if (video_manager_->switch_to_image(image_path)) {
                response["success"] = true;
                response["action"] = "image_set";
                response["video_source"] = "image";
                response["image_path"] = image_path;
                Logger::info("VideoApiServer", "Static image set: " + image_path);
            } else {
                response["success"] = false;
                response["error"] = "Failed to set static image";
            }
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = "Invalid JSON in request body";
        }
        
        return response.dump();
    }
    
    std::string handle_slideshow_start(const HttpRequest& req) {
        json response;
        
        try {
            json request_data = json::parse(req.body);
            
            if (!request_data.contains("images") || !request_data["images"].is_array()) {
                response["success"] = false;
                response["error"] = "images array is required";
                return response.dump();
            }
            
            SlideShowConfig config;
            config.image_paths = request_data["images"].get<std::vector<std::string>>();
            config.slide_duration_seconds = request_data.value("duration", 5);
            config.loop = request_data.value("loop", true);
            config.transition_effect = request_data.value("transition", "fade");
            
            if (video_manager_->switch_to_slideshow(config)) {
                response["success"] = true;
                response["action"] = "slideshow_started";
                response["video_source"] = "slideshow";
                response["slideshow_config"] = {
                    {"image_count", config.image_paths.size()},
                    {"duration", config.slide_duration_seconds},
                    {"loop", config.loop},
                    {"transition", config.transition_effect}
                };
                Logger::info("VideoApiServer", "Slideshow started with " + 
                           std::to_string(config.image_paths.size()) + " images");
            } else {
                response["success"] = false;
                response["error"] = "Failed to start slideshow";
            }
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = "Invalid JSON in request body";
        }
        
        return response.dump();
    }
    
    std::string handle_slideshow_stop(const HttpRequest& req) {
        json response;
        
        video_manager_->get_composer().stop_slideshow();
        video_manager_->switch_to_off();
        
        response["success"] = true;
        response["action"] = "slideshow_stopped";
        response["video_source"] = "off";
        
        Logger::info("VideoApiServer", "Slideshow stopped");
        return response.dump();
    }
    
    std::string handle_slideshow_next(const HttpRequest& req) {
        json response;
        
        if (!video_manager_->get_composer().is_slideshow_active()) {
            response["success"] = false;
            response["error"] = "Slideshow is not active";
            return response.dump();
        }
        
        video_manager_->get_composer().next_slide();
        
        response["success"] = true;
        response["action"] = "next_slide";
        
        Logger::info("VideoApiServer", "Next slide");
        return response.dump();
    }
    
    std::string handle_slideshow_previous(const HttpRequest& req) {
        json response;
        
        if (!video_manager_->get_composer().is_slideshow_active()) {
            response["success"] = false;
            response["error"] = "Slideshow is not active";
            return response.dump();
        }
        
        video_manager_->get_composer().previous_slide();
        
        response["success"] = true;
        response["action"] = "previous_slide";
        
        Logger::info("VideoApiServer", "Previous slide");
        return response.dump();
    }
    
    std::string handle_platform_config(const HttpRequest& req, const std::string& platform) {
        json response;
        
        try {
            json request_data = json::parse(req.body);
            
            if (!request_data.contains("stream_key")) {
                response["success"] = false;
                response["error"] = "stream_key is required";
                return response.dump();
            }
            
            std::string stream_key = request_data["stream_key"];
            std::string title = request_data.value("title", "OneStopRadio Live Stream");
            
            bool success = false;
            if (platform == "youtube") {
                success = video_manager_->setup_youtube_stream(stream_key, title);
            } else if (platform == "twitch") {
                success = video_manager_->setup_twitch_stream(stream_key, title);
            } else if (platform == "facebook") {
                success = video_manager_->setup_facebook_stream(stream_key, title);
            }
            
            if (success) {
                response["success"] = true;
                response["action"] = "platform_configured";
                response["platform"] = platform;
                Logger::info("VideoApiServer", "Platform configured: " + platform);
            } else {
                response["success"] = false;
                response["error"] = "Failed to configure platform";
            }
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = "Invalid JSON in request body";
        }
        
        return response.dump();
    }
    
    std::string handle_streaming_start(const HttpRequest& req) {
        json response;
        
        try {
            json request_data = json::parse(req.body);
            
            std::vector<std::string> platforms;
            if (request_data.contains("platforms") && request_data["platforms"].is_array()) {
                platforms = request_data["platforms"].get<std::vector<std::string>>();
            }
            
            if (platforms.empty()) {
                response["success"] = false;
                response["error"] = "At least one platform must be specified";
                return response.dump();
            }
            
            if (video_manager_->start_live_stream(platforms)) {
                response["success"] = true;
                response["action"] = "streaming_started";
                response["is_live"] = true;
                response["started_platforms"] = platforms;
                Logger::info("VideoApiServer", "Live streaming started");
            } else {
                response["success"] = false;
                response["error"] = "Failed to start streaming";
            }
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = "Invalid JSON in request body";
        }
        
        return response.dump();
    }
    
    std::string handle_streaming_stop(const HttpRequest& req) {
        json response;
        
        if (video_manager_->stop_live_stream()) {
            response["success"] = true;
            response["action"] = "streaming_stopped";
            response["is_live"] = false;
            Logger::info("VideoApiServer", "Live streaming stopped");
        } else {
            response["success"] = false;
            response["error"] = "Failed to stop streaming";
        }
        
        return response.dump();
    }
    
    std::string handle_overlay_text(const HttpRequest& req) {
        json response;
        
        try {
            json request_data = json::parse(req.body);
            
            if (!request_data.contains("text")) {
                response["success"] = false;
                response["error"] = "text is required";
                return response.dump();
            }
            
            std::string text = request_data["text"];
            int x = request_data.value("x", 50);
            int y = request_data.value("y", 50);
            std::string font = request_data.value("font", "Arial");
            int font_size = request_data.value("font_size", 24);
            
            if (video_manager_->get_composer().add_text_overlay(text, x, y, font, font_size)) {
                response["success"] = true;
                response["action"] = "overlay_added";
                response["overlay"] = {
                    {"text", text},
                    {"x", x},
                    {"y", y},
                    {"font", font},
                    {"font_size", font_size}
                };
                Logger::info("VideoApiServer", "Text overlay added: " + text);
            } else {
                response["success"] = false;
                response["error"] = "Failed to add text overlay";
            }
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = "Invalid JSON in request body";
        }
        
        return response.dump();
    }
    
    std::string handle_overlay_clear(const HttpRequest& req) {
        json response;
        
        if (video_manager_->get_composer().remove_text_overlay()) {
            response["success"] = true;
            response["action"] = "overlay_cleared";
            Logger::info("VideoApiServer", "Text overlay cleared");
        } else {
            response["success"] = false;
            response["error"] = "Failed to clear overlay";
        }
        
        return response.dump();
    }
    
    std::string handle_video_stats(const HttpRequest& req) {
        json response;
        
        // Get statistics from active streams
        auto active_streams = video_manager_->get_streamer().get_active_streams();
        
        response["success"] = true;
        response["stats"] = {
            {"is_live", video_manager_->is_live()},
            {"video_source", video_source_to_string(video_manager_->get_composer().get_current_source())},
            {"active_streams", active_streams},
            {"stream_count", active_streams.size()}
        };
        
        // Add individual stream statistics
        json stream_stats = json::object();
        for (const auto& stream_id : active_streams) {
            auto stats = video_manager_->get_streamer().get_stream_stats(stream_id);
            stream_stats[stream_id] = {
                {"bytes_sent", stats.bytes_sent},
                {"frames_sent", stats.frames_sent},
                {"current_bitrate", stats.current_bitrate},
                {"is_connected", stats.is_connected},
                {"last_error", stats.last_error}
            };
        }
        response["stream_stats"] = stream_stats;
        
        return response.dump();
    }
    
    std::string handle_health_check(const HttpRequest& req) {
        json response;
        response["success"] = true;
        response["service"] = "OneStopRadio Video API Server";
        response["version"] = "1.0.0";
        response["status"] = "healthy";
        response["video_manager_initialized"] = (video_manager_ != nullptr);
        response["timestamp"] = std::time(nullptr);
        
        return response.dump();
    }
    
    std::string video_source_to_string(VideoSource source) {
        switch (source) {
            case VideoSource::CAMERA: return "camera";
            case VideoSource::IMAGE: return "image";
            case VideoSource::SLIDESHOW: return "slideshow";
            case VideoSource::OFF: return "off";
            default: return "unknown";
        }
    }
    
private:
    HttpServer server_;
    std::unique_ptr<VideoStreamManager> video_manager_;
};

int main(int argc, char* argv[]) {
    try {
        // Initialize logging
        Logger::init("video_api_server.log", Logger::Level::INFO);
        
        Logger::info("Main", "OneStopRadio Video API Server starting...");
        
        // Create and initialize server
        VideoApiServer server(8081);
        
        if (!server.initialize()) {
            Logger::error("Main", "Failed to initialize video API server");
            return 1;
        }
        
        Logger::info("Main", "Video API Server initialized successfully");
        Logger::info("Main", "Server will handle video streaming API on port 8081");
        Logger::info("Main", "Available endpoints:");
        Logger::info("Main", "  GET /api/video/status - Video streaming status");
        Logger::info("Main", "  POST /api/video/camera/on - Enable camera");
        Logger::info("Main", "  POST /api/video/camera/off - Disable camera");
        Logger::info("Main", "  POST /api/video/stream/start - Start live streaming");
        Logger::info("Main", "  POST /api/video/stream/stop - Stop live streaming");
        Logger::info("Main", "  GET /api/video/stats - Streaming statistics");
        
        // Run server (blocking call)
        server.run();
        
    } catch (const std::exception& e) {
        Logger::error("Main", std::string("Server error: ") + e.what());
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}