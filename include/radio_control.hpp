#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Forward declarations
class AudioSystem;
class VideoStreamManager;
class AudioStreamEncoder;
class DatabaseManager;

/**
 * Track information structure for radio control
 */
struct RadioTrack {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string file_path;
    int duration_ms = 0;
    int bpm = 0;
    std::string key;
    float gain = 1.0f;
    bool is_analyzed = false;
    std::chrono::system_clock::time_point added_at;
    std::chrono::system_clock::time_point last_played;
    int play_count = 0;
    
    json to_json() const {
        return json{
            {"id", id},
            {"title", title},
            {"artist", artist},
            {"album", album},
            {"genre", genre},
            {"file_path", file_path},
            {"duration_ms", duration_ms},
            {"bpm", bpm},
            {"key", key},
            {"gain", gain},
            {"is_analyzed", is_analyzed},
            {"play_count", play_count}
        };
    }
    
    static RadioTrack from_json(const json& j) {
        RadioTrack track;
        track.id = j.value("id", "");
        track.title = j.value("title", "");
        track.artist = j.value("artist", "");
        track.album = j.value("album", "");
        track.genre = j.value("genre", "");
        track.file_path = j.value("file_path", "");
        track.duration_ms = j.value("duration_ms", 0);
        track.bpm = j.value("bpm", 0);
        track.key = j.value("key", "");
        track.gain = j.value("gain", 1.0f);
        track.is_analyzed = j.value("is_analyzed", false);
        track.play_count = j.value("play_count", 0);
        return track;
    }
};

/**
 * Playlist structure for organizing tracks
 */
struct RadioPlaylist {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> track_ids;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    bool is_active = false;
    
    json to_json() const {
        return json{
            {"id", id},
            {"name", name},
            {"description", description},
            {"track_ids", track_ids},
            {"is_active", is_active}
        };
    }
};

/**
 * Radio station configuration
 */
struct RadioStation {
    std::string id;
    std::string name;
    std::string description;
    std::string logo_url;
    std::string website_url;
    std::string genre;
    std::string language;
    std::string country;
    bool is_live = false;
    int listener_count = 0;
    
    // Streaming configuration
    struct StreamConfig {
        std::string server_host;
        int server_port = 8000;
        std::string mount_point = "/stream";
        std::string password;
        std::string username = "source";
        std::string format = "mp3";
        int bitrate = 128;
        bool is_public = true;
    } stream_config;
    
    json to_json() const {
        return json{
            {"id", id},
            {"name", name},
            {"description", description},
            {"logo_url", logo_url},
            {"website_url", website_url},
            {"genre", genre},
            {"language", language},
            {"country", country},
            {"is_live", is_live},
            {"listener_count", listener_count},
            {"stream_config", {
                {"server_host", stream_config.server_host},
                {"server_port", stream_config.server_port},
                {"mount_point", stream_config.mount_point},
                {"username", stream_config.username},
                {"format", stream_config.format},
                {"bitrate", stream_config.bitrate},
                {"is_public", stream_config.is_public}
            }}
        };
    }
};

/**
 * DJ Deck state for mixing interface
 */
struct DJDeck {
    std::string id;
    std::string name;
    RadioTrack* current_track = nullptr;
    
    // Playback state
    bool is_playing = false;
    bool is_paused = false;
    bool is_cue_enabled = false;
    double position_ms = 0.0;
    double playback_rate = 1.0;
    
    // Mix controls
    float volume = 1.0f;
    float gain = 1.0f;
    float high_eq = 0.0f;  // -1.0 to 1.0
    float mid_eq = 0.0f;
    float low_eq = 0.0f;
    
    // Effects
    bool filter_enabled = false;
    float filter_cutoff = 1000.0f;
    bool reverb_enabled = false;
    float reverb_level = 0.0f;
    
    // Loop and cue points
    struct CuePoint {
        double position_ms;
        std::string label;
        bool is_loop_start = false;
        bool is_loop_end = false;
    };
    std::vector<CuePoint> cue_points;
    
