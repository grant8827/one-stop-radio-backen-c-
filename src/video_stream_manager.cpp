#include "video_stream_manager.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <fstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}

// VideoEncoder Implementation
class VideoEncoder::Impl {
public:
    Impl() : codec_context_(nullptr), frame_(nullptr), packet_(nullptr), initialized_(false) {}
    
    ~Impl() {
        cleanup();
    }
    
    bool initialize(const VideoFormat& format) {
        format_ = format;
        
        // Find H.264 encoder
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            Logger::error("VideoEncoder", "H.264 encoder not found");
            return false;
        }
        
        // Allocate codec context
        codec_context_ = avcodec_alloc_context3(codec);
        if (!codec_context_) {
            Logger::error("VideoEncoder", "Failed to allocate codec context");
            return false;
        }
        
        // Set codec parameters
        codec_context_->bit_rate = format.bitrate;
        codec_context_->width = format.width;
        codec_context_->height = format.height;
        codec_context_->time_base = {1, format.fps};
        codec_context_->framerate = {format.fps, 1};
        codec_context_->gop_size = format.fps; // I-frame every second
        codec_context_->max_b_frames = 1;
        codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;
        
        // Set H.264 specific options for streaming
        av_opt_set(codec_context_->priv_data, "preset", "fast", 0);
        av_opt_set(codec_context_->priv_data, "tune", "zerolatency", 0);
        av_opt_set(codec_context_->priv_data, "profile", "baseline", 0);
        
        // Open codec
        if (avcodec_open2(codec_context_, codec, nullptr) < 0) {
            Logger::error("VideoEncoder", "Failed to open codec");
            cleanup();
            return false;
        }
        
        // Allocate frame
        frame_ = av_frame_alloc();
        if (!frame_) {
            Logger::error("VideoEncoder", "Failed to allocate frame");
            cleanup();
            return false;
        }
        
        frame_->format = codec_context_->pix_fmt;
        frame_->width = codec_context_->width;
        frame_->height = codec_context_->height;
        
        if (av_frame_get_buffer(frame_, 0) < 0) {
            Logger::error("VideoEncoder", "Failed to allocate frame buffer");
            cleanup();
            return false;
        }
        
        // Allocate packet
        packet_ = av_packet_alloc();
        if (!packet_) {
            Logger::error("VideoEncoder", "Failed to allocate packet");
            cleanup();
            return false;
        }
        
        initialized_ = true;
        Logger::info("VideoEncoder", "Initialized successfully");
        return true;
    }
    
    bool encode_frame(const uint8_t* frame_data, std::vector<uint8_t>& encoded_data) {
        if (!initialized_) {
            return false;
        }
        
        // Copy frame data (assuming RGB24 input)
        // In a real implementation, you'd convert RGB to YUV420P here
        memcpy(frame_->data[0], frame_data, 
               format_.width * format_.height * 3 / 2); // YUV420P size
        
        frame_->pts = frame_count_++;
        
        // Encode frame
        int ret = avcodec_send_frame(codec_context_, frame_);
        if (ret < 0) {
            Logger::error("VideoEncoder", "Error sending frame to encoder");
            return false;
        }
        
        // Receive encoded packets
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_context_, packet_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                Logger::error("VideoEncoder", "Error receiving packet from encoder");
                break;
            }
            
            // Copy encoded data
            encoded_data.insert(encoded_data.end(), 
                              packet_->data, 
                              packet_->data + packet_->size);
            
            av_packet_unref(packet_);
        }
        
        return true;
    }
    
    void cleanup() {
        if (packet_) {
            av_packet_free(&packet_);
        }
        
        if (frame_) {
            av_frame_free(&frame_);
        }
        
        if (codec_context_) {
            avcodec_free_context(&codec_context_);
        }
        
        initialized_ = false;
    }

private:
    AVCodecContext* codec_context_;
    AVFrame* frame_;
    AVPacket* packet_;
    VideoFormat format_;
    bool initialized_;
    int64_t frame_count_ = 0;
};

VideoEncoder::VideoEncoder() : impl_(std::make_unique<Impl>()) {}
VideoEncoder::~VideoEncoder() = default;

bool VideoEncoder::initialize(const VideoFormat& format) {
    return impl_->initialize(format);
}

