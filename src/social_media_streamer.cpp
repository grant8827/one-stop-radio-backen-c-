#include "video_stream_manager.hpp"
#include "logger.hpp"
#include <map>
#include <sstream>

// SocialMediaStreamer Implementation
class SocialMediaStreamer::Impl {
public:
    Impl() {}
    
    bool add_platform(const std::string& platform_id, const SocialMediaConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        platforms_[platform_id] = config;
        stream_stats_[platform_id] = StreamStats{};
        
        Logger::info("SocialMediaStreamer", "Added platform: " + platform_id);
        return true;
    }
    
    bool remove_platform(const std::string& platform_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Stop streaming if active
        stop_streaming_internal(platform_id);
        
        platforms_.erase(platform_id);
        stream_stats_.erase(platform_id);
        
        Logger::info("SocialMediaStreamer", "Removed platform: " + platform_id);
        return true;
    }
    
    bool update_platform_config(const std::string& platform_id, const SocialMediaConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = platforms_.find(platform_id);
        if (it == platforms_.end()) {
            Logger::error("SocialMediaStreamer", "Platform not found: " + platform_id);
            return false;
        }
        
        bool was_streaming = it->second.is_live;
        if (was_streaming) {
            stop_streaming_internal(platform_id);
        }
        
        it->second = config;
        
        if (was_streaming) {
            start_streaming_internal(platform_id);
        }
        
        Logger::info("SocialMediaStreamer", "Updated platform config: " + platform_id);
        return true;
    }
    
    bool start_streaming(const std::string& platform_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return start_streaming_internal(platform_id);
    }
    
    bool stop_streaming(const std::string& platform_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return stop_streaming_internal(platform_id);
    }
    
    bool is_streaming(const std::string& platform_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = platforms_.find(platform_id);
        return it != platforms_.end() && it->second.is_live;
    }
    
    bool start_multi_stream(const std::vector<std::string>& platform_ids) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        bool all_started = true;
        for (const auto& platform_id : platform_ids) {
            if (!start_streaming_internal(platform_id)) {
                all_started = false;
                Logger::error("SocialMediaStreamer", "Failed to start streaming on: " + platform_id);
            }
        }
        
        if (all_started) {
            Logger::info("SocialMediaStreamer", "Multi-stream started on " + 
                        std::to_string(platform_ids.size()) + " platforms");
        }
        
        return all_started;
    }
    
    bool stop_all_streams() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& pair : platforms_) {
            stop_streaming_internal(pair.first);
        }
        
        Logger::info("SocialMediaStreamer", "All streams stopped");
        return true;
    }
    
    bool send_video_data(const uint8_t* video_data, size_t video_size,
                        const uint8_t* audio_data, size_t audio_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        bool success = true;
        for (const auto& pair : platforms_) {
            if (pair.second.is_live) {
                // In real implementation, send to RTMP server
                auto& stats = stream_stats_[pair.first];
                stats.bytes_sent += video_size + audio_size;
                stats.frames_sent++;
                stats.is_connected = true;
                
                // Mock bitrate calculation
                auto now = std::chrono::steady_clock::now();
                if (stats.last_update_time.time_since_epoch().count() > 0) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - stats.last_update_time).count();
                    if (elapsed > 0) {
                        stats.current_bitrate = (video_size + audio_size) * 8.0 / (elapsed / 1000.0);
                    }
                }
                stats.last_update_time = now;
            }
        }
        
        return success;
    }
    
    SocialMediaStreamer::StreamStats get_stream_stats(const std::string& platform_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = stream_stats_.find(platform_id);
        if (it != stream_stats_.end()) {
            return it->second;
        }
        
        return SocialMediaStreamer::StreamStats{};
    }
    
    std::vector<std::string> get_active_streams() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::string> active_streams;
        for (const auto& pair : platforms_) {
            if (pair.second.is_live) {
                active_streams.push_back(pair.first);
            }
        }
        
        return active_streams;
    }
    
    void set_status_callback(SocialMediaStreamer::StatusCallback callback) {
        status_callback_ = std::move(callback);
    }

