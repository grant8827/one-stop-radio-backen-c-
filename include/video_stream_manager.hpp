#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <map>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

enum class VideoSource {
    CAMERA,
    IMAGE,
    SLIDESHOW,
    OFF
};

enum class SocialPlatform {
    YOUTUBE,
    TWITCH,
    FACEBOOK,
    TIKTOK,
    INSTAGRAM,
    CUSTOM_RTMP
};

struct VideoFormat {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate = 2500000; // 2.5 Mbps
    std::string codec = "h264";
};

struct SocialMediaConfig {
    SocialPlatform platform;
    std::string rtmp_url;
    std::string stream_key;
    std::string title;
    std::string description;
    bool is_live = false;
    VideoFormat video_format;
};

struct SlideShowConfig {
    std::vector<std::string> image_paths;
    int slide_duration_seconds = 5;
    bool loop = true;
    std::string transition_effect = "fade";
};

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();
    
    bool initialize(const VideoFormat& format);
    bool encode_frame(const uint8_t* frame_data, std::vector<uint8_t>& encoded_data);
    void reset();
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class VideoComposer {
public:
    VideoComposer();
    ~VideoComposer();
    
    bool initialize(const VideoFormat& format);
    
    // Video source management
    void set_video_source(VideoSource source);
    VideoSource get_current_source() const;
    
    // Camera controls
    bool enable_camera();
    bool disable_camera();
    bool is_camera_enabled() const;
    
    // Static image
    bool set_static_image(const std::string& image_path);
    
    // Slideshow
    bool start_slideshow(const SlideShowConfig& config);
    bool stop_slideshow();
    bool is_slideshow_active() const;
    void next_slide();
    void previous_slide();
    
    // Frame generation
    bool get_current_frame(uint8_t* frame_buffer, size_t buffer_size);
    
    // Overlay text/graphics
    bool add_text_overlay(const std::string& text, int x, int y, 
                         const std::string& font = "Arial", int font_size = 24);
    bool remove_text_overlay();
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class SocialMediaStreamer {
public:
    SocialMediaStreamer();
    ~SocialMediaStreamer();
    
    // Platform configuration
    bool add_platform(const std::string& platform_id, const SocialMediaConfig& config);
    bool remove_platform(const std::string& platform_id);
    bool update_platform_config(const std::string& platform_id, const SocialMediaConfig& config);
    
    // Streaming control
    bool start_streaming(const std::string& platform_id);
    bool stop_streaming(const std::string& platform_id);
    bool is_streaming(const std::string& platform_id) const;
    
    // Multi-platform streaming
    bool start_multi_stream(const std::vector<std::string>& platform_ids);
    bool stop_all_streams();
    
    // Stream data
    bool send_video_data(const uint8_t* video_data, size_t video_size,
                        const uint8_t* audio_data, size_t audio_size);
    
    // Statistics
    struct StreamStats {
        uint64_t bytes_sent = 0;
        uint64_t frames_sent = 0;
        double current_bitrate = 0.0;
        bool is_connected = false;
        std::string last_error;
    };
    
    StreamStats get_stream_stats(const std::string& platform_id) const;
    std::vector<std::string> get_active_streams() const;
    
    // Callbacks
    using StatusCallback = std::function<void(const std::string&, bool, const std::string&)>;
    void set_status_callback(StatusCallback callback);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Utility functions for platform-specific RTMP URLs
class SocialPlatformHelper {
public:
    static std::string get_rtmp_url(SocialPlatform platform, const std::string& region = "");
    static std::vector<std::string> get_supported_regions(SocialPlatform platform);
    static VideoFormat get_recommended_format(SocialPlatform platform);
    static bool validate_stream_key(SocialPlatform platform, const std::string& stream_key);
};

// Main video streaming manager
class VideoStreamManager {
public:
    VideoStreamManager();
    ~VideoStreamManager();
    
    bool initialize(const VideoFormat& format = VideoFormat{});
    void shutdown();
    
    // Video composition
    VideoComposer& get_composer() { return *composer_; }
    
    // Social media streaming
    SocialMediaStreamer& get_streamer() { return *streamer_; }
    
    // Integrated controls
    bool start_live_stream(const std::vector<std::string>& platform_ids);
    bool stop_live_stream();
    bool is_live() const;
    
    // Video source shortcuts
    bool switch_to_camera();
    bool switch_to_image(const std::string& image_path);
    bool switch_to_slideshow(const SlideShowConfig& config);
    bool switch_to_off();
    
    // Quick platform setup
    bool setup_youtube_stream(const std::string& stream_key, const std::string& title = "");
    bool setup_twitch_stream(const std::string& stream_key, const std::string& title = "");
    bool setup_facebook_stream(const std::string& stream_key, const std::string& title = "");
    bool setup_custom_rtmp(const std::string& rtmp_url, const std::string& stream_key);
    
private:
    void video_processing_loop();
    
    std::unique_ptr<VideoComposer> composer_;
    std::unique_ptr<VideoEncoder> encoder_;
    std::unique_ptr<SocialMediaStreamer> streamer_;
    
    VideoFormat current_format_;
    bool initialized_;
    bool running_;
    std::thread processing_thread_;
    mutable std::mutex mutex_;
};