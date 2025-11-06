#include "database_manager.hpp"
#include "utils/logger.hpp"
#include <filesystem>
#include <sstream>
#include <iomanip>

DatabaseManager::DatabaseManager() 
    : db_(nullptr)
    , is_connected_(false)
{
    Logger::info("DatabaseManager: Initializing database manager");
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::initialize(const std::string& db_path) {
    db_path_ = db_path;
    
    Logger::info("DatabaseManager: Opening database " + db_path_);
    
    // Open SQLite database
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        Logger::error("DatabaseManager: Failed to open database: " + std::string(sqlite3_errmsg(db_)));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    is_connected_ = true;
    
    // Enable foreign keys
    char* error_msg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &error_msg);
    if (rc != SQLITE_OK) {
        Logger::error("DatabaseManager: Failed to enable foreign keys: " + std::string(error_msg));
        sqlite3_free(error_msg);
    }
    
    // Create tables
    if (!create_tables()) {
        Logger::error("DatabaseManager: Failed to create tables");
        close();
        return false;
    }
    
    // Prepare statements
    if (!prepare_statements()) {
        Logger::error("DatabaseManager: Failed to prepare statements");
        close();
        return false;
    }
    
    Logger::info("DatabaseManager: Database initialized successfully");
    return true;
}

void DatabaseManager::close() {
    if (is_connected_) {
        finalize_statements();
        
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        
        is_connected_ = false;
        Logger::info("DatabaseManager: Database connection closed");
    }
}

bool DatabaseManager::is_connected() const {
    return is_connected_;
}

bool DatabaseManager::create_tables() {
    Logger::info("DatabaseManager: Creating database tables");
    
    const char* tables[] = {
        CREATE_TRACKS_TABLE,
        CREATE_PLAYLISTS_TABLE,
        CREATE_PLAYLIST_TRACKS_TABLE,
        CREATE_CUE_POINTS_TABLE,
        CREATE_HOT_CUES_TABLE,
        CREATE_BROADCAST_SESSIONS_TABLE,
        CREATE_BROADCAST_TRACKS_TABLE,
        CREATE_STATION_CONFIG_TABLE,
        CREATE_SETTINGS_TABLE
    };
    
    for (const char* sql : tables) {
        char* error_msg = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error_msg);
        if (rc != SQLITE_OK) {
            Logger::error("DatabaseManager: Failed to create table: " + std::string(error_msg));
            sqlite3_free(error_msg);
            return false;
        }
    }
    
    // Create indices for performance
    const char* indices[] = {
        "CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_genre ON tracks(genre);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_bpm ON tracks(bpm);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_added_at ON tracks(added_at);",
        "CREATE INDEX IF NOT EXISTS idx_tracks_last_played ON tracks(last_played);",
        "CREATE INDEX IF NOT EXISTS idx_playlist_tracks_position ON playlist_tracks(playlist_id, position);",
        "CREATE INDEX IF NOT EXISTS idx_cue_points_track ON cue_points(track_id);",
        "CREATE INDEX IF NOT EXISTS idx_hot_cues_track ON hot_cues(track_id);",
        "CREATE INDEX IF NOT EXISTS idx_broadcast_tracks_session ON broadcast_tracks(session_id);"
    };
    
    for (const char* sql : indices) {
        char* error_msg = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error_msg);
        if (rc != SQLITE_OK) {
            Logger::warn("DatabaseManager: Failed to create index: " + std::string(error_msg));
            sqlite3_free(error_msg);
        }
    }
    
    Logger::info("DatabaseManager: Tables created successfully");
    return true;
}

