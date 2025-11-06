#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include "radio_control.hpp"

using json = nlohmann::json;

/**
 * Database Manager for persistent storage of radio data
 * Uses SQLite for local database operations
 */
class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();
    
    // Database initialization
    bool initialize(const std::string& db_path = "radio_database.db");
    void close();
    bool is_connected() const;
    
    // Database maintenance
    bool create_tables();
    bool migrate_database();
    bool vacuum_database();
    bool backup_database(const std::string& backup_path);
    bool restore_database(const std::string& backup_path);
    
    // ===== TRACK OPERATIONS =====
    
    bool insert_track(const RadioTrack& track);
    bool update_track(const RadioTrack& track);
    bool delete_track(const std::string& track_id);
    RadioTrack* get_track(const std::string& track_id);
    std::vector<RadioTrack> get_all_tracks();
    std::vector<RadioTrack> search_tracks(const std::string& query);
    std::vector<RadioTrack> get_tracks_by_genre(const std::string& genre);
    std::vector<RadioTrack> get_tracks_by_artist(const std::string& artist);
    std::vector<RadioTrack> get_tracks_by_bpm_range(int min_bpm, int max_bpm);
    
    // Track statistics
    bool increment_play_count(const std::string& track_id);
    bool update_last_played(const std::string& track_id);
    std::vector<RadioTrack> get_most_played_tracks(int limit = 10);
    std::vector<RadioTrack> get_recently_played_tracks(int limit = 10);
    std::vector<RadioTrack> get_recently_added_tracks(int limit = 10);
    
    // ===== PLAYLIST OPERATIONS =====
    
    bool insert_playlist(const RadioPlaylist& playlist);
    bool update_playlist(const RadioPlaylist& playlist);
    bool delete_playlist(const std::string& playlist_id);
    RadioPlaylist* get_playlist(const std::string& playlist_id);
    std::vector<RadioPlaylist> get_all_playlists();
    
    // Playlist-track relationships
    bool add_track_to_playlist(const std::string& playlist_id, const std::string& track_id, int position = -1);
    bool remove_track_from_playlist(const std::string& playlist_id, const std::string& track_id);
    bool reorder_playlist_tracks(const std::string& playlist_id, const std::vector<std::string>& track_order);
    std::vector<RadioTrack> get_playlist_tracks(const std::string& playlist_id);
    
    // ===== STATION CONFIGURATION =====
    
    bool save_station_config(const RadioStation& station);
    RadioStation get_station_config();
    bool update_station_metadata(const std::string& name, const std::string& description);
    
    // ===== BROADCAST HISTORY =====
    
    struct BroadcastSession {
        std::string id;
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        int duration_minutes = 0;
        int peak_listeners = 0;
        std::vector<std::string> played_track_ids;
        json metadata;
    };
    
    bool start_broadcast_session(const std::string& session_id);
    bool end_broadcast_session(const std::string& session_id, int peak_listeners = 0);
    bool log_track_play(const std::string& session_id, const std::string& track_id, 
                       std::chrono::system_clock::time_point timestamp);
    std::vector<BroadcastSession> get_broadcast_history(int limit = 50);
    BroadcastSession get_broadcast_session(const std::string& session_id);
    
    // ===== CUE POINTS AND LOOPS =====
    
    struct CuePointData {
        std::string id;
        std::string track_id;
        double position_ms;
        std::string label;
        bool is_loop_start = false;
        bool is_loop_end = false;
        std::chrono::system_clock::time_point created_at;
    };
    
    bool save_cue_point(const std::string& track_id, double position_ms, 
                       const std::string& label, bool is_loop_start = false, bool is_loop_end = false);
    bool delete_cue_point(const std::string& cue_point_id);
    std::vector<CuePointData> get_track_cue_points(const std::string& track_id);
    bool clear_track_cue_points(const std::string& track_id);
    
    // ===== HOT CUES =====
    
    struct HotCueData {
        std::string track_id;
        int hot_cue_index; // 0-7
        double position_ms;
        std::string label;
        std::chrono::system_clock::time_point created_at;
    };
    
    bool save_hot_cue(const std::string& track_id, int hot_cue_index, 
                     double position_ms, const std::string& label = "");
    bool delete_hot_cue(const std::string& track_id, int hot_cue_index);
    std::vector<HotCueData> get_track_hot_cues(const std::string& track_id);
    bool clear_track_hot_cues(const std::string& track_id);
    
    // ===== SETTINGS AND PREFERENCES =====
    
    bool save_setting(const std::string& key, const std::string& value);
    std::string get_setting(const std::string& key, const std::string& default_value = "");
    bool delete_setting(const std::string& key);
    std::map<std::string, std::string> get_all_settings();
    
    // ===== ANALYTICS AND STATISTICS =====
    
    struct LibraryStats {
        int total_tracks = 0;
        int total_playlists = 0;
        int total_playtime_minutes = 0;
        int total_broadcasts = 0;
        std::string most_played_genre;
        std::string most_played_artist;
        float average_track_bpm = 0.0f;
        int total_cue_points = 0;
    };
    
    LibraryStats get_library_statistics();
    
    struct GenreStats {
        std::string genre;
        int track_count = 0;
        int play_count = 0;
        float percentage = 0.0f;
    };
    
    std::vector<GenreStats> get_genre_statistics();
    
    // ===== IMPORT/EXPORT =====
    
    bool export_library_to_json(const std::string& file_path);
    bool import_library_from_json(const std::string& file_path);
    bool export_playlist_to_m3u(const std::string& playlist_id, const std::string& file_path);
    bool import_playlist_from_m3u(const std::string& file_path, const std::string& playlist_name);
    
    // ===== DATABASE QUERIES =====
    
    // Custom query execution
    json execute_custom_query(const std::string& sql);
    
    // Transaction support
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

