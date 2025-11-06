#include "radio_control.hpp"
#include "database_manager.hpp"
#include "audio_system.hpp"
#include "video_stream_manager.hpp"
#include "audio_stream_encoder.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <random>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>

RadioControl::RadioControl(AudioSystem* audio_system, 
                          VideoStreamManager* video_manager,
                          AudioStreamEncoder* audio_encoder)
    : audio_system_(audio_system)
    , video_manager_(video_manager)
    , audio_encoder_(audio_encoder)
    , database_(std::make_unique<DatabaseManager>())
    , crossfader_position_(0.0f)
    , crossfader_curve_(0.5f)
    , master_volume_(0.8f)
    , headphone_volume_(0.7f)
    , headphone_mix_(0.5f)
    , master_cue_enabled_(false)
    , auto_dj_enabled_(false)
    , auto_dj_crossfade_time_(10)
    , is_recording_(false)
{
    Logger::info("RadioControl: Initializing radio control system");
}

RadioControl::~RadioControl() {
    shutdown();
}

bool RadioControl::initialize() {
    Logger::info("RadioControl: Starting initialization");
    
    // Initialize database
    if (!database_->initialize("radio_database.db")) {
        Logger::error("RadioControl: Failed to initialize database");
        return false;
    }
    
    // Initialize default decks
    initialize_default_decks();
    
    // Load existing data from database
    if (!load_from_database()) {
        Logger::warn("RadioControl: Failed to load data from database, starting fresh");
    }
    
    // Initialize station configuration if not exists
    if (station_config_.id.empty()) {
        station_config_.id = "onestopradio_main";
        station_config_.name = "OneStopRadio";
        station_config_.description = "Professional DJ Radio Station";
        station_config_.genre = "Electronic";
        station_config_.language = "English";
        station_config_.country = "US";
        
        // Default streaming configuration
        station_config_.stream_config.server_host = "localhost";
        station_config_.stream_config.server_port = 8000;
        station_config_.stream_config.mount_point = "/onestopradio";
        station_config_.stream_config.username = "source";
        station_config_.stream_config.format = "mp3";
        station_config_.stream_config.bitrate = 128;
        station_config_.stream_config.is_public = true;
        
        database_->save_station_config(station_config_);
    }
    
    Logger::info("RadioControl: Initialization completed successfully");
    return true;
}

void RadioControl::shutdown() {
    Logger::info("RadioControl: Shutting down radio control system");
    
    // Stop all decks
    for (auto& [deck_id, deck] : decks_) {
        stop_deck(deck_id);
    }
    
    // Stop recording if active
    if (is_recording_) {
        stop_recording();
    }
    
    // Stop broadcasting
    stop_broadcast();
    
    // Save current state to database
    save_to_database();
    
    // Close database
    if (database_) {
        database_->close();
    }
    
    Logger::info("RadioControl: Shutdown completed");
}

// ===== TRACK MANAGEMENT =====

std::string RadioControl::add_track(const std::string& file_path, const json& metadata) {
    Logger::info("RadioControl: Adding track from " + file_path);
    
    // Validate file exists and is readable
    if (!validate_track_file(file_path)) {
        Logger::error("RadioControl: Invalid track file: " + file_path);
        return "";
    }
    
    // Generate unique track ID
    std::string track_id = generate_track_id();
    
    // Create track object
    RadioTrack track;
    track.id = track_id;
    track.file_path = file_path;
    track.added_at = std::chrono::system_clock::now();
    
    // Extract metadata from file
    json file_metadata = extract_metadata_from_file(file_path);
    
    // Merge provided metadata with extracted metadata
    json combined_metadata = file_metadata;
    combined_metadata.update(metadata);
    
    // Populate track fields
    track.title = combined_metadata.value("title", std::filesystem::path(file_path).stem().string());
    track.artist = combined_metadata.value("artist", "Unknown Artist");
    track.album = combined_metadata.value("album", "");
    track.genre = combined_metadata.value("genre", "");
    track.duration_ms = combined_metadata.value("duration_ms", 0);
    track.bpm = combined_metadata.value("bpm", 0);
    track.key = combined_metadata.value("key", "");
    track.gain = combined_metadata.value("gain", 1.0f);
    
    // Store track in memory
    tracks_[track_id] = track;
    
    // Save to database
    if (!database_->insert_track(track)) {
        Logger::error("RadioControl: Failed to save track to database");
        tracks_.erase(track_id);
        return "";
    }
    
    // Analyze track if requested
    if (combined_metadata.value("analyze", true)) {
        analyze_track(track_id);
    }
    
    Logger::info("RadioControl: Successfully added track " + track.title + " by " + track.artist);
    return track_id;
}