bool VideoEncoder::encode_frame(const uint8_t* frame_data, std::vector<uint8_t>& encoded_data) {
    return impl_->encode_frame(frame_data, encoded_data);
}

void VideoEncoder::reset() {
    impl_->cleanup();
}

// VideoComposer Implementation
class VideoComposer::Impl {
public:
    Impl() : current_source_(VideoSource::OFF), camera_enabled_(false), 
             slideshow_active_(false), current_slide_index_(0) {}
    
    bool initialize(const VideoFormat& format) {
        format_ = format;
        
        // Allocate frame buffer
        frame_size_ = format.width * format.height * 3; // RGB24
        frame_buffer_.resize(frame_size_);
        
        // Create black frame as default
        std::fill(frame_buffer_.begin(), frame_buffer_.end(), 0);
        
        Logger::info("VideoComposer", "Initialized with resolution " + 
                    std::to_string(format.width) + "x" + std::to_string(format.height));
        return true;
    }
    
    void set_video_source(VideoSource source) {
        std::lock_guard<std::mutex> lock(mutex_);
        current_source_ = source;
        
        switch (source) {
            case VideoSource::CAMERA:
                enable_camera();
                break;
            case VideoSource::OFF:
                disable_camera();
                stop_slideshow();
                generate_black_frame();
                break;
            default:
                break;
        }
        
        Logger::info("VideoComposer", "Video source changed to " + std::to_string((int)source));
    }
    
    VideoSource get_current_source() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_source_;
    }
    
    bool enable_camera() {
        // In a real implementation, this would initialize camera capture
        camera_enabled_ = true;
        Logger::info("VideoComposer", "Camera enabled");
        return true;
    }
    
    bool disable_camera() {
        camera_enabled_ = false;
        Logger::info("VideoComposer", "Camera disabled");
        return true;
    }
    
    bool is_camera_enabled() const {
        return camera_enabled_;
    }
    
    bool set_static_image(const std::string& image_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // In a real implementation, load and scale image using FFmpeg
        current_image_path_ = image_path;
        current_source_ = VideoSource::IMAGE;
        
        // Mock: Create a colored frame to represent the image
        generate_colored_frame(100, 150, 200); // Light blue
        
        Logger::info("VideoComposer", "Static image set: " + image_path);
        return true;
    }
    
    bool start_slideshow(const SlideShowConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (config.image_paths.empty()) {
            Logger::warn("VideoComposer", "Slideshow started with no images");
            return false;
        }
        
        slideshow_config_ = config;
        slideshow_active_ = true;
        current_slide_index_ = 0;
        current_source_ = VideoSource::SLIDESHOW;
        
        // Start slideshow timer thread
        slideshow_thread_ = std::thread([this]() {
            while (slideshow_active_) {
                std::this_thread::sleep_for(
                    std::chrono::seconds(slideshow_config_.slide_duration_seconds));
                
                if (slideshow_active_) {
                    next_slide();
                }
            }
        });
        
        // Load first slide
        load_current_slide();
        
        Logger::info("VideoComposer", "Slideshow started with " + 
                    std::to_string(config.image_paths.size()) + " images");
        return true;
    }
    
    bool stop_slideshow() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        slideshow_active_ = false;
        if (slideshow_thread_.joinable()) {
            slideshow_thread_.join();
        }
        
        Logger::info("VideoComposer", "Slideshow stopped");
        return true;
    }
    
    bool is_slideshow_active() const {
        return slideshow_active_;
    }
    
    void next_slide() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!slideshow_active_ || slideshow_config_.image_paths.empty()) {
            return;
        }
        
        current_slide_index_++;
        if (current_slide_index_ >= slideshow_config_.image_paths.size()) {
            if (slideshow_config_.loop) {
                current_slide_index_ = 0;
            } else {
                current_slide_index_--;
                return;
            }
        }
        
        load_current_slide();
    }
    
    void previous_slide() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!slideshow_active_ || slideshow_config_.image_paths.empty()) {
            return;
        }
        
        if (current_slide_index_ == 0) {
            if (slideshow_config_.loop) {
                current_slide_index_ = slideshow_config_.image_paths.size() - 1;
            }
        } else {
            current_slide_index_--;
        }
        
        load_current_slide();
    }
    
    bool get_current_frame(uint8_t* frame_buffer, size_t buffer_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (buffer_size < frame_size_) {
            return false;
        }
        
        // Generate frame based on current source
        switch (current_source_) {
            case VideoSource::CAMERA:
                if (camera_enabled_) {
                    generate_camera_frame();
                } else {
                    generate_black_frame();
                }
                break;
                
            case VideoSource::IMAGE:
            case VideoSource::SLIDESHOW:
                // Current frame already prepared
                break;
                
            case VideoSource::OFF:
            default:
                generate_black_frame();
                break;
        }
        
        // Apply text overlay if present
        if (!overlay_text_.empty()) {
            apply_text_overlay();
        }
        
        memcpy(frame_buffer, frame_buffer_.data(), frame_size_);
        return true;
    }
    
    bool add_text_overlay(const std::string& text, int x, int y, 
                         const std::string& font, int font_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        overlay_text_ = text;
        overlay_x_ = x;
        overlay_y_ = y;
        overlay_font_ = font;
        overlay_font_size_ = font_size;
        
        Logger::info("VideoComposer", "Text overlay added: " + text);
        return true;
    }
    
    bool remove_text_overlay() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        overlay_text_.clear();
        Logger::info("VideoComposer", "Text overlay removed");
        return true;
    }