bool DatabaseManager::prepare_statements() {
    Logger::info("DatabaseManager: Preparing SQL statements");
    
    // Track operations
    const char* insert_track_sql = R"(
        INSERT INTO tracks (id, title, artist, album, genre, file_path, duration_ms, bpm, 
                           musical_key, gain, is_analyzed, play_count, added_at, file_size, metadata_json)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    const char* update_track_sql = R"(
        UPDATE tracks SET title = ?, artist = ?, album = ?, genre = ?, duration_ms = ?, 
                         bpm = ?, musical_key = ?, gain = ?, is_analyzed = ?, play_count = ?, 
                         last_played = ?, metadata_json = ?
        WHERE id = ?
    )";
    
    const char* get_track_sql = "SELECT * FROM tracks WHERE id = ?";
    const char* search_tracks_sql = R"(
        SELECT * FROM tracks WHERE title LIKE ? OR artist LIKE ? OR album LIKE ? OR genre LIKE ?
        ORDER BY title, artist
    )";
    
    // Playlist operations
    const char* insert_playlist_sql = R"(
        INSERT INTO playlists (id, name, description, is_active, created_at, track_count, total_duration_ms)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";
    
    const char* update_playlist_sql = R"(
        UPDATE playlists SET name = ?, description = ?, is_active = ?, updated_at = CURRENT_TIMESTAMP,
                            track_count = ?, total_duration_ms = ?
        WHERE id = ?
    )";
    
    const char* get_playlist_sql = "SELECT * FROM playlists WHERE id = ?";
    
    // Playlist-track relationships
    const char* add_playlist_track_sql = R"(
        INSERT OR REPLACE INTO playlist_tracks (playlist_id, track_id, position)
        VALUES (?, ?, ?)
    )";
    
    const char* remove_playlist_track_sql = R"(
        DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?
    )";
    
    const char* get_playlist_tracks_sql = R"(
        SELECT t.* FROM tracks t
        JOIN playlist_tracks pt ON t.id = pt.track_id
        WHERE pt.playlist_id = ?
        ORDER BY pt.position
    )";
    
    // Cue points
    const char* save_cue_point_sql = R"(
        INSERT INTO cue_points (id, track_id, position_ms, label, is_loop_start, is_loop_end)
        VALUES (?, ?, ?, ?, ?, ?)
    )";
    
    const char* get_track_cue_points_sql = R"(
        SELECT * FROM cue_points WHERE track_id = ? ORDER BY position_ms
    )";
    
    // Hot cues
    const char* save_hot_cue_sql = R"(
        INSERT OR REPLACE INTO hot_cues (track_id, hot_cue_index, position_ms, label)
        VALUES (?, ?, ?, ?)
    )";
    
    const char* get_track_hot_cues_sql = R"(
        SELECT * FROM hot_cues WHERE track_id = ? ORDER BY hot_cue_index
    )";
    
    // Statistics
    const char* increment_play_count_sql = R"(
        UPDATE tracks SET play_count = play_count + 1 WHERE id = ?
    )";
    
    const char* update_last_played_sql = R"(
        UPDATE tracks SET last_played = CURRENT_TIMESTAMP WHERE id = ?
    )";
    
    // Settings
    const char* save_setting_sql = R"(
        INSERT OR REPLACE INTO settings (key, value, updated_at) VALUES (?, ?, CURRENT_TIMESTAMP)
    )";
    
    const char* get_setting_sql = "SELECT value FROM settings WHERE key = ?";
    
    // Prepare all statements
    struct {
        sqlite3_stmt** stmt;
        const char* sql;
    } statements[] = {
        {&prepared_statements_.insert_track, insert_track_sql},
        {&prepared_statements_.update_track, update_track_sql},
        {&prepared_statements_.get_track, get_track_sql},
        {&prepared_statements_.search_tracks, search_tracks_sql},
        {&prepared_statements_.insert_playlist, insert_playlist_sql},
        {&prepared_statements_.update_playlist, update_playlist_sql},
        {&prepared_statements_.get_playlist, get_playlist_sql},
        {&prepared_statements_.add_playlist_track, add_playlist_track_sql},
        {&prepared_statements_.remove_playlist_track, remove_playlist_track_sql},
        {&prepared_statements_.get_playlist_tracks, get_playlist_tracks_sql},
        {&prepared_statements_.save_cue_point, save_cue_point_sql},
        {&prepared_statements_.get_track_cue_points, get_track_cue_points_sql},
        {&prepared_statements_.save_hot_cue, save_hot_cue_sql},
        {&prepared_statements_.get_track_hot_cues, get_track_hot_cues_sql},
        {&prepared_statements_.increment_play_count, increment_play_count_sql},
        {&prepared_statements_.update_last_played, update_last_played_sql},
        {&prepared_statements_.save_setting, save_setting_sql},
        {&prepared_statements_.get_setting, get_setting_sql}
    };
    
    for (const auto& stmt_info : statements) {
        int rc = sqlite3_prepare_v2(db_, stmt_info.sql, -1, stmt_info.stmt, nullptr);
        if (rc != SQLITE_OK) {
            log_sqlite_error("prepare statement");
            finalize_statements();
            return false;
        }
    }
    
    Logger::info("DatabaseManager: SQL statements prepared successfully");
    return true;
}