private:
    bool start_streaming_internal(const std::string& platform_id) {
        auto it = platforms_.find(platform_id);
        if (it == platforms_.end()) {
            Logger::error("SocialMediaStreamer", "Platform not found: " + platform_id);
            return false;
        }
        
        auto& config = it->second;
        
        // Validate configuration
        if (config.rtmp_url.empty() || config.stream_key.empty()) {
            Logger::error("SocialMediaStreamer", "Invalid RTMP configuration for: " + platform_id);
            if (status_callback_) {
                status_callback_(platform_id, false, "Invalid RTMP configuration");
            }
            return false;
        }
        
        // In real implementation, connect to RTMP server
        config.is_live = true;
        auto& stats = stream_stats_[platform_id];
        stats.is_connected = true;
        stats.last_error.clear();
        
        Logger::info("SocialMediaStreamer", "Started streaming to: " + platform_id + 
                    " (" + get_platform_name(config.platform) + ")");
        
        if (status_callback_) {
            status_callback_(platform_id, true, "Stream started successfully");
        }
        
        return true;
    }
    
    bool stop_streaming_internal(const std::string& platform_id) {
        auto it = platforms_.find(platform_id);
        if (it == platforms_.end()) {
            return false;
        }
        
        auto& config = it->second;
        config.is_live = false;
        
        auto& stats = stream_stats_[platform_id];
        stats.is_connected = false;
        stats.current_bitrate = 0.0;
        
        Logger::info("SocialMediaStreamer", "Stopped streaming to: " + platform_id);
        
        if (status_callback_) {
            status_callback_(platform_id, false, "Stream stopped");
        }
        
        return true;
    }
    
    std::string get_platform_name(SocialPlatform platform) {
        switch (platform) {
            case SocialPlatform::YOUTUBE: return "YouTube";
            case SocialPlatform::TWITCH: return "Twitch";
            case SocialPlatform::FACEBOOK: return "Facebook";
            case SocialPlatform::TIKTOK: return "TikTok";
            case SocialPlatform::INSTAGRAM: return "Instagram";
            case SocialPlatform::CUSTOM_RTMP: return "Custom RTMP";
            default: return "Unknown";
        }
    }
    
    std::map<std::string, SocialMediaConfig> platforms_;
    
    struct ExtendedStreamStats : SocialMediaStreamer::StreamStats {
        std::chrono::steady_clock::time_point last_update_time;
    };
    std::map<std::string, ExtendedStreamStats> stream_stats_;
    
    SocialMediaStreamer::StatusCallback status_callback_;
    mutable std::mutex mutex_;
};

SocialMediaStreamer::SocialMediaStreamer() : impl_(std::make_unique<Impl>()) {}
SocialMediaStreamer::~SocialMediaStreamer() = default;

bool SocialMediaStreamer::add_platform(const std::string& platform_id, const SocialMediaConfig& config) {
    return impl_->add_platform(platform_id, config);
}

bool SocialMediaStreamer::remove_platform(const std::string& platform_id) {
    return impl_->remove_platform(platform_id);
}

bool SocialMediaStreamer::update_platform_config(const std::string& platform_id, const SocialMediaConfig& config) {
    return impl_->update_platform_config(platform_id, config);
}

bool SocialMediaStreamer::start_streaming(const std::string& platform_id) {
    return impl_->start_streaming(platform_id);
}

bool SocialMediaStreamer::stop_streaming(const std::string& platform_id) {
    return impl_->stop_streaming(platform_id);
}

bool SocialMediaStreamer::is_streaming(const std::string& platform_id) const {
    return impl_->is_streaming(platform_id);
}

bool SocialMediaStreamer::start_multi_stream(const std::vector<std::string>& platform_ids) {
    return impl_->start_multi_stream(platform_ids);
}

bool SocialMediaStreamer::stop_all_streams() {
    return impl_->stop_all_streams();
}