private:
    void generate_black_frame() {
        std::fill(frame_buffer_.begin(), frame_buffer_.end(), 0);
    }
    
    void generate_colored_frame(uint8_t r, uint8_t g, uint8_t b) {
        for (size_t i = 0; i < frame_size_; i += 3) {
            frame_buffer_[i] = r;     // Red
            frame_buffer_[i + 1] = g; // Green  
            frame_buffer_[i + 2] = b; // Blue
        }
    }
    
    void generate_camera_frame() {
        // Mock camera frame with moving gradient
        static int frame_counter = 0;
        frame_counter++;
        
        for (int y = 0; y < format_.height; ++y) {
            for (int x = 0; x < format_.width; ++x) {
                size_t index = (y * format_.width + x) * 3;
                
                // Create a moving gradient effect
                uint8_t r = (x + frame_counter) % 256;
                uint8_t g = (y + frame_counter) % 256;
                uint8_t b = ((x + y + frame_counter) / 2) % 256;
                
                frame_buffer_[index] = r;
                frame_buffer_[index + 1] = g;
                frame_buffer_[index + 2] = b;
            }
        }
    }
    
    void load_current_slide() {
        if (current_slide_index_ < slideshow_config_.image_paths.size()) {
            const std::string& image_path = slideshow_config_.image_paths[current_slide_index_];
            
            // Mock: Generate different colored frame for each slide
            uint8_t r = (current_slide_index_ * 50) % 256;
            uint8_t g = (current_slide_index_ * 80) % 256;
            uint8_t b = (current_slide_index_ * 120) % 256;
            
            generate_colored_frame(r, g, b);
            
            Logger::debug("VideoComposer", "Loaded slide " + std::to_string(current_slide_index_) + 
                         ": " + image_path);
        }
    }
    
    void apply_text_overlay() {
        // Mock text overlay - in real implementation, use FFmpeg's text filter
        // For now, just draw a simple white rectangle where text would be
        int text_width = overlay_text_.length() * (overlay_font_size_ / 2);
        int text_height = overlay_font_size_;
        
        for (int y = overlay_y_; y < overlay_y_ + text_height && y < format_.height; ++y) {
            for (int x = overlay_x_; x < overlay_x_ + text_width && x < format_.width; ++x) {
                size_t index = (y * format_.width + x) * 3;
                if (index + 2 < frame_size_) {
                    frame_buffer_[index] = 255;     // White text background
                    frame_buffer_[index + 1] = 255;
                    frame_buffer_[index + 2] = 255;
                }
            }
        }
    }
    
    VideoFormat format_;
    VideoSource current_source_;
    bool camera_enabled_;
    
    // Frame buffer
    std::vector<uint8_t> frame_buffer_;
    size_t frame_size_;
    
    // Static image
    std::string current_image_path_;
    
    // Slideshow
    SlideShowConfig slideshow_config_;
    bool slideshow_active_;
    size_t current_slide_index_;
    std::thread slideshow_thread_;
    
    // Text overlay
    std::string overlay_text_;
    int overlay_x_ = 0;
    int overlay_y_ = 0;
    std::string overlay_font_;
    int overlay_font_size_ = 24;
    
    mutable std::mutex mutex_;
};