bool RadioControl::remove_track(const std::string& track_id) {
    Logger::info("RadioControl: Removing track " + track_id);
    
    // Check if track is currently loaded in any deck
    for (auto& [deck_id, deck] : decks_) {
        if (deck->current_track && deck->current_track->id == track_id) {
            unload_deck(deck_id);
        }
    }
    
    // Remove from memory
    auto it = tracks_.find(track_id);
    if (it != tracks_.end()) {
        tracks_.erase(it);
    }
    
    // Remove from database
    if (!database_->delete_track(track_id)) {
        Logger::error("RadioControl: Failed to remove track from database");
        return false;
    }
    
    Logger::info("RadioControl: Successfully removed track " + track_id);
    return true;
}

bool RadioControl::update_track_metadata(const std::string& track_id, const json& metadata) {
    auto it = tracks_.find(track_id);
    if (it == tracks_.end()) {
        Logger::error("RadioControl: Track not found: " + track_id);
        return false;
    }
    
    RadioTrack& track = it->second;
    
    // Update fields from metadata
    if (metadata.contains("title")) track.title = metadata["title"];
    if (metadata.contains("artist")) track.artist = metadata["artist"];
    if (metadata.contains("album")) track.album = metadata["album"];
    if (metadata.contains("genre")) track.genre = metadata["genre"];
    if (metadata.contains("bpm")) track.bpm = metadata["bpm"];
    if (metadata.contains("key")) track.key = metadata["key"];
    if (metadata.contains("gain")) track.gain = metadata["gain"];
    
    // Update in database
    if (!database_->update_track(track)) {
        Logger::error("RadioControl: Failed to update track in database");
        return false;
    }
    
    Logger::info("RadioControl: Updated metadata for track " + track_id);
    return true;
}

RadioTrack* RadioControl::get_track(const std::string& track_id) {
    auto it = tracks_.find(track_id);
    return (it != tracks_.end()) ? &it->second : nullptr;
}

std::vector<RadioTrack> RadioControl::get_all_tracks() {
    std::vector<RadioTrack> result;
    result.reserve(tracks_.size());
    
    for (const auto& [id, track] : tracks_) {
        result.push_back(track);
    }
    
    return result;
}

std::vector<RadioTrack> RadioControl::search_tracks(const std::string& query) {
    std::vector<RadioTrack> result;
    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
    
    for (const auto& [id, track] : tracks_) {
        std::string searchable = track.title + " " + track.artist + " " + track.album + " " + track.genre;
        std::transform(searchable.begin(), searchable.end(), searchable.begin(), ::tolower);
        
        if (searchable.find(lower_query) != std::string::npos) {
            result.push_back(track);
        }
    }
    
    return result;
}

// ===== DECK OPERATIONS =====