bool SocialMediaStreamer::send_video_data(const uint8_t* video_data, size_t video_size,
                                        const uint8_t* audio_data, size_t audio_size) {
    return impl_->send_video_data(video_data, video_size, audio_data, audio_size);
}

SocialMediaStreamer::StreamStats SocialMediaStreamer::get_stream_stats(const std::string& platform_id) const {
    return impl_->get_stream_stats(platform_id);
}

std::vector<std::string> SocialMediaStreamer::get_active_streams() const {
    return impl_->get_active_streams();
}

void SocialMediaStreamer::set_status_callback(StatusCallback callback) {
    impl_->set_status_callback(std::move(callback));
}

// SocialPlatformHelper Implementation
std::string SocialPlatformHelper::get_rtmp_url(SocialPlatform platform, const std::string& region) {
    switch (platform) {
        case SocialPlatform::YOUTUBE:
            return "rtmp://a.rtmp.youtube.com/live2";
            
        case SocialPlatform::TWITCH:
            if (!region.empty()) {
                return "rtmp://" + region + ".contribute.live-video.net/app";
            }
            return "rtmp://live.twitch.tv/app";
            
        case SocialPlatform::FACEBOOK:
            return "rtmps://live-api-s.facebook.com:443/rtmp";
            
        case SocialPlatform::TIKTOK:
            return "rtmp://push.tiktokcdn.com/live";
            
        case SocialPlatform::INSTAGRAM:
            return "rtmps://live-upload.instagram.com:443/rtmp";
            
        default:
            return "";
    }
}

std::vector<std::string> SocialPlatformHelper::get_supported_regions(SocialPlatform platform) {
    switch (platform) {
        case SocialPlatform::TWITCH:
            return {
                "live", "live-ord", "live-dfw", "live-sjc", "live-lax",
                "live-fra", "live-arn", "live-mad", "live-lhr", "live-cdg",
                "live-nrt", "live-hkg", "live-syd", "live-sao"
            };
            
        case SocialPlatform::YOUTUBE:
            return {"global"}; // YouTube auto-selects optimal server
            
        default:
            return {"global"};
    }
}

VideoFormat SocialPlatformHelper::get_recommended_format(SocialPlatform platform) {
    VideoFormat format;
    
    switch (platform) {
        case SocialPlatform::YOUTUBE:
            format.width = 1920;
            format.height = 1080;
            format.fps = 30;
            format.bitrate = 4500000; // 4.5 Mbps
            break;
            
        case SocialPlatform::TWITCH:
            format.width = 1920;
            format.height = 1080;
            format.fps = 60;
            format.bitrate = 6000000; // 6 Mbps
            break;
            
        case SocialPlatform::FACEBOOK:
            format.width = 1280;
            format.height = 720;
            format.fps = 30;
            format.bitrate = 4000000; // 4 Mbps
            break;
            
        case SocialPlatform::TIKTOK:
            format.width = 1080;
            format.height = 1920; // Vertical
            format.fps = 30;
            format.bitrate = 3000000; // 3 Mbps
            break;
            
        case SocialPlatform::INSTAGRAM:
            format.width = 1080;
            format.height = 1080; // Square
            format.fps = 30;
            format.bitrate = 3500000; // 3.5 Mbps
            break;
            
        default:
            // Default 1080p format
            break;
    }
    
    return format;
}

bool SocialPlatformHelper::validate_stream_key(SocialPlatform platform, const std::string& stream_key) {
    if (stream_key.empty()) {
        return false;
    }
    
    switch (platform) {
        case SocialPlatform::YOUTUBE:
            // YouTube stream keys are typically 20-40 characters
            return stream_key.length() >= 20 && stream_key.length() <= 40;
            
        case SocialPlatform::TWITCH:
            // Twitch stream keys start with "live_"
            return stream_key.find("live_") == 0 && stream_key.length() > 20;
            
        case SocialPlatform::FACEBOOK:
            // Facebook stream keys are typically long and contain special characters
            return stream_key.length() > 30;
            
        default:
            // Basic validation for other platforms
            return stream_key.length() > 10;
    }
}