VideoComposer::VideoComposer() : impl_(std::make_unique<Impl>()) {}
VideoComposer::~VideoComposer() = default;

bool VideoComposer::initialize(const VideoFormat& format) {
    return impl_->initialize(format);
}

void VideoComposer::set_video_source(VideoSource source) {
    impl_->set_video_source(source);
}

VideoSource VideoComposer::get_current_source() const {
    return impl_->get_current_source();
}

bool VideoComposer::enable_camera() {
    return impl_->enable_camera();
}

bool VideoComposer::disable_camera() {
    return impl_->disable_camera();
}

bool VideoComposer::is_camera_enabled() const {
    return impl_->is_camera_enabled();
}

bool VideoComposer::set_static_image(const std::string& image_path) {
    return impl_->set_static_image(image_path);
}

bool VideoComposer::start_slideshow(const SlideShowConfig& config) {
    return impl_->start_slideshow(config);
}

bool VideoComposer::stop_slideshow() {
    return impl_->stop_slideshow();
}

bool VideoComposer::is_slideshow_active() const {
    return impl_->is_slideshow_active();
}

void VideoComposer::next_slide() {
    impl_->next_slide();
}

void VideoComposer::previous_slide() {
    impl_->previous_slide();
}

bool VideoComposer::get_current_frame(uint8_t* frame_buffer, size_t buffer_size) {
    return impl_->get_current_frame(frame_buffer, buffer_size);
}

bool VideoComposer::add_text_overlay(const std::string& text, int x, int y, 
                                    const std::string& font, int font_size) {
    return impl_->add_text_overlay(text, x, y, font, font_size);
}

bool VideoComposer::remove_text_overlay() {
    return impl_->remove_text_overlay();
}

// VideoStreamManager Implementation
VideoStreamManager::VideoStreamManager() 
    : initialized_(false), running_(false) {
}

VideoStreamManager::~VideoStreamManager() {
    shutdown();
}

bool VideoStreamManager::initialize(const VideoFormat& format) {
    if (initialized_) {
        Logger::warn("VideoStreamManager", "Already initialized");
        return true;
    }
    
    current_format_ = format;
    
    // Initialize components
    composer_ = std::make_unique<VideoComposer>();
    encoder_ = std::make_unique<VideoEncoder>();
    streamer_ = std::make_unique<SocialMediaStreamer>();
    
    if (!composer_->initialize(format)) {
        Logger::error("VideoStreamManager", "Failed to initialize video composer");
        return false;
    }
    
    if (!encoder_->initialize(format)) {
        Logger::error("VideoStreamManager", "Failed to initialize video encoder");
        return false;
    }
    
    // Set up streamer callback
    streamer_->set_status_callback([](const std::string& platform_id, bool success, const std::string& message) {
        Logger::info("VideoStreamManager", "Stream " + platform_id + ": " + message);
    });
    
    initialized_ = true;
    Logger::info("VideoStreamManager", "Initialized successfully");
    return true;
}

void VideoStreamManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    stop_live_stream();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    composer_.reset();
    encoder_.reset();  
    streamer_.reset();
    
    initialized_ = false;
    Logger::info("VideoStreamManager", "Shutdown complete");
}

bool VideoStreamManager::start_live_stream(const std::vector<std::string>& platform_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        Logger::error("VideoStreamManager", "Not initialized");
        return false;
    }
    
    if (running_) {
        Logger::warn("VideoStreamManager", "Live stream already running");
        return true;
    }
    
    // Start streaming on specified platforms
    if (!streamer_->start_multi_stream(platform_ids)) {
        Logger::error("VideoStreamManager", "Failed to start streaming on some platforms");
        return false;
    }
    
    // Start video processing loop
    running_ = true;
    processing_thread_ = std::thread([this]() {
        video_processing_loop();
    });
    
    Logger::info("VideoStreamManager", "Live stream started");
    return true;
}

bool VideoStreamManager::stop_live_stream() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!running_) {
        return true;
    }
    
    running_ = false;
    
    // Stop video processing
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    
    // Stop all streams
    streamer_->stop_all_streams();
    
    Logger::info("VideoStreamManager", "Live stream stopped");
    return true;
}