private:
    sqlite3* db_;
    bool is_connected_;
    std::string db_path_;
    
    // Prepared statements for common operations
    struct PreparedStatements {
        sqlite3_stmt* insert_track = nullptr;
        sqlite3_stmt* update_track = nullptr;
        sqlite3_stmt* delete_track = nullptr;
        sqlite3_stmt* get_track = nullptr;
        sqlite3_stmt* search_tracks = nullptr;
        
        sqlite3_stmt* insert_playlist = nullptr;
        sqlite3_stmt* update_playlist = nullptr;
        sqlite3_stmt* delete_playlist = nullptr;
        sqlite3_stmt* get_playlist = nullptr;
        
        sqlite3_stmt* add_playlist_track = nullptr;
        sqlite3_stmt* remove_playlist_track = nullptr;
        sqlite3_stmt* get_playlist_tracks = nullptr;
        
        sqlite3_stmt* save_cue_point = nullptr;
        sqlite3_stmt* delete_cue_point = nullptr;
        sqlite3_stmt* get_track_cue_points = nullptr;
        
        sqlite3_stmt* save_hot_cue = nullptr;
        sqlite3_stmt* delete_hot_cue = nullptr;
        sqlite3_stmt* get_track_hot_cues = nullptr;
        
        sqlite3_stmt* increment_play_count = nullptr;
        sqlite3_stmt* update_last_played = nullptr;
        
        sqlite3_stmt* save_setting = nullptr;
        sqlite3_stmt* get_setting = nullptr;
        sqlite3_stmt* delete_setting = nullptr;
    } prepared_statements_;
    
    // Helper methods
    bool prepare_statements();
    void finalize_statements();
    RadioTrack track_from_statement(sqlite3_stmt* stmt);
    RadioPlaylist playlist_from_statement(sqlite3_stmt* stmt);
    CuePointData cue_point_from_statement(sqlite3_stmt* stmt);
    HotCueData hot_cue_from_statement(sqlite3_stmt* stmt);
    
    // Database schema
    static const char* CREATE_TRACKS_TABLE;
    static const char* CREATE_PLAYLISTS_TABLE;
    static const char* CREATE_PLAYLIST_TRACKS_TABLE;
    static const char* CREATE_CUE_POINTS_TABLE;
    static const char* CREATE_HOT_CUES_TABLE;
    static const char* CREATE_BROADCAST_SESSIONS_TABLE;
    static const char* CREATE_BROADCAST_TRACKS_TABLE;
    static const char* CREATE_STATION_CONFIG_TABLE;
    static const char* CREATE_SETTINGS_TABLE;
    
    // Indices for performance
    static const char* CREATE_TRACKS_INDICES;
    static const char* CREATE_PLAYLISTS_INDICES;
    static const char* CREATE_PLAYLIST_TRACKS_INDICES;
    static const char* CREATE_CUE_POINTS_INDICES;
    static const char* CREATE_HOT_CUES_INDICES;
    
    // Error handling
    void log_sqlite_error(const std::string& operation);
    std::string get_sqlite_error_message();
};