void DatabaseManager::finalize_statements() {
    sqlite3_stmt* statements[] = {
        prepared_statements_.insert_track,
        prepared_statements_.update_track,
        prepared_statements_.delete_track,
        prepared_statements_.get_track,
        prepared_statements_.search_tracks,
        prepared_statements_.insert_playlist,
        prepared_statements_.update_playlist,
        prepared_statements_.delete_playlist,
        prepared_statements_.get_playlist,
        prepared_statements_.add_playlist_track,
        prepared_statements_.remove_playlist_track,
        prepared_statements_.get_playlist_tracks,
        prepared_statements_.save_cue_point,
        prepared_statements_.delete_cue_point,
        prepared_statements_.get_track_cue_points,
        prepared_statements_.save_hot_cue,
        prepared_statements_.delete_hot_cue,
        prepared_statements_.get_track_hot_cues,
        prepared_statements_.increment_play_count,
        prepared_statements_.update_last_played,
        prepared_statements_.save_setting,
        prepared_statements_.get_setting,
        prepared_statements_.delete_setting
    };
    
    for (sqlite3_stmt* stmt : statements) {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }
    
    // Reset structure
    memset(&prepared_statements_, 0, sizeof(prepared_statements_));
}

// ===== TRACK OPERATIONS =====

bool DatabaseManager::insert_track(const RadioTrack& track) {
    if (!prepared_statements_.insert_track) return false;
    
    sqlite3_stmt* stmt = prepared_statements_.insert_track;
    sqlite3_reset(stmt);
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, track.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, track.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, track.artist.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, track.album.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, track.genre.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, track.file_path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, track.duration_ms);
    sqlite3_bind_int(stmt, 8, track.bpm);
    sqlite3_bind_text(stmt, 9, track.key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 10, track.gain);
    sqlite3_bind_int(stmt, 11, track.is_analyzed ? 1 : 0);
    sqlite3_bind_int(stmt, 12, track.play_count);
    
    // Convert time_point to string
    auto time_t = std::chrono::system_clock::to_time_t(track.added_at);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    sqlite3_bind_text(stmt, 13, ss.str().c_str(), -1, SQLITE_TRANSIENT);
    
    sqlite3_bind_int64(stmt, 14, 0); // file_size - to be implemented
    sqlite3_bind_text(stmt, 15, "{}", -1, SQLITE_STATIC); // metadata_json
    
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log_sqlite_error("insert track");
        return false;
    }
    
    return true;
}

bool DatabaseManager::update_track(const RadioTrack& track) {
    if (!prepared_statements_.update_track) return false;
    
    sqlite3_stmt* stmt = prepared_statements_.update_track;
    sqlite3_reset(stmt);
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, track.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, track.artist.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, track.album.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, track.genre.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, track.duration_ms);
    sqlite3_bind_int(stmt, 6, track.bpm);
    sqlite3_bind_text(stmt, 7, track.key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 8, track.gain);
    sqlite3_bind_int(stmt, 9, track.is_analyzed ? 1 : 0);
    sqlite3_bind_int(stmt, 10, track.play_count);
    
    // last_played
    if (track.last_played != std::chrono::system_clock::time_point{}) {
        auto time_t = std::chrono::system_clock::to_time_t(track.last_played);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
        sqlite3_bind_text(stmt, 11, ss.str().c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 11);
    }
    
    sqlite3_bind_text(stmt, 12, "{}", -1, SQLITE_STATIC); // metadata_json
    sqlite3_bind_text(stmt, 13, track.id.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    return (rc == SQLITE_DONE);
}

bool DatabaseManager::delete_track(const std::string& track_id) {
    const char* sql = "DELETE FROM tracks WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE);
}

RadioTrack* DatabaseManager::get_track(const std::string& track_id) {
    if (!prepared_statements_.get_track) return nullptr;
    
    sqlite3_stmt* stmt = prepared_statements_.get_track;
    sqlite3_reset(stmt);
    
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        static RadioTrack track;
        track = track_from_statement(stmt);
        return &track;
    }
    
    return nullptr;
}

std::vector<RadioTrack> DatabaseManager::get_all_tracks() {
    std::vector<RadioTrack> tracks;
    
    const char* sql = "SELECT * FROM tracks ORDER BY title, artist";
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return tracks;
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        tracks.push_back(track_from_statement(stmt));
    }
    
    sqlite3_finalize(stmt);
    return tracks;
}

std::vector<RadioTrack> DatabaseManager::search_tracks(const std::string& query) {
    std::vector<RadioTrack> tracks;
    
    if (!prepared_statements_.search_tracks) return tracks;
    
    sqlite3_stmt* stmt = prepared_statements_.search_tracks;
    sqlite3_reset(stmt);
    
    std::string search_pattern = "%" + query + "%";
    for (int i = 1; i <= 4; i++) {
        sqlite3_bind_text(stmt, i, search_pattern.c_str(), -1, SQLITE_TRANSIENT);
    }
    
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        tracks.push_back(track_from_statement(stmt));
    }
    
    return tracks;
}