bool VideoStreamManager::is_live() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void VideoStreamManager::video_processing_loop() {
    const int target_fps = current_format_.fps;
    const auto frame_duration = std::chrono::milliseconds(1000 / target_fps);
    
    std::vector<uint8_t> frame_buffer(current_format_.width * current_format_.height * 3);
    std::vector<uint8_t> encoded_data;
    
    Logger::info("VideoStreamManager", "Video processing loop started");
    
    while (running_) {
        auto frame_start = std::chrono::steady_clock::now();
        
        // Get current frame from composer
        if (composer_->get_current_frame(frame_buffer.data(), frame_buffer.size())) {
            
            // Encode frame
            encoded_data.clear();
            if (encoder_->encode_frame(frame_buffer.data(), encoded_data)) {
                
                // Send to all active streams
                if (!encoded_data.empty()) {
                    streamer_->send_video_data(encoded_data.data(), encoded_data.size(),
                                             nullptr, 0); // Audio data handled separately
                }
            }
        }
        
        // Maintain target FPS
        auto frame_end = std::chrono::steady_clock::now();
        auto elapsed = frame_end - frame_start;
        
        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }
    
    Logger::info("VideoStreamManager", "Video processing loop stopped");
}

// Video source shortcuts
bool VideoStreamManager::switch_to_camera() {
    if (!initialized_) return false;
    composer_->set_video_source(VideoSource::CAMERA);
    return true;
}

bool VideoStreamManager::switch_to_image(const std::string& image_path) {
    if (!initialized_) return false;
    return composer_->set_static_image(image_path);
}

bool VideoStreamManager::switch_to_slideshow(const SlideShowConfig& config) {
    if (!initialized_) return false;
    return composer_->start_slideshow(config);
}

bool VideoStreamManager::switch_to_off() {
    if (!initialized_) return false;
    composer_->set_video_source(VideoSource::OFF);
    return true;
}

// Quick platform setup
bool VideoStreamManager::setup_youtube_stream(const std::string& stream_key, const std::string& title) {
    if (!initialized_) return false;
    
    SocialMediaConfig config;
    config.platform = SocialPlatform::YOUTUBE;
    config.rtmp_url = SocialPlatformHelper::get_rtmp_url(SocialPlatform::YOUTUBE);
    config.stream_key = stream_key;
    config.title = title.empty() ? "OneStopRadio Live Stream" : title;
    config.video_format = SocialPlatformHelper::get_recommended_format(SocialPlatform::YOUTUBE);
    
    return streamer_->add_platform("youtube", config);
}

bool VideoStreamManager::setup_twitch_stream(const std::string& stream_key, const std::string& title) {
    if (!initialized_) return false;
    
    SocialMediaConfig config;
    config.platform = SocialPlatform::TWITCH;
    config.rtmp_url = SocialPlatformHelper::get_rtmp_url(SocialPlatform::TWITCH);
    config.stream_key = stream_key;
    config.title = title.empty() ? "OneStopRadio Live DJ Set" : title;
    config.video_format = SocialPlatformHelper::get_recommended_format(SocialPlatform::TWITCH);
    
    return streamer_->add_platform("twitch", config);
}

bool VideoStreamManager::setup_facebook_stream(const std::string& stream_key, const std::string& title) {
    if (!initialized_) return false;
    
    SocialMediaConfig config;
    config.platform = SocialPlatform::FACEBOOK;
    config.rtmp_url = SocialPlatformHelper::get_rtmp_url(SocialPlatform::FACEBOOK);
    config.stream_key = stream_key;
    config.title = title.empty() ? "Live Radio Show" : title;
    config.video_format = SocialPlatformHelper::get_recommended_format(SocialPlatform::FACEBOOK);
    
    return streamer_->add_platform("facebook", config);
}

bool VideoStreamManager::setup_custom_rtmp(const std::string& rtmp_url, const std::string& stream_key) {
    if (!initialized_) return false;
    
    SocialMediaConfig config;
    config.platform = SocialPlatform::CUSTOM_RTMP;
    config.rtmp_url = rtmp_url;
    config.stream_key = stream_key;
    config.title = "Custom RTMP Stream";
    config.video_format = VideoFormat{}; // Use default format
    
    return streamer_->add_platform("custom", config);
}