// Database schema SQL constants
const char* DatabaseManager::CREATE_TRACKS_TABLE = R"(
    CREATE TABLE IF NOT EXISTS tracks (
        id TEXT PRIMARY KEY,
        title TEXT NOT NULL,
        artist TEXT NOT NULL,
        album TEXT,
        genre TEXT,
        file_path TEXT NOT NULL UNIQUE,
        duration_ms INTEGER DEFAULT 0,
        bpm INTEGER DEFAULT 0,
        musical_key TEXT,
        gain REAL DEFAULT 1.0,
        is_analyzed INTEGER DEFAULT 0,
        play_count INTEGER DEFAULT 0,
        added_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        last_played TIMESTAMP,
        file_size INTEGER,
        file_hash TEXT,
        metadata_json TEXT
    )
)";

const char* DatabaseManager::CREATE_PLAYLISTS_TABLE = R"(
    CREATE TABLE IF NOT EXISTS playlists (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        description TEXT,
        is_active INTEGER DEFAULT 0,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        track_count INTEGER DEFAULT 0,
        total_duration_ms INTEGER DEFAULT 0
    )
)";

const char* DatabaseManager::CREATE_PLAYLIST_TRACKS_TABLE = R"(
    CREATE TABLE IF NOT EXISTS playlist_tracks (
        playlist_id TEXT NOT NULL,
        track_id TEXT NOT NULL,
        position INTEGER NOT NULL,
        added_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (playlist_id, track_id),
        FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE,
        FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
    )
)";

const char* DatabaseManager::CREATE_CUE_POINTS_TABLE = R"(
    CREATE TABLE IF NOT EXISTS cue_points (
        id TEXT PRIMARY KEY,
        track_id TEXT NOT NULL,
        position_ms REAL NOT NULL,
        label TEXT,
        is_loop_start INTEGER DEFAULT 0,
        is_loop_end INTEGER DEFAULT 0,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
    )
)";

const char* DatabaseManager::CREATE_HOT_CUES_TABLE = R"(
    CREATE TABLE IF NOT EXISTS hot_cues (
        track_id TEXT NOT NULL,
        hot_cue_index INTEGER NOT NULL,
        position_ms REAL NOT NULL,
        label TEXT,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (track_id, hot_cue_index),
        FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE,
        CHECK (hot_cue_index >= 0 AND hot_cue_index <= 7)
    )
)";

const char* DatabaseManager::CREATE_BROADCAST_SESSIONS_TABLE = R"(
    CREATE TABLE IF NOT EXISTS broadcast_sessions (
        id TEXT PRIMARY KEY,
        start_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        end_time TIMESTAMP,
        duration_minutes INTEGER,
        peak_listeners INTEGER DEFAULT 0,
        metadata_json TEXT
    )
)";

const char* DatabaseManager::CREATE_BROADCAST_TRACKS_TABLE = R"(
    CREATE TABLE IF NOT EXISTS broadcast_tracks (
        session_id TEXT NOT NULL,
        track_id TEXT NOT NULL,
        played_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (session_id) REFERENCES broadcast_sessions(id) ON DELETE CASCADE,
        FOREIGN KEY (track_id) REFERENCES tracks(id) ON DELETE CASCADE
    )
)";

const char* DatabaseManager::CREATE_STATION_CONFIG_TABLE = R"(
    CREATE TABLE IF NOT EXISTS station_config (
        key TEXT PRIMARY KEY,
        value TEXT NOT NULL,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
)";

const char* DatabaseManager::CREATE_SETTINGS_TABLE = R"(
    CREATE TABLE IF NOT EXISTS settings (
        key TEXT PRIMARY KEY,
        value TEXT NOT NULL,
        category TEXT,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
)";