bool RadioControl::load_track_to_deck(const std::string& deck_id, const std::string& track_id) {
    Logger::info("RadioControl: Loading track " + track_id + " to deck " + deck_id);
    
    auto deck_it = decks_.find(deck_id);
    if (deck_it == decks_.end()) {
        Logger::error("RadioControl: Deck not found: " + deck_id);
        return false;
    }
    
    auto track_it = tracks_.find(track_id);
    if (track_it == tracks_.end()) {
        Logger::error("RadioControl: Track not found: " + track_id);
        return false;
    }
    
    DJDeck* deck = deck_it->second.get();
    RadioTrack* track = &track_it->second;
    
    // Stop current playback if active
    if (deck->is_playing) {
        stop_deck(deck_id);
    }
    
    // Load track in audio system
    if (!audio_system_->load_audio_file(deck_id, track->file_path)) {
        Logger::error("RadioControl: Failed to load audio file to audio system");
        return false;
    }
    
    // Update deck state
    deck->current_track = track;
    deck->position_ms = 0.0;
    deck->is_playing = false;
    deck->is_paused = false;
    deck->playback_rate = 1.0;
    
    // Apply track gain
    audio_system_->set_channel_volume(deck_id, track->gain * deck->volume);
    
    // Load cue points and hot cues from database
    auto cue_points = database_->get_track_cue_points(track_id);
    deck->cue_points.clear();
    for (const auto& cp : cue_points) {
        DJDeck::CuePoint cue;
        cue.position_ms = cp.position_ms;
        cue.label = cp.label;
        cue.is_loop_start = cp.is_loop_start;
        cue.is_loop_end = cp.is_loop_end;
        deck->cue_points.push_back(cue);
    }
    
    // Load hot cues
    auto hot_cues = database_->get_track_hot_cues(track_id);
    for (int i = 0; i < 8; i++) {
        deck->hot_cues[i] = nullptr;
    }
    for (const auto& hc : hot_cues) {
        if (hc.hot_cue_index >= 0 && hc.hot_cue_index < 8) {
            // Find corresponding cue point
            for (auto& cp : deck->cue_points) {
                if (std::abs(cp.position_ms - hc.position_ms) < 100.0) { // 100ms tolerance
                    deck->hot_cues[hc.hot_cue_index] = &cp;
                    break;
                }
            }
        }
    }
    
    // Trigger callback
    if (track_loaded_callback_) {
        track_loaded_callback_(deck_id, *track);
    }
    
    Logger::info("RadioControl: Successfully loaded " + track->title + " to deck " + deck_id);
    return true;
}

bool RadioControl::unload_deck(const std::string& deck_id) {
    auto deck_it = decks_.find(deck_id);
    if (deck_it == decks_.end()) {
        return false;
    }
    
    DJDeck* deck = deck_it->second.get();
    
    // Stop playback
    stop_deck(deck_id);
    
    // Clear deck state
    deck->current_track = nullptr;
    deck->position_ms = 0.0;
    deck->cue_points.clear();
    for (int i = 0; i < 8; i++) {
        deck->hot_cues[i] = nullptr;
    }
    
    return true;
}

bool RadioControl::play_deck(const std::string& deck_id) {
    auto deck_it = decks_.find(deck_id);
    if (deck_it == decks_.end() || !deck_it->second->current_track) {
        return false;
    }
    
    DJDeck* deck = deck_it->second.get();
    
    if (!audio_system_->play_channel(deck_id)) {
        return false;
    }
    
    deck->is_playing = true;
    deck->is_paused = false;
    
    // Update track play statistics
    if (deck->current_track) {
        database_->increment_play_count(deck->current_track->id);
        database_->update_last_played(deck->current_track->id);
        deck->current_track->play_count++;
        deck->current_track->last_played = std::chrono::system_clock::now();
    }
    
    Logger::info("RadioControl: Started playback on deck " + deck_id);
    return true;
}

bool RadioControl::pause_deck(const std::string& deck_id) {
    auto deck_it = decks_.find(deck_id);
    if (deck_it == decks_.end()) {
        return false;
    }
    
    DJDeck* deck = deck_it->second.get();
    
    if (!audio_system_->pause_channel(deck_id)) {
        return false;
    }
    
    deck->is_playing = false;
    deck->is_paused = true;
    
    Logger::info("RadioControl: Paused playback on deck " + deck_id);
    return true;
}

bool RadioControl::stop_deck(const std::string& deck_id) {
    auto deck_it = decks_.find(deck_id);
    if (deck_it == decks_.end()) {
        return false;
    }
    
    DJDeck* deck = deck_it->second.get();
    
    if (!audio_system_->stop_channel(deck_id)) {
        return false;
    }
    
    deck->is_playing = false;
    deck->is_paused = false;
    deck->position_ms = 0.0;
    
    // Trigger callback if track ended naturally
    if (deck->current_track && track_ended_callback_) {
        track_ended_callback_(deck_id);
    }
    
    Logger::info("RadioControl: Stopped playback on deck " + deck_id);
    return true;
}

// ===== MIXER OPERATIONS =====

bool RadioControl::set_crossfader_position(float position) {
    crossfader_position_ = std::clamp(position, -1.0f, 1.0f);
    
    if (!audio_system_->set_crossfader_position(crossfader_position_)) {
        return false;
    }
    
    update_mixer_output();
    return true;
}

bool RadioControl::set_master_volume(float volume) {
    master_volume_ = std::clamp(volume, 0.0f, 1.0f);
    
    if (!audio_system_->set_master_volume(master_volume_)) {
        return false;
    }
    
    return true;
}

