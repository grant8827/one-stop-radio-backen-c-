#include <iostream>
#include <memory>
#include <thread>
#include <signal.h>
#include <nlohmann/json.hpp>
#include "http_server.hpp"
#include "webrtc_server.hpp"
#include "stream_manager.hpp"
#include "video_stream_manager.hpp"
#include "audio_system.hpp"
#include "audio_stream_encoder.hpp"
#include "radio_control.hpp"
#include "config_manager.hpp"
#include "utils/logger.hpp"

using json = nlohmann::json;

class RadioServer {
public:
    RadioServer() 
        : config_manager_(),
          stream_manager_(),
          video_manager_(),
          audio_system_(),
          audio_encoder_(),
          radio_control_(nullptr),
          http_server_(8080),
          webrtc_server_(nullptr),
          running_(false) {}
    
    bool initialize(const std::string& config_file = "config/config.json") {
        Logger::info("Initializing OneStopRadio Server...");
        
        // Load configuration
        if (!config_manager_.load_from_file(config_file)) {
            Logger::warn("Using default configuration");
        }
        
        if (!config_manager_.validate_config()) {
            Logger::error("Configuration validation failed");
            return false;
        }
        
        // Initialize video streaming with default 1080p settings
        VideoFormat video_format;
        video_format.width = 1920;
        video_format.height = 1080;
        video_format.fps = 30;
        video_format.bitrate = 4500000; // 4.5 Mbps
        
        if (!video_manager_.initialize(video_format)) {
            Logger::error("Failed to initialize video streaming");
            return false;
        }
        
        // Initialize audio system with high-quality settings
        AudioFormat audio_format;
        audio_format.sample_rate = config_manager_.get_int("audio", "sample_rate", 48000);
        audio_format.channels = config_manager_.get_int("audio", "channels", 2);
        audio_format.bit_depth = config_manager_.get_int("audio", "bit_depth", 16);
        audio_format.bitrate = config_manager_.get_int("audio", "bitrate", 128000);
        
        if (!audio_system_.initialize(audio_format)) {
            Logger::error("Failed to initialize audio system");
            return false;
        }
        
        // Setup HTTP API routes
        setup_api_routes();
        
        // Initialize WebRTC server
        int webrtc_port = config_manager_.get_int("server", "webrtc_port", 8081);
        webrtc_server_ = std::make_unique<WebRTCServer>(webrtc_port);
        
        // Initialize radio control system
        radio_control_ = std::make_unique<RadioControl>(&audio_system_, &video_manager_, &audio_encoder_);
        if (!radio_control_->initialize()) {
            Logger::error("Failed to initialize radio control system");
            return false;
        }
        
        Logger::info("Server initialization complete");
        return true;
    }
    
    void run() {
        if (running_) {
            Logger::warn("Server is already running");
            return;
        }
        
        running_ = true;
        Logger::info("🎵 OneStopRadio Server Starting...");
        
        // Start audio system
        if (!audio_system_.start()) {
            Logger::error("Failed to start audio system");
            running_ = false;
            return;
        }
        
        // Start WebRTC server
        if (!webrtc_server_->start()) {
            Logger::error("Failed to start WebRTC server");
            audio_system_.stop();
            running_ = false;
            return;
        }
        
        // Start HTTP server (this blocks)
        http_server_.run();
    }
    