    // Hot cues (8 hot cue buttons)
    std::array<CuePoint*, 8> hot_cues = {nullptr};
    
    json to_json() const {
        json j = {
            {"id", id},
            {"name", name},
            {"is_playing", is_playing},
            {"is_paused", is_paused},
            {"is_cue_enabled", is_cue_enabled},
            {"position_ms", position_ms},
            {"playback_rate", playback_rate},
            {"volume", volume},
            {"gain", gain},
            {"high_eq", high_eq},
            {"mid_eq", mid_eq},
            {"low_eq", low_eq},
            {"filter_enabled", filter_enabled},
            {"filter_cutoff", filter_cutoff},
            {"reverb_enabled", reverb_enabled},
            {"reverb_level", reverb_level}
        };
        
        if (current_track) {
            j["current_track"] = current_track->to_json();
        }
        
        return j;
    }
};

/**
 * Radio Control System - Main interface for DJ operations
 */
class RadioControl {
public:
    RadioControl(AudioSystem* audio_system, 
                VideoStreamManager* video_manager,
                AudioStreamEncoder* audio_encoder);
    
    ~RadioControl();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // ===== TRACK MANAGEMENT =====
    
    // Add track to library with metadata analysis
    std::string add_track(const std::string& file_path, const json& metadata = {});
    bool remove_track(const std::string& track_id);
    bool update_track_metadata(const std::string& track_id, const json& metadata);
    RadioTrack* get_track(const std::string& track_id);
    std::vector<RadioTrack> get_all_tracks();
    std::vector<RadioTrack> search_tracks(const std::string& query);
    
    // Track analysis
    bool analyze_track(const std::string& track_id);
    bool analyze_all_tracks();
    
    // ===== PLAYLIST MANAGEMENT =====
    