// ===== MICROPHONE AND TALKOVER IMPLEMENTATION =====

bool RadioControl::enable_microphone(bool enabled) {
    Logger::info("RadioControl: " + std::string(enabled ? "Enabling" : "Disabling") + " microphone");
    
    microphone_enabled_ = enabled;
    
    if (!audio_system_->enable_microphone_input(enabled)) {
        Logger::error("RadioControl: Failed to enable/disable microphone in audio system");
        return false;
    }
    
    if (enabled) {
        // Apply current microphone gain when enabling
        audio_system_->set_microphone_gain(microphone_gain_);
        Logger::info("RadioControl: Microphone enabled with gain " + std::to_string(microphone_gain_));
    } else {
        // Disable talkover if microphone is disabled
        if (talkover_active_) {
            enable_talkover(false);
        }
        Logger::info("RadioControl: Microphone disabled");
    }
    
    return true;
}

bool RadioControl::set_microphone_gain(float gain) {
    microphone_gain_ = std::clamp(gain, 0.0f, 2.0f);
    
    if (microphone_enabled_) {
        if (!audio_system_->set_microphone_gain(microphone_gain_)) {
            Logger::error("RadioControl: Failed to set microphone gain");
            return false;
        }
    }
    
    Logger::info("RadioControl: Microphone gain set to " + std::to_string(microphone_gain_));
    return true;
}

bool RadioControl::set_microphone_mute(bool muted) {
    microphone_muted_ = muted;
    
    if (microphone_enabled_) {
        if (!audio_system_->set_microphone_mute(muted)) {
            Logger::error("RadioControl: Failed to mute/unmute microphone");
            return false;
        }
    }
    
    // Disable talkover if microphone is muted
    if (muted && talkover_active_) {
        enable_talkover(false);
    }
    
    Logger::info("RadioControl: Microphone " + std::string(muted ? "muted" : "unmuted"));
    return true;
}

bool RadioControl::is_microphone_enabled() {
    return microphone_enabled_;
}

bool RadioControl::is_microphone_muted() {
    return microphone_muted_;
}

float RadioControl::get_microphone_gain() {
    return microphone_gain_;
}

bool RadioControl::enable_talkover(bool enabled) {
    Logger::info("RadioControl: " + std::string(enabled ? "Enabling" : "Disabling") + " talkover");
    
    // Can only enable talkover if microphone is enabled and not muted
    if (enabled && (!microphone_enabled_ || microphone_muted_)) {
        Logger::error("RadioControl: Cannot enable talkover - microphone must be enabled and unmuted");
        return false;
    }
    
    talkover_active_ = enabled;
    
    if (enabled) {
        // Store current master volume before ducking
        original_master_volume_ = master_volume_;
        
        // Calculate ducked volume
        float ducked_volume = master_volume_ * talkover_duck_level_;
        
        // Apply ducking with fade
        if (!audio_system_->fade_master_volume(ducked_volume, talkover_duck_time_)) {
            // Fallback to instant volume change if fade not supported
            set_master_volume(ducked_volume);
        }
        
        Logger::info("RadioControl: Talkover enabled - Master volume ducked from " + 
                    std::to_string(original_master_volume_) + " to " + std::to_string(ducked_volume));
    } else {
        // Restore original volume with fade
        if (!audio_system_->fade_master_volume(original_master_volume_, talkover_duck_time_)) {
            // Fallback to instant volume change if fade not supported  
            set_master_volume(original_master_volume_);
        }
        
        Logger::info("RadioControl: Talkover disabled - Master volume restored to " + 
                    std::to_string(original_master_volume_));
    }
    
    return true;
}

bool RadioControl::set_talkover_duck_level(float level) {
    talkover_duck_level_ = std::clamp(level, 0.0f, 1.0f);
    
    // If talkover is currently active, update the ducked volume
    if (talkover_active_) {
        float ducked_volume = original_master_volume_ * talkover_duck_level_;
        set_master_volume(ducked_volume);
        
        Logger::info("RadioControl: Updated talkover duck level to " + std::to_string(level) + 
                    " - Current ducked volume: " + std::to_string(ducked_volume));
    }
    
    return true;
}

bool RadioControl::set_talkover_duck_time(float time_ms) {
    talkover_duck_time_ = std::clamp(time_ms, 10.0f, 5000.0f); // 10ms to 5s
    Logger::info("RadioControl: Talkover duck time set to " + std::to_string(talkover_duck_time_) + "ms");
    return true;
}