    void stop() {
        if (!running_) {
            return;
        }
        
        Logger::info("Stopping OneStopRadio Server...");
        
        // Stop audio system first
        audio_system_.stop();
        
        // Stop video streaming
        video_manager_.stop_live_stream();
        
        // Stop servers
        webrtc_server_->stop();
        http_server_.stop();
        
        // Stop radio control system
        if (radio_control_) {
            radio_control_->shutdown();
        }
        
        // Stop all audio streams
        stream_manager_.stop_all_streams();
        
        running_ = false;
        Logger::info("Server stopped");
    }

private:
    void setup_api_routes() {
        // Server status
        http_server_.add_route("/api/status", [this](const HttpRequest& req) {
            json response = {
                {"status", "running"},
                {"audio_system", audio_system_.is_running()},
                {"audio_channels", audio_system_.get_active_channels().size()},
                {"audio_streaming", audio_system_.is_streaming()},
                {"audio_recording", audio_system_.is_recording()},
                {"video_streaming", video_manager_.is_live()},
                {"webrtc_connections", webrtc_server_->get_connection_count()}
            };
            return response.dump();
        });
        
        // Video streaming controls
        http_server_.add_route("/api/video/camera/on", [this](const HttpRequest& req) {
            bool success = video_manager_.switch_to_camera();
            json response = {{"success", success}, {"source", "camera"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/video/camera/off", [this](const HttpRequest& req) {
            bool success = video_manager_.switch_to_off();
            json response = {{"success", success}, {"source", "off"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/video/image", [this](const HttpRequest& req) {
            // Parse image path from request body
            json body = json::parse(req.body);
            std::string image_path = body.value("image_path", "");
            
            bool success = video_manager_.switch_to_image(image_path);
            json response = {
                {"success", success}, 
                {"source", "image"}, 
                {"image_path", image_path}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/video/slideshow/start", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            
            SlideShowConfig config;
            if (body.contains("images")) {
                for (const auto& img : body["images"]) {
                    config.image_paths.push_back(img.get<std::string>());
                }
            }
            config.slide_duration_seconds = body.value("duration", 5);
            config.loop = body.value("loop", true);
            config.transition_effect = body.value("transition", "fade");
            
            bool success = video_manager_.switch_to_slideshow(config);
            json response = {
                {"success", success}, 
                {"source", "slideshow"},
                {"image_count", config.image_paths.size()}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/video/slideshow/stop", [this](const HttpRequest& req) {
            video_manager_.get_composer().stop_slideshow();
            json response = {{"success", true}, {"action", "slideshow_stopped"}};
            return response.dump();
        });
        
        // Social media streaming
        http_server_.add_route("/api/video/stream/youtube", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string stream_key = body.value("stream_key", "");
            std::string title = body.value("title", "OneStopRadio Live");
            
            bool success = video_manager_.setup_youtube_stream(stream_key, title);
            json response = {{"success", success}, {"platform", "youtube"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/video/stream/twitch", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string stream_key = body.value("stream_key", "");
            std::string title = body.value("title", "OneStopRadio DJ Set");
            
            bool success = video_manager_.setup_twitch_stream(stream_key, title);
            json response = {{"success", success}, {"platform", "twitch"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/video/stream/facebook", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string stream_key = body.value("stream_key", "");
            std::string title = body.value("title", "Live Radio Show");
            
            bool success = video_manager_.setup_facebook_stream(stream_key, title);
            json response = {{"success", success}, {"platform", "facebook"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/video/stream/start", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::vector<std::string> platforms;
            
            if (body.contains("platforms")) {
                for (const auto& platform : body["platforms"]) {
                    platforms.push_back(platform.get<std::string>());
                }
            }
            
            bool success = video_manager_.start_live_stream(platforms);
            json response = {
                {"success", success}, 
                {"action", "stream_started"},
                {"platforms", platforms}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/video/stream/stop", [this](const HttpRequest& req) {
            bool success = video_manager_.stop_live_stream();
            json response = {{"success", success}, {"action", "stream_stopped"}};
            return response.dump();
        });
        
        // Video overlay controls
        http_server_.add_route("/api/video/overlay/text", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string text = body.value("text", "");
            int x = body.value("x", 50);
            int y = body.value("y", 50);
            std::string font = body.value("font", "Arial");
            int font_size = body.value("font_size", 24);
            
            bool success = video_manager_.get_composer().add_text_overlay(text, x, y, font, font_size);
            json response = {{"success", success}, {"overlay", "text_added"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/video/overlay/clear", [this](const HttpRequest& req) {
            bool success = video_manager_.get_composer().remove_text_overlay();
            json response = {{"success", success}, {"overlay", "text_removed"}};
            return response.dump();
        });
        
        // ===========================================
        // RADIO CONTROL API ENDPOINTS
        // ===========================================
        
        // ===== TRACK MANAGEMENT =====
        
        http_server_.add_route("/api/radio/tracks/add", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string file_path = body.value("file_path", "");
                json metadata = body.value("metadata", json::object());
                
                std::string track_id = radio_control_->add_track(file_path, metadata);
                json response = {
                    {"success", !track_id.empty()},
                    {"track_id", track_id},
                    {"message", track_id.empty() ? "Failed to add track" : "Track added successfully"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/tracks/remove", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string track_id = body.value("track_id", "");
                
                bool success = radio_control_->remove_track(track_id);
                json response = {
                    {"success", success},
                    {"message", success ? "Track removed successfully" : "Failed to remove track"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/tracks/update", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string track_id = body.value("track_id", "");
                json metadata = body.value("metadata", json::object());
                
                bool success = radio_control_->update_track_metadata(track_id, metadata);
                json response = {
                    {"success", success},
                    {"message", success ? "Track updated successfully" : "Failed to update track"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/tracks/list", [this](const HttpRequest& req) {
            try {
                auto tracks = radio_control_->get_all_tracks();
                json tracks_json = json::array();
                
                for (const auto& track : tracks) {
                    tracks_json.push_back(track.to_json());
                }
                
                json response = {
                    {"success", true},
                    {"tracks", tracks_json},
                    {"count", tracks.size()}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/tracks/search", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string query = body.value("query", "");
                
                auto tracks = radio_control_->search_tracks(query);
                json tracks_json = json::array();
                
                for (const auto& track : tracks) {
                    tracks_json.push_back(track.to_json());
                }
                
                json response = {
                    {"success", true},
                    {"tracks", tracks_json},
                    {"query", query},
                    {"count", tracks.size()}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/tracks/analyze", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string track_id = body.value("track_id", "");
                
                bool success = radio_control_->analyze_track(track_id);
                json response = {
                    {"success", success},
                    {"message", success ? "Track analysis completed" : "Failed to analyze track"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // ===== DECK OPERATIONS =====
        
        http_server_.add_route("/api/radio/deck/load", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string deck_id = body.value("deck_id", "");
                std::string track_id = body.value("track_id", "");
                
                bool success = radio_control_->load_track_to_deck(deck_id, track_id);
                json response = {
                    {"success", success},
                    {"deck_id", deck_id},
                    {"track_id", track_id},
                    {"message", success ? "Track loaded to deck" : "Failed to load track to deck"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/deck/unload", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string deck_id = body.value("deck_id", "");
                
                bool success = radio_control_->unload_deck(deck_id);
                json response = {
                    {"success", success},
                    {"deck_id", deck_id},
                    {"message", success ? "Deck unloaded" : "Failed to unload deck"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/deck/play", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string deck_id = body.value("deck_id", "");
                
                bool success = radio_control_->play_deck(deck_id);
                json response = {
                    {"success", success},
                    {"deck_id", deck_id},
                    {"action", "play"},
                    {"message", success ? "Deck playback started" : "Failed to start deck playback"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/deck/pause", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string deck_id = body.value("deck_id", "");
                
                bool success = radio_control_->pause_deck(deck_id);
                json response = {
                    {"success", success},
                    {"deck_id", deck_id},
                    {"action", "pause"},
                    {"message", success ? "Deck playback paused" : "Failed to pause deck playback"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/deck/stop", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string deck_id = body.value("deck_id", "");
                
                bool success = radio_control_->stop_deck(deck_id);
                json response = {
                    {"success", success},
                    {"deck_id", deck_id},
                    {"action", "stop"},
                    {"message", success ? "Deck playback stopped" : "Failed to stop deck playback"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/deck/status", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string deck_id = body.value("deck_id", "");
                
                DJDeck* deck = radio_control_->get_deck(deck_id);
                if (deck) {
                    json response = {
                        {"success", true},
                        {"deck", deck->to_json()}
                    };
                    return response.dump();
                } else {
                    json response = {
                        {"success", false},
                        {"error", "Deck not found"}
                    };
                    return response.dump();
                }
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/deck/volume", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string deck_id = body.value("deck_id", "");
                float volume = body.value("volume", 1.0f);
                
                bool success = radio_control_->set_deck_volume(deck_id, volume);
                json response = {
                    {"success", success},
                    {"deck_id", deck_id},
                    {"volume", volume},
                    {"message", success ? "Deck volume updated" : "Failed to update deck volume"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // ===== MIXER CONTROLS =====
        
        http_server_.add_route("/api/radio/mixer/crossfader", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                float position = body.value("position", 0.0f);
                
                bool success = radio_control_->set_crossfader_position(position);
                json response = {
                    {"success", success},
                    {"crossfader_position", position},
                    {"message", success ? "Crossfader position updated" : "Failed to update crossfader"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/mixer/master_volume", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                float volume = body.value("volume", 0.8f);
                
                bool success = radio_control_->set_master_volume(volume);
                json response = {
                    {"success", success},
                    {"master_volume", volume},
                    {"message", success ? "Master volume updated" : "Failed to update master volume"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/mixer/status", [this](const HttpRequest& req) {
            try {
                json mixer_status = radio_control_->get_mixer_status();
                json response = {
                    {"success", true},
                    {"mixer", mixer_status}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // ===== MICROPHONE AND TALKOVER CONTROL =====
        
        // Start microphone (matches frontend expectations)
        http_server_.add_route("/api/mixer/microphone/start", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                float gain = body.value("gain", 70.0f);
                std::string device_id = body.value("device_id", "");
                
                // Enable microphone input with enhanced configuration
                bool success = audio_system_.enable_microphone_input(true);
                if (success) {
                    // Set microphone gain (convert from percentage to linear)
                    success = audio_system_.set_microphone_gain(gain / 100.0f);
                }
                
                json response = {
                    {"success", success},
                    {"enabled", true},
                    {"gain", gain},
                    {"device_id", device_id},
                    {"message", success ? "Microphone started successfully" : "Failed to start microphone"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Stop microphone (matches frontend expectations)
        http_server_.add_route("/api/mixer/microphone/stop", [this](const HttpRequest& req) {
            try {
                bool success = audio_system_.enable_microphone_input(false);
                
                json response = {
                    {"success", success},
                    {"enabled", false},
                    {"message", success ? "Microphone stopped successfully" : "Failed to stop microphone"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Toggle microphone (legacy compatibility)
        http_server_.add_route("/api/mixer/microphone/toggle", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                bool enabled = body.value("enabled", false);
                
                bool success = audio_system_.enable_microphone_input(enabled);
                json response = {
                    {"success", success},
                    {"enabled", enabled},
                    {"message", success ? (enabled ? "Microphone enabled" : "Microphone disabled") : "Failed to toggle microphone"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Enable/disable microphone (legacy compatibility)
        http_server_.add_route("/api/radio/microphone/enable", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                bool enabled = body.value("enabled", false);
                
                bool success = radio_control_->enable_microphone(enabled);
                json response = {
                    {"success", success},
                    {"enabled", enabled},
                    {"message", success ? (enabled ? "Microphone enabled" : "Microphone disabled") : "Failed to toggle microphone"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Set microphone gain (matches frontend expectations)
        http_server_.add_route("/api/mixer/microphone/gain", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                float gain = body.value("gain", 70.0f);
                
                // Convert percentage to linear gain
                bool success = audio_system_.set_microphone_gain(gain / 100.0f);
                json response = {
                    {"success", success},
                    {"gain", gain},
                    {"message", success ? "Microphone gain updated" : "Failed to update microphone gain"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Mute/unmute microphone (matches frontend expectations)
        http_server_.add_route("/api/mixer/microphone/mute", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                bool muted = body.value("muted", false);
                
                bool success = audio_system_.set_microphone_mute(muted);
                json response = {
                    {"success", success},
                    {"muted", muted},
                    {"message", success ? (muted ? "Microphone muted" : "Microphone unmuted") : "Failed to toggle microphone mute"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Set microphone gain (legacy compatibility)
        http_server_.add_route("/api/radio/microphone/gain", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                float gain = body.value("gain", 1.0f);
                
                bool success = radio_control_->set_microphone_gain(gain);
                json response = {
                    {"success", success},
                    {"gain", gain},
                    {"message", success ? "Microphone gain updated" : "Failed to update microphone gain"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Mute/unmute microphone
        http_server_.add_route("/api/radio/microphone/mute", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                bool muted = body.value("muted", false);
                
                bool success = radio_control_->set_microphone_mute(muted);
                json response = {
                    {"success", success},
                    {"muted", muted},
                    {"message", success ? (muted ? "Microphone muted" : "Microphone unmuted") : "Failed to toggle microphone mute"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Get microphone status
        http_server_.add_route("/api/radio/microphone/status", [this](const HttpRequest& req) {
            try {
                json response = {
                    {"success", true},
                    {"enabled", radio_control_->is_microphone_enabled()},
                    {"muted", radio_control_->is_microphone_muted()},
                    {"gain", radio_control_->get_microphone_gain()},
                    {"level", radio_control_->get_real_time_levels().microphone_level}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Enable/disable talkover
        http_server_.add_route("/api/radio/talkover/enable", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                bool enabled = body.value("enabled", false);
                
                bool success = radio_control_->enable_talkover(enabled);
                json response = {
                    {"success", success},
                    {"enabled", enabled},
                    {"duck_level", radio_control_->get_talkover_duck_level()},
                    {"message", success ? (enabled ? "Talkover enabled - Audio ducked" : "Talkover disabled - Audio restored") : "Failed to toggle talkover"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Set talkover duck level
        http_server_.add_route("/api/radio/talkover/duck_level", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                float duck_level = body.value("duck_level", 0.25f);
                
                bool success = radio_control_->set_talkover_duck_level(duck_level);
                json response = {
                    {"success", success},
                    {"duck_level", duck_level},
                    {"message", success ? "Talkover duck level updated" : "Failed to update duck level"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Get talkover status
        http_server_.add_route("/api/radio/talkover/status", [this](const HttpRequest& req) {
            try {
                json response = {
                    {"success", true},
                    {"active", radio_control_->is_talkover_active()},
                    {"duck_level", radio_control_->get_talkover_duck_level()}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Get real-time audio levels for VU meters
        http_server_.add_route("/api/radio/audio/levels", [this](const HttpRequest& req) {
            try {
                auto levels = radio_control_->get_real_time_levels();
                json response = {
                    {"success", true},
                    {"levels", {
                        {"left_peak", levels.left_peak},
                        {"right_peak", levels.right_peak},
                        {"left_rms", levels.left_rms},
                        {"right_rms", levels.right_rms},
                        {"microphone_level", levels.microphone_level},
                        {"is_clipping", levels.is_clipping},
                        {"is_ducked", levels.is_ducked},
                        {"timestamp", levels.timestamp_ms}
                    }}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Get waveform data for a deck
        http_server_.add_route("/api/radio/deck/waveform/{deck_id}", [this](const HttpRequest& req) {
            try {
                std::string deck_id = req.path_params.at("deck_id");
                auto waveform = radio_control_->get_deck_waveform(deck_id);
                
                json response = {
                    {"success", true},
                    {"deck_id", deck_id},
                    {"waveform", {
                        {"peaks", waveform.peaks},
                        {"rms", waveform.rms},
                        {"duration_ms", waveform.duration_ms},
                        {"current_position_ms", waveform.current_position_ms},
                        {"sample_rate", waveform.sample_rate},
                        {"samples_per_pixel", waveform.samples_per_pixel}
                    }}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Load audio file into channel (matches frontend expectations)
        http_server_.add_route("/api/mixer/channel/A/load", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string track_url = body.value("track_url", "");
                
                if (track_url.empty()) {
                    json response = {
                        {"success", false},
                        {"error", "track_url is required"}
                    };
                    return response.dump();
                }
                
                bool success = audio_system_.load_audio_file("A", track_url);
                json response = {
                    {"success", success},
                    {"channel_id", "A"},
                    {"track_url", track_url},
                    {"message", success ? "Track loaded into channel A" : "Failed to load track into channel A"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/mixer/channel/B/load", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string track_url = body.value("track_url", "");
                
                if (track_url.empty()) {
                    json response = {
                        {"success", false},
                        {"error", "track_url is required"}
                    };
                    return response.dump();
                }
                
                bool success = audio_system_.load_audio_file("B", track_url);
                json response = {
                    {"success", success},
                    {"channel_id", "B"},
                    {"track_url", track_url},
                    {"message", success ? "Track loaded into channel B" : "Failed to load track into channel B"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Channel playback control (matches frontend expectations)
        http_server_.add_route("/api/mixer/channel/A/playback", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                bool play = body.value("play", false);
                
                bool success = audio_system_.set_channel_playback("A", play);
                json response = {
                    {"success", success},
                    {"channel_id", "A"},
                    {"playing", play},
                    {"message", success ? (play ? "Channel A playback started" : "Channel A playback stopped") : "Failed to control channel A playback"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/mixer/channel/B/playback", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                bool play = body.value("play", false);
                
                bool success = audio_system_.set_channel_playback("B", play);
                json response = {
                    {"success", success},
                    {"channel_id", "B"},
                    {"playing", play},
                    {"message", success ? (play ? "Channel B playback started" : "Channel B playback stopped") : "Failed to control channel B playback"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Mixer status endpoint (matches frontend expectations)
        http_server_.add_route("/api/mixer/status", [this](const HttpRequest& req) {
            try {
                AudioLevels master_levels = audio_system_.get_master_audio_levels();
                float microphone_level = audio_system_.get_microphone_level();
                
                json response = {
                    {"success", true},
                    {"data", {
                        {"masterVolume", 0.8f}, // Default value
                        {"crossfader", 0.0f},   // Default value
                        {"channelA", {
                            {"volume", 0.75f},
                            {"bass", 0.0f},
                            {"mid", 0.0f},
                            {"treble", 0.0f}
                        }},
                        {"channelB", {
                            {"volume", 0.75f},
                            {"bass", 0.0f},
                            {"mid", 0.0f},
                            {"treble", 0.0f}
                        }},
                        {"microphone", {
                            {"isEnabled", audio_system_.is_microphone_enabled()},
                            {"isActive", audio_system_.is_microphone_enabled()},
                            {"isMuted", false},
                            {"gain", 70.0f}
                        }},
                        {"levels", {
                            {"left", master_levels.left_peak * 100.0f},
                            {"right", master_levels.right_peak * 100.0f}
                        }}
                    }}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // Load audio file into channel (legacy compatibility)
        http_server_.add_route("/api/radio/audio/load", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string channel_id = body.value("channel_id", "A");
                std::string file_path = body.value("file_path", "");
                
                if (file_path.empty()) {
                    json response = {
                        {"success", false},
                        {"error", "file_path is required"},
                        {"action", "load_audio_file"}
                    };
                    return response.dump();
                }
                
                bool success = radio_control_->load_audio_file(channel_id, file_path);
                json response = {
                    {"success", success},
                    {"action", "load_audio_file"},
                    {"channel_id", channel_id},
                    {"file_path", file_path}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {
                    {"success", false},
                    {"error", e.what()},
                    {"action", "load_audio_file"}
                };
                return response.dump();
            }
        });

        // Channel playback control
        http_server_.add_route("/api/radio/channel/play", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string channel_id = body.value("channel_id", "A");
                bool play = body.value("play", true);
                
                bool success = radio_control_->set_channel_playback(channel_id, play);
                json response = {
                    {"success", success},
                    {"action", "channel_playback"},
                    {"channel_id", channel_id},
                    {"playing", play}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {
                    {"success", false},
                    {"error", e.what()},
                    {"action", "channel_playback"}
                };
                return response.dump();
            }
        });

        // Channel volume control
        http_server_.add_route("/api/radio/channel/volume", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string channel_id = body.value("channel_id", "A");
                float volume = body.value("volume", 0.75f);
                
                bool success = radio_control_->set_channel_volume(channel_id, volume);
                json response = {
                    {"success", success},
                    {"action", "channel_volume"},
                    {"channel_id", channel_id},
                    {"volume", volume}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {
                    {"success", false},
                    {"error", e.what()},
                    {"action", "channel_volume"}
                };
                return response.dump();
            }
        });

        // Channel EQ control
        http_server_.add_route("/api/radio/channel/eq", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string channel_id = body.value("channel_id", "A");
                float bass = body.value("bass", 0.0f);
                float mid = body.value("mid", 0.0f);
                float treble = body.value("treble", 0.0f);
                
                bool success = radio_control_->set_channel_eq(channel_id, bass, mid, treble);
                json response = {
                    {"success", success},
                    {"action", "channel_eq"},
                    {"channel_id", channel_id},
                    {"eq", {
                        {"bass", bass},
                        {"mid", mid},
                        {"treble", treble}
                    }}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {
                    {"success", false},
                    {"error", e.what()},
                    {"action", "channel_eq"}
                };
                return response.dump();
            }
        });

        // Start/stop audio monitoring
        http_server_.add_route("/api/radio/audio/monitoring", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                bool enabled = body.value("enabled", false);
                
                bool success = enabled ? radio_control_->start_audio_monitoring() : radio_control_->stop_audio_monitoring();
                json response = {
                    {"success", success},
                    {"monitoring", enabled},
                    {"message", success ? (enabled ? "Audio monitoring started" : "Audio monitoring stopped") : "Failed to toggle audio monitoring"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // ===== RADIO STATION CONTROL =====
        
        http_server_.add_route("/api/radio/station/start_broadcast", [this](const HttpRequest& req) {
            try {
                bool success = radio_control_->start_broadcast();
                json response = {
                    {"success", success},
                    {"action", "start_broadcast"},
                    {"message", success ? "Broadcast started successfully" : "Failed to start broadcast"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/station/stop_broadcast", [this](const HttpRequest& req) {
            try {
                bool success = radio_control_->stop_broadcast();
                json response = {
                    {"success", success},
                    {"action", "stop_broadcast"},
                    {"message", success ? "Broadcast stopped successfully" : "Failed to stop broadcast"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/station/update_metadata", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string artist = body.value("artist", "");
                std::string title = body.value("title", "");
                
                bool success = radio_control_->update_stream_metadata(artist, title);
                json response = {
                    {"success", success},
                    {"artist", artist},
                    {"title", title},
                    {"message", success ? "Stream metadata updated" : "Failed to update metadata"}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/station/config", [this](const HttpRequest& req) {
            try {
                if (req.method == "GET") {
                    RadioStation config = radio_control_->get_station_config();
                    json response = {
                        {"success", true},
                        {"station", config.to_json()}
                    };
                    return response.dump();
                } else if (req.method == "POST") {
                    json body = json::parse(req.body);
                    RadioStation config;
                    // Parse station config from body
                    config.name = body.value("name", "OneStopRadio");
                    config.description = body.value("description", "");
                    config.genre = body.value("genre", "");
                    // ... more config parsing
                    
                    bool success = radio_control_->configure_station(config);
                    json response = {
                        {"success", success},
                        {"message", success ? "Station config updated" : "Failed to update station config"}
                    };
                    return response.dump();
                }
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
            
            json response = {{"success", false}, {"error", "Invalid request method"}};
            return response.dump();
        });
        
        // ===== AUDIO LEVELS AND MONITORING =====
        
        http_server_.add_route("/api/radio/levels/master", [this](const HttpRequest& req) {
            try {
                auto levels = radio_control_->get_master_levels();
                json response = {
                    {"success", true},
                    {"levels", {
                        {"left_peak", levels.left_peak},
                        {"right_peak", levels.right_peak},
                        {"left_rms", levels.left_rms},
                        {"right_rms", levels.right_rms},
                        {"clipping", levels.clipping}
                    }}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/radio/levels/deck", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string deck_id = body.value("deck_id", "");
                
                auto levels = radio_control_->get_deck_levels(deck_id);
                json response = {
                    {"success", true},
                    {"deck_id", deck_id},
                    {"levels", {
                        {"left_peak", levels.left_peak},
                        {"right_peak", levels.right_peak},
                        {"left_rms", levels.left_rms},
                        {"right_rms", levels.right_rms},
                        {"clipping", levels.clipping}
                    }}
                };
                return response.dump();
            } catch (const std::exception& e) {
                json response = {{"success", false}, {"error", e.what()}};
                return response.dump();
            }
        });
        
        // ===========================================
        // LEGACY AUDIO API ENDPOINTS (PRESERVED)
        // ===========================================
        
        // Audio devices
        http_server_.add_route("/api/audio/devices/input", [this](const HttpRequest& req) {
            auto devices = audio_system_.get_input_devices();
            json response = {{"success", true}, {"devices", devices}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/devices/output", [this](const HttpRequest& req) {
            auto devices = audio_system_.get_output_devices();
            json response = {{"success", true}, {"devices", devices}};
            return response.dump();
        });
        
        // Microphone controls
        http_server_.add_route("/api/audio/microphone/enable", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            
            MicrophoneConfig config;
            config.enabled = true;
            config.gain = body.value("gain", 1.0f);
            config.gate_threshold = body.value("gate_threshold", -40.0f);
            config.noise_suppression = body.value("noise_suppression", true);
            config.echo_cancellation = body.value("echo_cancellation", true);
            config.auto_gain_control = body.value("auto_gain_control", false);
            config.device_id = body.value("device_id", 0);
            
            bool success = audio_system_.enable_microphone(config);
            json response = {
                {"success", success}, 
                {"microphone", "enabled"},
                {"config", {
                    {"gain", config.gain},
                    {"gate_threshold", config.gate_threshold},
                    {"noise_suppression", config.noise_suppression},
                    {"echo_cancellation", config.echo_cancellation},
                    {"device_id", config.device_id}
                }}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/microphone/disable", [this](const HttpRequest& req) {
            bool success = audio_system_.disable_microphone();
            json response = {{"success", success}, {"microphone", "disabled"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/microphone/gain", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            float gain = body.value("gain", 1.0f);
            
            bool success = audio_system_.set_microphone_gain(gain);
            json response = {{"success", success}, {"gain", gain}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/microphone/config", [this](const HttpRequest& req) {
            auto config = audio_system_.get_microphone_config();
            json response = {
                {"success", true}, 
                {"config", {
                    {"enabled", config.enabled},
                    {"gain", config.gain},
                    {"gate_threshold", config.gate_threshold},
                    {"noise_suppression", config.noise_suppression},
                    {"echo_cancellation", config.echo_cancellation},
                    {"auto_gain_control", config.auto_gain_control},
                    {"device_id", config.device_id}
                }}
            };
            return response.dump();
        });
        
        // Audio channels
        http_server_.add_route("/api/audio/channels/create", [this](const HttpRequest& req) {
            std::string channel_id = audio_system_.create_audio_channel();
            json response = {{"success", !channel_id.empty()}, {"channel_id", channel_id}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/channels/list", [this](const HttpRequest& req) {
            auto channels = audio_system_.get_active_channels();
            json response = {{"success", true}, {"channels", channels}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/channel/load", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string channel_id = body.value("channel_id", "");
            std::string file_path = body.value("file_path", "");
            
            bool success = audio_system_.load_audio_file(channel_id, file_path);
            json response = {{"success", success}, {"channel_id", channel_id}, {"file_path", file_path}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/channel/play", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string channel_id = body.value("channel_id", "");
            
            bool success = audio_system_.play_channel(channel_id);
            json response = {{"success", success}, {"channel_id", channel_id}, {"action", "play"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/channel/pause", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string channel_id = body.value("channel_id", "");
            
            bool success = audio_system_.pause_channel(channel_id);
            json response = {{"success", success}, {"channel_id", channel_id}, {"action", "pause"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/channel/stop", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string channel_id = body.value("channel_id", "");
            
            bool success = audio_system_.stop_channel(channel_id);
            json response = {{"success", success}, {"channel_id", channel_id}, {"action", "stop"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/channel/volume", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string channel_id = body.value("channel_id", "");
            float volume = body.value("volume", 1.0f);
            
            bool success = audio_system_.set_channel_volume(channel_id, volume);
            json response = {{"success", success}, {"channel_id", channel_id}, {"volume", volume}};
            return response.dump();
        });
        
        // Master controls
        http_server_.add_route("/api/audio/master/volume", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            float volume = body.value("volume", 0.8f);
            
            bool success = audio_system_.set_master_volume(volume);
            json response = {{"success", success}, {"master_volume", volume}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/crossfader", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            float position = body.value("position", 0.0f);
            
            bool success = audio_system_.set_crossfader_position(position);
            json response = {{"success", success}, {"crossfader_position", position}};
            return response.dump();
        });
        
        // Audio levels and monitoring
        http_server_.add_route("/api/audio/levels/master", [this](const HttpRequest& req) {
            AudioLevels levels = audio_system_.get_master_levels();
            json response = {
                {"success", true},
                {"levels", {
                    {"left_peak", levels.left_peak},
                    {"right_peak", levels.right_peak},
                    {"left_rms", levels.left_rms},
                    {"right_rms", levels.right_rms},
                    {"left_db", levels.left_db},
                    {"right_db", levels.right_db},
                    {"clipping", levels.clipping},
                    {"timestamp", levels.timestamp}
                }}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/levels/microphone", [this](const HttpRequest& req) {
            AudioLevels levels = audio_system_.get_microphone_levels();
            json response = {
                {"success", true},
                {"levels", {
                    {"left_peak", levels.left_peak},
                    {"right_peak", levels.right_peak},
                    {"left_rms", levels.left_rms},
                    {"right_rms", levels.right_rms},
                    {"left_db", levels.left_db},
                    {"right_db", levels.right_db},
                    {"clipping", levels.clipping},
                    {"timestamp", levels.timestamp}
                }}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/levels/channel", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string channel_id = body.value("channel_id", "");
            
            AudioLevels levels = audio_system_.get_channel_levels(channel_id);
            json response = {
                {"success", true},
                {"channel_id", channel_id},
                {"levels", {
                    {"left_peak", levels.left_peak},
                    {"right_peak", levels.right_peak},
                    {"left_rms", levels.left_rms},
                    {"right_rms", levels.right_rms},
                    {"left_db", levels.left_db},
                    {"right_db", levels.right_db},
                    {"clipping", levels.clipping},
                    {"timestamp", levels.timestamp}
                }}
            };
            return response.dump();
        });
        
        // Legacy endpoint for compatibility
        http_server_.add_route("/api/audio/levels", [this](const HttpRequest& req) {
            AudioLevels master_levels = audio_system_.get_master_levels();
            AudioLevels mic_levels = audio_system_.get_microphone_levels();
            
            // Convert to percentage for legacy compatibility
            float left_pct = std::max(0.0f, std::min(100.0f, (master_levels.left_db + 60.0f) / 60.0f * 100.0f));
            float right_pct = std::max(0.0f, std::min(100.0f, (master_levels.right_db + 60.0f) / 60.0f * 100.0f));
            float mic_pct = std::max(0.0f, std::min(100.0f, (mic_levels.left_db + 60.0f) / 60.0f * 100.0f));
            
            json response = {
                {"success", true},
                {"levels", {
                    {"left", left_pct},
                    {"right", right_pct},
                    {"microphone", mic_pct},
                    {"timestamp", master_levels.timestamp}
                }}
            };
            return response.dump();
        });
        
        // Audio streaming
        http_server_.add_route("/api/audio/stream/start", [this](const HttpRequest& req) {
            bool success = audio_system_.start_streaming();
            json response = {{"success", success}, {"action", "stream_started"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/stream/stop", [this](const HttpRequest& req) {
            bool success = audio_system_.stop_streaming();
            json response = {{"success", success}, {"action", "stream_stopped"}};
            return response.dump();
        });
        
        // Streaming encoder endpoints
        http_server_.add_route("/api/audio/stream/connect", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                
                // Create stream config from request
                StreamConfig config;
                config.protocol = body.value("protocol", "icecast2") == "icecast2" ? 
                    StreamProtocol::ICECAST2 : StreamProtocol::SHOUTCAST;
                config.server_host = body.value("serverHost", "localhost");
                config.server_port = body.value("serverPort", 8000);
                config.mount_point = body.value("mountPoint", "/stream.mp3");
                config.password = body.value("password", "hackme");
                config.username = body.value("username", "source");
                config.stream_name = body.value("streamName", "OneStopRadio");
                config.stream_description = body.value("streamDescription", "Live DJ Stream");
                config.stream_genre = body.value("streamGenre", "Electronic");
                config.stream_url = body.value("streamUrl", "");
                
                std::string codec_str = body.value("codec", "mp3");
                if (codec_str == "mp3") config.codec = StreamCodec::MP3;
                else if (codec_str == "ogg_vorbis") config.codec = StreamCodec::OGG_VORBIS;
                else if (codec_str == "ogg_opus") config.codec = StreamCodec::OGG_OPUS;
                else if (codec_str == "aac") config.codec = StreamCodec::AAC;
                
                config.bitrate = body.value("bitrate", 128);
                config.sample_rate = body.value("sampleRate", 44100);
                config.channels = body.value("channels", 2);
                config.quality = body.value("quality", 5);
                config.public_stream = body.value("publicStream", true);
                config.enable_metadata = body.value("enableMetadata", true);
                
                bool success = audio_encoder_.configure(config);
                if (success) {
                    success = audio_encoder_.connect();
                }
                
                json response = {
                    {"success", success},
                    {"action", "stream_connect"},
                    {"status", success ? "connected" : "failed"}
                };
                return response.dump();
                
            } catch (const std::exception& e) {
                json response = {
                    {"success", false},
                    {"error", e.what()},
                    {"action", "stream_connect"}
                };
                return response.dump();
            }
        });
        
        http_server_.add_route("/api/audio/stream/disconnect", [this](const HttpRequest& req) {
            bool success = audio_encoder_.disconnect();
            json response = {
                {"success", success},
                {"action", "stream_disconnect"},
                {"status", "disconnected"}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/stream/start", [this](const HttpRequest& req) {
            bool success = audio_encoder_.start_streaming();
            json response = {
                {"success", success},
                {"action", "streaming_start"},
                {"status", success ? "streaming" : "failed"}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/stream/stop", [this](const HttpRequest& req) {
            bool success = audio_encoder_.stop_streaming();
            json response = {
                {"success", success},
                {"action", "streaming_stop"},
                {"status", "connected"}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/stream/status", [this](const HttpRequest& req) {
            StreamStats stats = audio_encoder_.get_statistics();
            StreamStatus status = audio_encoder_.get_status();
            
            std::string status_str;
            switch (status) {
                case StreamStatus::DISCONNECTED: status_str = "disconnected"; break;
                case StreamStatus::CONNECTING: status_str = "connecting"; break;
                case StreamStatus::CONNECTED: status_str = "connected"; break;
                case StreamStatus::STREAMING: status_str = "streaming"; break;
                case StreamStatus::ERROR: status_str = "error"; break;
                default: status_str = "unknown"; break;
            }
            
            json response = {
                {"success", true},
                {"stats", {
                    {"status", status_str},
                    {"statusMessage", audio_encoder_.get_status_message()},
                    {"connectedTime", stats.connected_time_ms},
                    {"bytesSent", stats.bytes_sent},
                    {"currentBitrate", stats.current_bitrate},
                    {"peakLevelLeft", stats.peak_level_left},
                    {"peakLevelRight", stats.peak_level_right},
                    {"currentListeners", stats.current_listeners},
                    {"reconnectCount", stats.reconnect_count}
                }}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/stream/metadata", [this](const HttpRequest& req) {
            try {
                json body = json::parse(req.body);
                std::string artist = body.value("artist", "");
                std::string title = body.value("title", "");
                
                bool success = audio_encoder_.update_metadata(artist, title);
                json response = {
                    {"success", success},
                    {"action", "metadata_update"},
                    {"artist", artist},
                    {"title", title}
                };
                return response.dump();
                
            } catch (const std::exception& e) {
                json response = {
                    {"success", false},
                    {"error", e.what()},
                    {"action", "metadata_update"}
                };
                return response.dump();
            }
        });
        
        // Audio recording
        http_server_.add_route("/api/audio/record/start", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string output_file = body.value("output_file", "recording.wav");
            
            bool success = audio_system_.start_recording(output_file);
            json response = {{"success", success}, {"output_file", output_file}, {"action", "record_started"}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/record/stop", [this](const HttpRequest& req) {
            bool success = audio_system_.stop_recording();
            json response = {{"success", success}, {"action", "record_stopped"}};
            return response.dump();
        });
        
        // Audio effects
        http_server_.add_route("/api/audio/effects/reverb", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            bool enabled = body.value("enabled", false);
            float room_size = body.value("room_size", 0.5f);
            float damping = body.value("damping", 0.5f);
            float wet_level = body.value("wet_level", 0.3f);
            
            bool success = audio_system_.enable_reverb(enabled, room_size, damping, wet_level);
            json response = {
                {"success", success}, 
                {"reverb", {
                    {"enabled", enabled},
                    {"room_size", room_size},
                    {"damping", damping},
                    {"wet_level", wet_level}
                }}
            };
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/effects/delay", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            bool enabled = body.value("enabled", false);
            float delay_time = body.value("delay_time", 250.0f);
            float feedback = body.value("feedback", 0.3f);
            float wet_level = body.value("wet_level", 0.3f);
            
            bool success = audio_system_.enable_delay(enabled, delay_time, feedback, wet_level);
            json response = {
                {"success", success}, 
                {"delay", {
                    {"enabled", enabled},
                    {"delay_time", delay_time},
                    {"feedback", feedback},
                    {"wet_level", wet_level}
                }}
            };
            return response.dump();
        });
        
        // BPM detection and sync
        http_server_.add_route("/api/audio/bpm/detect", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string channel_id = body.value("channel_id", "");
            
            float bpm = audio_system_.detect_bpm(channel_id);
            json response = {{"success", true}, {"channel_id", channel_id}, {"bpm", bpm}};
            return response.dump();
        });
        
        http_server_.add_route("/api/audio/bpm/sync", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            std::string channel_a = body.value("channel_a", "");
            std::string channel_b = body.value("channel_b", "");
            
            bool success = audio_system_.enable_bpm_sync(channel_a, channel_b);
            json response = {{"success", success}, {"channel_a", channel_a}, {"channel_b", channel_b}};
            return response.dump();
        });
        
        // Spectrum analyzer
        http_server_.add_route("/api/audio/spectrum", [this](const HttpRequest& req) {
            json body = json::parse(req.body);
            int bins = body.value("bins", 256);
            
            auto spectrum = audio_system_.get_spectrum_data(bins);
            json response = {{"success", true}, {"spectrum", spectrum}, {"bins", bins}};
            return response.dump();
        });
        
        Logger::info("API routes configured");
    }
    
    ConfigManager config_manager_;
    StreamManager stream_manager_;
    VideoStreamManager video_manager_;
    AudioSystem audio_system_;
    AudioStreamEncoder audio_encoder_;
    std::unique_ptr<RadioControl> radio_control_;
    HttpServer http_server_;
    std::unique_ptr<WebRTCServer> webrtc_server_;
    bool running_;
};

// Global server instance for signal handling
static std::unique_ptr<RadioServer> g_server;

void signal_handler(int signal) {
    if (g_server) {
        g_server->stop();
    }
    exit(signal);
}

int main(int argc, char* argv[]) {
    // Setup signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize logging
    Logger::set_level(Logger::Level::INFO);
    Logger::set_log_file("radio_server.log", 10 * 1024 * 1024, true);
    
    // Get config file path
    std::string config_file = "config/config.json";
    if (argc > 1) {
        config_file = argv[1];
    }
    
    try {
        // Create and initialize server
        g_server = std::make_unique<RadioServer>();
        
        if (!g_server->initialize(config_file)) {
            Logger::error("Failed to initialize server");
            return 1;
        }
        
        // Run server
        g_server->run();
        
    } catch (const std::exception& e) {
        Logger::error("Server error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}