    std::string create_playlist(const std::string& name, const std::string& description = "");
    bool delete_playlist(const std::string& playlist_id);
    bool add_track_to_playlist(const std::string& playlist_id, const std::string& track_id);
    bool remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id);
    bool reorder_playlist_tracks(const std::string& playlist_id, const std::vector<std::string>& new_order);
    RadioPlaylist* get_playlist(const std::string& playlist_id);
    std::vector<RadioPlaylist> get_all_playlists();
    bool set_active_playlist(const std::string& playlist_id);
    
    // ===== DECK OPERATIONS =====
    
    // Load tracks to decks
    bool load_track_to_deck(const std::string& deck_id, const std::string& track_id);
    bool unload_deck(const std::string& deck_id);
    
    // Playback control
    bool play_deck(const std::string& deck_id);
    bool pause_deck(const std::string& deck_id);
    bool stop_deck(const std::string& deck_id);
    bool cue_deck(const std::string& deck_id);
    bool seek_deck(const std::string& deck_id, double position_ms);
    bool set_deck_playback_rate(const std::string& deck_id, double rate);
    
    // Deck mixing controls
    bool set_deck_volume(const std::string& deck_id, float volume);
    bool set_deck_gain(const std::string& deck_id, float gain);
    bool set_deck_eq(const std::string& deck_id, float high, float mid, float low);
    bool set_deck_filter(const std::string& deck_id, bool enabled, float cutoff = 1000.0f);
    bool set_deck_reverb(const std::string& deck_id, bool enabled, float level = 0.3f);
    
    // Cue points and loops
    bool set_cue_point(const std::string& deck_id, double position_ms, const std::string& label = "");
    bool set_hot_cue(const std::string& deck_id, int hot_cue_index, double position_ms);
    bool trigger_hot_cue(const std::string& deck_id, int hot_cue_index);
    bool clear_hot_cue(const std::string& deck_id, int hot_cue_index);
    bool set_loop(const std::string& deck_id, double start_ms, double end_ms);
    bool enable_loop(const std::string& deck_id, bool enabled);
    
    // ===== MIXER OPERATIONS =====
    
    // Crossfader and channel mixing
    bool set_crossfader_position(float position); // -1.0 (A) to 1.0 (B)
    bool set_crossfader_curve(float curve); // 0.0 (linear) to 1.0 (cut)
    bool set_master_volume(float volume);
    bool set_headphone_volume(float volume);
    bool set_headphone_mix(float mix); // 0.0 (cue) to 1.0 (program)
    
    // Monitor and cue
    bool set_deck_cue_enabled(const std::string& deck_id, bool enabled);
    bool set_master_cue_enabled(bool enabled);

    // ===== MICROPHONE AND TALKOVER =====
    
    // Microphone control
    bool enable_microphone(bool enabled);
    bool set_microphone_gain(float gain); // 0.0 to 2.0 (200%)
    bool set_microphone_mute(bool muted);
    bool is_microphone_enabled();
    bool is_microphone_muted();
    float get_microphone_gain();
    
    // Talkover functionality
    bool enable_talkover(bool enabled);
    bool set_talkover_duck_level(float level); // 0.0 to 1.0 (percentage of original volume)
    bool set_talkover_duck_time(float time_ms); // Fade time in milliseconds
    bool is_talkover_active();
    float get_talkover_duck_level();
    
    // Audio visualization and waveform
    struct WaveformData {
        std::vector<float> peaks;      // Peak values for waveform display
        std::vector<float> rms;        // RMS values for smoother visualization  
        int sample_rate = 44100;
        int samples_per_pixel = 512;   // How many audio samples per pixel
        double duration_ms = 0.0;
        double current_position_ms = 0.0;
    };
    
    WaveformData get_deck_waveform(const std::string& deck_id);
    bool generate_waveform_data(const std::string& track_id, int width_pixels = 800);
    
    // Real-time audio levels for VU meters
    struct RealTimeAudioLevels {
        float left_peak = 0.0f;
        float right_peak = 0.0f;
        float left_rms = 0.0f;
        float right_rms = 0.0f;
        float microphone_level = 0.0f;
        bool is_clipping = false;
        bool is_ducked = false;        // True when talkover is ducking audio
        double timestamp_ms = 0.0;     // For smooth animations
    };
    
    RealTimeAudioLevels get_real_time_levels();
    bool start_audio_monitoring(); // Start continuous audio level monitoring
    bool stop_audio_monitoring();
    
    // ===== CHANNEL CONTROL =====
    
    // Channel audio file loading and playback
    bool load_audio_file(const std::string& channel_id, const std::string& file_path);
    bool set_channel_playback(const std::string& channel_id, bool play);
    bool set_channel_volume(const std::string& channel_id, float volume);
    bool set_channel_eq(const std::string& channel_id, float bass, float mid, float treble);
    
    // Deck waveform data
    WaveformData get_deck_waveform(const std::string& deck_id);
    
    // ===== BPM AND SYNC =====
    
    // BPM detection and sync
    float get_deck_bpm(const std::string& deck_id);
    bool enable_bpm_sync(const std::string& master_deck_id, const std::string& slave_deck_id);
    bool disable_bpm_sync(const std::string& deck_id);
    bool tap_bpm(const std::string& deck_id); // Manual BPM tapping
    
    // Beat matching assistance
    bool enable_beat_matching(const std::string& deck_a, const std::string& deck_b);
    float get_beat_offset(const std::string& deck_a, const std::string& deck_b);
    
    // ===== RADIO STATION CONTROL =====
    
    // Station management
    bool configure_station(const RadioStation& station_config);
    RadioStation get_station_config();
    bool start_broadcast();
    bool stop_broadcast();
    bool update_stream_metadata(const std::string& artist, const std::string& title);
    
    // Auto DJ functionality
    bool enable_auto_dj(bool enabled);
    bool set_auto_dj_crossfade_time(int seconds);
    bool load_auto_dj_playlist(const std::string& playlist_id);
    
    // ===== RECORDING =====
    
    bool start_recording(const std::string& output_path, const std::string& format = "wav");
    bool stop_recording();
    bool is_recording();
    
    // ===== EFFECTS =====
    
    // Global effects
    bool enable_master_limiter(bool enabled, float threshold = -3.0f);
    bool enable_master_compressor(bool enabled, float ratio = 4.0f, float threshold = -12.0f);
    
    // ===== STATUS AND MONITORING =====
    
    // Get current status
    DJDeck* get_deck(const std::string& deck_id);
    std::vector<DJDeck*> get_all_decks();
    json get_mixer_status();
    json get_stream_status();
    json get_system_status();
    
    // Audio levels and monitoring
    struct AudioLevels {
        float left_peak = 0.0f;
        float right_peak = 0.0f;
        float left_rms = 0.0f;
        float right_rms = 0.0f;
        bool clipping = false;
    };
    
    AudioLevels get_master_levels();
    AudioLevels get_deck_levels(const std::string& deck_id);
    AudioLevels get_cue_levels();
    
    // ===== DATABASE INTEGRATION =====
    
    bool save_to_database();
    bool load_from_database();
    bool backup_library(const std::string& backup_path);
    bool restore_library(const std::string& backup_path);
    
    // ===== EVENT CALLBACKS =====
    
    using TrackLoadedCallback = std::function<void(const std::string& deck_id, const RadioTrack& track)>;
    using TrackEndedCallback = std::function<void(const std::string& deck_id)>;
    using BeatCallback = std::function<void(const std::string& deck_id, int beat_number)>;
    
    void set_track_loaded_callback(TrackLoadedCallback callback);
    void set_track_ended_callback(TrackEndedCallback callback);
    void set_beat_callback(BeatCallback callback);