bool RadioControl::is_talkover_active() {
    return talkover_active_;
}

float RadioControl::get_talkover_duck_level() {
    return talkover_duck_level_;
}

// ===== WAVEFORM AND AUDIO VISUALIZATION =====

RadioControl::WaveformData RadioControl::get_deck_waveform(const std::string& deck_id) {
    auto deck_it = decks_.find(deck_id);
    if (deck_it == decks_.end() || !deck_it->second->current_track) {
        return WaveformData{};
    }
    
    const RadioTrack* track = deck_it->second->current_track;
    
    // Check if waveform is cached
    auto cache_it = waveform_cache_.find(track->id);
    if (cache_it != waveform_cache_.end()) {
        WaveformData data = cache_it->second;
        // Update current position
        data.current_position_ms = deck_it->second->position_ms;
        return data;
    }
    
    // Generate waveform if not cached
    if (generate_waveform_data(track->id)) {
        return get_deck_waveform(deck_id); // Recursive call now that it's cached
    }
    
    return WaveformData{};
}

bool RadioControl::generate_waveform_data(const std::string& track_id, int width_pixels) {
    Logger::info("RadioControl: Generating waveform data for track " + track_id);
    
    auto track_it = tracks_.find(track_id);
    if (track_it == tracks_.end()) {
        Logger::error("RadioControl: Track not found for waveform generation: " + track_id);
        return false;
    }
    
    const RadioTrack& track = track_it->second;
    
    // Get waveform data from audio system
    WaveformData waveform;
    if (!audio_system_->generate_waveform(track.file_path, width_pixels, waveform.peaks, waveform.rms)) {
        Logger::error("RadioControl: Failed to generate waveform data from audio system");
        return false;
    }
    
    // Set metadata
    waveform.duration_ms = track.duration_ms;
    waveform.samples_per_pixel = (track.duration_ms * 44.1) / width_pixels; // Approximate
    waveform.sample_rate = 44100;
    waveform.current_position_ms = 0.0;
    
    // Cache the waveform
    waveform_cache_[track_id] = waveform;
    
    Logger::info("RadioControl: Generated waveform with " + std::to_string(waveform.peaks.size()) + 
                " data points for " + track.title);
    return true;
}

RadioControl::RealTimeAudioLevels RadioControl::get_real_time_levels() {
    RealTimeAudioLevels levels;
    
    if (!audio_monitoring_active_) {
        return levels; // Return zeroed levels if monitoring is not active
    }
    
    // Get master output levels
    auto master_levels = audio_system_->get_master_audio_levels();
    levels.left_peak = master_levels.left_peak;
    levels.right_peak = master_levels.right_peak;
    levels.left_rms = master_levels.left_rms;
    levels.right_rms = master_levels.right_rms;
    levels.is_clipping = master_levels.clipping;
    
    // Get microphone level if enabled
    if (microphone_enabled_ && !microphone_muted_) {
        levels.microphone_level = audio_system_->get_microphone_level();
    } else {
        levels.microphone_level = 0.0f;
    }
    
    // Set ducking status
    levels.is_ducked = talkover_active_;
    
    // Set timestamp for smooth animations
    levels.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // Cache current levels
    current_levels_ = levels;
    
    return levels;
}

bool RadioControl::start_audio_monitoring() {
    Logger::info("RadioControl: Starting real-time audio monitoring");
    
    if (!audio_system_->enable_level_monitoring(true)) {
        Logger::error("RadioControl: Failed to enable level monitoring in audio system");
        return false;
    }
    
    audio_monitoring_active_ = true;
    Logger::info("RadioControl: Audio monitoring started successfully");
    return true;
}

bool RadioControl::stop_audio_monitoring() {
    Logger::info("RadioControl: Stopping real-time audio monitoring");
    
    audio_monitoring_active_ = false;
    
    if (!audio_system_->enable_level_monitoring(false)) {
        Logger::error("RadioControl: Failed to disable level monitoring in audio system");
        return false;
    }
    
    // Reset cached levels
    current_levels_ = RealTimeAudioLevels{};
    
    Logger::info("RadioControl: Audio monitoring stopped");
    return true;
}

// ===== RADIO STATION CONTROL =====