// ===== TRACK STATISTICS =====

bool DatabaseManager::increment_play_count(const std::string& track_id) {
    if (!prepared_statements_.increment_play_count) return false;
    
    sqlite3_stmt* stmt = prepared_statements_.increment_play_count;
    sqlite3_reset(stmt);
    
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    return (rc == SQLITE_DONE);
}

bool DatabaseManager::update_last_played(const std::string& track_id) {
    if (!prepared_statements_.update_last_played) return false;
    
    sqlite3_stmt* stmt = prepared_statements_.update_last_played;
    sqlite3_reset(stmt);
    
    sqlite3_bind_text(stmt, 1, track_id.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    return (rc == SQLITE_DONE);
}

// ===== STATION CONFIGURATION =====

bool DatabaseManager::save_station_config(const RadioStation& station) {
    if (!is_connected_) return false;
    
    json config_json = station.to_json();
    
    const char* sql = R"(
        INSERT OR REPLACE INTO station_config (key, value, updated_at)
        VALUES ('station_config', ?, CURRENT_TIMESTAMP)
    )";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    std::string config_str = config_json.dump();
    sqlite3_bind_text(stmt, 1, config_str.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE);
}

RadioStation DatabaseManager::get_station_config() {
    RadioStation station;
    
    const char* sql = "SELECT value FROM station_config WHERE key = 'station_config'";
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return station;
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char* json_str = (const char*)sqlite3_column_text(stmt, 0);
        if (json_str) {
            try {
                json config_json = json::parse(json_str);
                
                station.id = config_json.value("id", "");
                station.name = config_json.value("name", "");
                station.description = config_json.value("description", "");
                station.logo_url = config_json.value("logo_url", "");
                station.website_url = config_json.value("website_url", "");
                station.genre = config_json.value("genre", "");
                station.language = config_json.value("language", "");
                station.country = config_json.value("country", "");
                station.is_live = config_json.value("is_live", false);
                station.listener_count = config_json.value("listener_count", 0);
                
                if (config_json.contains("stream_config")) {
                    auto stream_config = config_json["stream_config"];
                    station.stream_config.server_host = stream_config.value("server_host", "localhost");
                    station.stream_config.server_port = stream_config.value("server_port", 8000);
                    station.stream_config.mount_point = stream_config.value("mount_point", "/stream");
                    station.stream_config.username = stream_config.value("username", "source");
                    station.stream_config.format = stream_config.value("format", "mp3");
                    station.stream_config.bitrate = stream_config.value("bitrate", 128);
                    station.stream_config.is_public = stream_config.value("is_public", true);
                }
            } catch (const std::exception& e) {
                Logger::error("DatabaseManager: Failed to parse station config JSON: " + std::string(e.what()));
            }
        }
    }
    
    sqlite3_finalize(stmt);
    return station;
}

// ===== HELPER METHODS =====

RadioTrack DatabaseManager::track_from_statement(sqlite3_stmt* stmt) {
    RadioTrack track;
    
    track.id = (char*)sqlite3_column_text(stmt, 0);
    track.title = (char*)sqlite3_column_text(stmt, 1);
    track.artist = (char*)sqlite3_column_text(stmt, 2);
    track.album = sqlite3_column_text(stmt, 3) ? (char*)sqlite3_column_text(stmt, 3) : "";
    track.genre = sqlite3_column_text(stmt, 4) ? (char*)sqlite3_column_text(stmt, 4) : "";
    track.file_path = (char*)sqlite3_column_text(stmt, 5);
    track.duration_ms = sqlite3_column_int(stmt, 6);
    track.bpm = sqlite3_column_int(stmt, 7);
    track.key = sqlite3_column_text(stmt, 8) ? (char*)sqlite3_column_text(stmt, 8) : "";
    track.gain = sqlite3_column_double(stmt, 9);
    track.is_analyzed = sqlite3_column_int(stmt, 10) != 0;
    track.play_count = sqlite3_column_int(stmt, 11);
    
    // Parse timestamps (columns 12, 13)
    // This would need proper timestamp parsing
    track.added_at = std::chrono::system_clock::now();
    track.last_played = std::chrono::system_clock::time_point{};
    
    return track;
}

void DatabaseManager::log_sqlite_error(const std::string& operation) {
    std::string error_msg = get_sqlite_error_message();
    Logger::error("DatabaseManager: " + operation + " failed: " + error_msg);
}

std::string DatabaseManager::get_sqlite_error_message() {
    if (db_) {
        return std::string(sqlite3_errmsg(db_));
    }
    return "No database connection";
}