private:
    // Component references
    AudioSystem* audio_system_;
    VideoStreamManager* video_manager_;
    AudioStreamEncoder* audio_encoder_;
    std::unique_ptr<DatabaseManager> database_;
    
    // Internal data
    std::map<std::string, RadioTrack> tracks_;
    std::map<std::string, RadioPlaylist> playlists_;
    std::map<std::string, std::unique_ptr<DJDeck>> decks_;
    RadioStation station_config_;
    
    // Mixer state
    float crossfader_position_ = 0.0f;
    float crossfader_curve_ = 0.5f;
    float master_volume_ = 0.8f;
    float headphone_volume_ = 0.7f;
    float headphone_mix_ = 0.5f;
    bool master_cue_enabled_ = false;
    
    // Auto DJ state
    bool auto_dj_enabled_ = false;
    int auto_dj_crossfade_time_ = 10;
    std::string auto_dj_playlist_id_;
    
    // Recording state
    bool is_recording_ = false;
    std::string recording_output_path_;
    
    // Microphone and talkover state
    bool microphone_enabled_ = false;
    bool microphone_muted_ = false;
    float microphone_gain_ = 1.0f;
    bool talkover_active_ = false;
    float talkover_duck_level_ = 0.25f;  // Duck to 25% by default
    float talkover_duck_time_ = 100.0f;  // 100ms fade time
    float original_master_volume_ = 0.8f; // Store original volume for restoration
    
    // Audio monitoring state
    bool audio_monitoring_active_ = false;
    RealTimeAudioLevels current_levels_;
    std::map<std::string, WaveformData> waveform_cache_;
    
    // Callbacks
    TrackLoadedCallback track_loaded_callback_;
    TrackEndedCallback track_ended_callback_;
    BeatCallback beat_callback_;
    
    // Internal methods
    std::string generate_track_id();
    std::string generate_playlist_id();
    void initialize_default_decks();
    void update_mixer_output();
    void process_auto_dj();
    bool validate_track_file(const std::string& file_path);
    json extract_metadata_from_file(const std::string& file_path);
};