bool RadioControl::start_broadcast() {
    Logger::info("RadioControl: Starting broadcast");
    
    // Configure audio encoder with station settings
    StreamConfig config;
    config.protocol = StreamProtocol::ICECAST2;
    config.server_host = station_config_.stream_config.server_host;
    config.server_port = station_config_.stream_config.server_port;
    config.mount_point = station_config_.stream_config.mount_point;
    config.password = station_config_.stream_config.password;
    config.username = station_config_.stream_config.username;
    config.stream_name = station_config_.name;
    config.stream_description = station_config_.description;
    config.stream_genre = station_config_.genre;
    config.codec = StreamCodec::MP3;
    config.bitrate = station_config_.stream_config.bitrate;
    
    if (!audio_encoder_->configure(config)) {
        Logger::error("RadioControl: Failed to configure audio encoder");
        return false;
    }
    
    if (!audio_encoder_->connect()) {
        Logger::error("RadioControl: Failed to connect to streaming server");
        return false;
    }
    
    if (!audio_encoder_->start_streaming()) {
        Logger::error("RadioControl: Failed to start streaming");
        return false;
    }
    
    station_config_.is_live = true;
    
    // Start broadcast session logging
    std::string session_id = "broadcast_" + std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    database_->start_broadcast_session(session_id);
    
    Logger::info("RadioControl: Broadcast started successfully");
    return true;
}

bool RadioControl::stop_broadcast() {
    Logger::info("RadioControl: Stopping broadcast");
    
    if (!audio_encoder_->stop_streaming()) {
        Logger::error("RadioControl: Failed to stop streaming");
        return false;
    }
    
    if (!audio_encoder_->disconnect()) {
        Logger::error("RadioControl: Failed to disconnect from streaming server");
        return false;
    }
    
    station_config_.is_live = false;
    station_config_.listener_count = 0;
    
    Logger::info("RadioControl: Broadcast stopped successfully");
    return true;
}

bool RadioControl::update_stream_metadata(const std::string& artist, const std::string& title) {
    if (!audio_encoder_->update_metadata(artist, title)) {
        Logger::error("RadioControl: Failed to update stream metadata");
        return false;
    }
    
    Logger::info("RadioControl: Updated stream metadata - " + artist + " - " + title);
    return true;
}

// ===== DATABASE INTEGRATION =====

bool RadioControl::save_to_database() {
    Logger::info("RadioControl: Saving state to database");
    
    // Save station configuration
    if (!database_->save_station_config(station_config_)) {
        Logger::error("RadioControl: Failed to save station config");
        return false;
    }
    
    // Save all tracks (they should already be in database, but update any changes)
    for (const auto& [id, track] : tracks_) {
        database_->update_track(track);
    }
    
    // Save all playlists
    for (const auto& [id, playlist] : playlists_) {
        database_->update_playlist(playlist);
    }
    
    Logger::info("RadioControl: State saved to database successfully");
    return true;
}

bool RadioControl::load_from_database() {
    Logger::info("RadioControl: Loading state from database");
    
    // Load station configuration
    station_config_ = database_->get_station_config();
    
    // Load all tracks
    auto db_tracks = database_->get_all_tracks();
    tracks_.clear();
    for (const auto& track : db_tracks) {
        tracks_[track.id] = track;
    }
    
    // Load all playlists
    auto db_playlists = database_->get_all_playlists();
    playlists_.clear();
    for (const auto& playlist : db_playlists) {
        playlists_[playlist.id] = playlist;
    }
    
    Logger::info("RadioControl: Loaded " + std::to_string(tracks_.size()) + 
                 " tracks and " + std::to_string(playlists_.size()) + " playlists");
    return true;
}

// ===== PRIVATE HELPER METHODS =====

std::string RadioControl::generate_track_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(10000000, 99999999);
    
    return "track_" + std::to_string(dis(gen));
}

std::string RadioControl::generate_playlist_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(10000000, 99999999);
    
    return "playlist_" + std::to_string(dis(gen));
}

void RadioControl::initialize_default_decks() {
    Logger::info("RadioControl: Initializing default decks");
    
    // Create Deck A
    auto deck_a = std::make_unique<DJDeck>();
    deck_a->id = "deck_a";
    deck_a->name = "Deck A";
    decks_["deck_a"] = std::move(deck_a);
    
    // Create Deck B
    auto deck_b = std::make_unique<DJDeck>();
    deck_b->id = "deck_b";
    deck_b->name = "Deck B";
    decks_["deck_b"] = std::move(deck_b);
    
    // Create audio channels in audio system
    audio_system_->create_audio_channel();  // This should return "deck_a"
    audio_system_->create_audio_channel();  // This should return "deck_b"
}

void RadioControl::update_mixer_output() {
    // Calculate crossfader mix based on position and curve
    float left_gain = 1.0f;
    float right_gain = 1.0f;
    
    if (crossfader_position_ < 0) {
        // Fading towards A (left)
        float fade_amount = -crossfader_position_;
        right_gain = 1.0f - (fade_amount * fade_amount * crossfader_curve_ + fade_amount * (1.0f - crossfader_curve_));
    } else if (crossfader_position_ > 0) {
        // Fading towards B (right)
        float fade_amount = crossfader_position_;
        left_gain = 1.0f - (fade_amount * fade_amount * crossfader_curve_ + fade_amount * (1.0f - crossfader_curve_));
    }
    
    // Apply crossfader gains to decks
    if (decks_.find("deck_a") != decks_.end()) {
        DJDeck* deck_a = decks_["deck_a"].get();
        float final_gain = deck_a->volume * deck_a->gain * left_gain;
        audio_system_->set_channel_volume("deck_a", final_gain);
    }
    
    if (decks_.find("deck_b") != decks_.end()) {
        DJDeck* deck_b = decks_["deck_b"].get();
        float final_gain = deck_b->volume * deck_b->gain * right_gain;
        audio_system_->set_channel_volume("deck_b", final_gain);
    }
}

bool RadioControl::validate_track_file(const std::string& file_path) {
    std::filesystem::path path(file_path);
    
    // Check if file exists
    if (!std::filesystem::exists(path)) {
        return false;
    }
    
    // Check if it's a regular file
    if (!std::filesystem::is_regular_file(path)) {
        return false;
    }
    
    // Check file extension
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    std::vector<std::string> supported_formats = {".mp3", ".wav", ".flac", ".ogg", ".aac", ".m4a"};
    return std::find(supported_formats.begin(), supported_formats.end(), extension) != supported_formats.end();
}

json RadioControl::extract_metadata_from_file(const std::string& file_path) {
    json metadata;
    
    // This would typically use a library like TagLib to extract ID3 tags
    // For now, we'll extract basic info from the filename and file system
    
    std::filesystem::path path(file_path);
    
    // Get file size
    try {
        metadata["file_size"] = std::filesystem::file_size(path);
    } catch (...) {
        metadata["file_size"] = 0;
    }
    
    // Extract title from filename (without extension)
    std::string filename = path.stem().string();
    
    // Try to parse "Artist - Title" format
    size_t dash_pos = filename.find(" - ");
    if (dash_pos != std::string::npos) {
        metadata["artist"] = filename.substr(0, dash_pos);
        metadata["title"] = filename.substr(dash_pos + 3);
    } else {
        metadata["title"] = filename;
        metadata["artist"] = "Unknown Artist";
    }
    
    // Set default values
    metadata["album"] = "";
    metadata["genre"] = "";
    metadata["duration_ms"] = 0;
    metadata["bpm"] = 0;
    metadata["key"] = "";
    
    return metadata;
}

// ===== CHANNEL CONTROL METHODS =====

bool RadioControl::load_audio_file(const std::string& channel_id, const std::string& file_path) {
    Logger::info("RadioControl: Loading audio file " + file_path + " into channel " + channel_id);
    
    if (!std::filesystem::exists(file_path)) {
        Logger::error("RadioControl: Audio file does not exist: " + file_path);
        return false;
    }
    
    if (!is_audio_file_supported(file_path)) {
        Logger::error("RadioControl: Unsupported audio file format: " + file_path);
        return false;
    }
    
    // Load the file using the audio system
    bool success = audio_system_->load_audio_file(channel_id, file_path);
    
    if (success) {
        Logger::info("RadioControl: Successfully loaded " + file_path + " into channel " + channel_id);
        
        // Extract metadata and create track entry
        json metadata = extract_metadata_from_file(file_path);
        
        RadioTrack track;
        track.id = channel_id + "_" + std::to_string(std::time(nullptr));
        track.title = metadata.value("title", "Unknown Track");
        track.artist = metadata.value("artist", "Unknown Artist");
        track.album = metadata.value("album", "");
        track.genre = metadata.value("genre", "");
        track.file_path = file_path;
        track.duration_ms = metadata.value("duration_ms", 0.0);
        track.bpm = metadata.value("bpm", 0);
        track.key = metadata.value("key", "");
        track.is_loaded = true;
        
        // Store track info
        tracks_[track.id] = track;
        
        // Store channel mapping
        if (channel_id == "A") {
            deck_a_track_id_ = track.id;
        } else if (channel_id == "B") {
            deck_b_track_id_ = track.id;
        }
        
        Logger::info("RadioControl: Created track entry with ID " + track.id);
    } else {
        Logger::error("RadioControl: Failed to load audio file into audio system");
    }
    
    return success;
}

bool RadioControl::set_channel_playback(const std::string& channel_id, bool play) {
    Logger::info("RadioControl: Setting channel " + channel_id + " playback to " + (play ? "play" : "pause"));
    
    // Use audio system to control playback
    bool success = audio_system_->set_channel_playback(channel_id, play);
    
    if (success) {
        // Update track state if we have track info
        std::string track_id = (channel_id == "A") ? deck_a_track_id_ : deck_b_track_id_;
        if (!track_id.empty() && tracks_.find(track_id) != tracks_.end()) {
            tracks_[track_id].is_playing = play;
        }
        
        Logger::info("RadioControl: Channel " + channel_id + " playback set to " + (play ? "playing" : "paused"));
    } else {
        Logger::error("RadioControl: Failed to set channel " + channel_id + " playback state");
    }
    
    return success;
}

bool RadioControl::set_channel_volume(const std::string& channel_id, float volume) {
    Logger::info("RadioControl: Setting channel " + channel_id + " volume to " + std::to_string(volume));
    
    // Clamp volume to valid range
    volume = std::max(0.0f, std::min(1.0f, volume));
    
    // Use audio system to set channel volume
    bool success = audio_system_->set_channel_volume(channel_id, volume);
    
    if (success) {
        Logger::info("RadioControl: Channel " + channel_id + " volume set to " + std::to_string(volume));
    } else {
        Logger::error("RadioControl: Failed to set channel " + channel_id + " volume");
    }
    
    return success;
}

bool RadioControl::set_channel_eq(const std::string& channel_id, float bass, float mid, float treble) {
    Logger::info("RadioControl: Setting channel " + channel_id + " EQ - Bass: " + 
                 std::to_string(bass) + ", Mid: " + std::to_string(mid) + ", Treble: " + std::to_string(treble));
    
    // Clamp EQ values to reasonable range (-20dB to +20dB)
    bass = std::max(-20.0f, std::min(20.0f, bass));
    mid = std::max(-20.0f, std::min(20.0f, mid));
    treble = std::max(-20.0f, std::min(20.0f, treble));
    
    // Use audio system to set EQ
    bool success = audio_system_->set_channel_eq(channel_id, bass, mid, treble);
    
    if (success) {
        Logger::info("RadioControl: Channel " + channel_id + " EQ set successfully");
    } else {
        Logger::error("RadioControl: Failed to set channel " + channel_id + " EQ");
    }
    
    return success;
}

WaveformData RadioControl::get_deck_waveform(const std::string& deck_id) {
    Logger::info("RadioControl: Getting waveform for deck " + deck_id);
    
    std::string track_id = (deck_id == "A" || deck_id == "a") ? deck_a_track_id_ : deck_b_track_id_;
    
    if (track_id.empty()) {
        Logger::warn("RadioControl: No track loaded on deck " + deck_id);
        return WaveformData{};
    }
    
    // Check if waveform is cached
    auto cache_it = waveform_cache_.find(track_id);
    if (cache_it != waveform_cache_.end()) {
        Logger::info("RadioControl: Returning cached waveform for deck " + deck_id);
        return cache_it->second;
    }
    
    // Generate waveform if not cached
    if (generate_waveform_data(track_id, 1000)) { // Default 1000 pixels width
        auto new_cache_it = waveform_cache_.find(track_id);
        if (new_cache_it != waveform_cache_.end()) {
            Logger::info("RadioControl: Generated and returning waveform for deck " + deck_id);
            return new_cache_it->second;
        }
    }
    
    Logger::error("RadioControl: Failed to get waveform for deck " + deck_id);
    return WaveformData{};
}