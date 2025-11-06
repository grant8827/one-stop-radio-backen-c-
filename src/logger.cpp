#include "logger.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem>

// Static member definitions
Logger::Level Logger::current_level_ = Logger::Level::INFO;
std::string Logger::log_file_path_ = "";
std::mutex Logger::log_mutex_;
size_t Logger::max_file_size_ = 10 * 1024 * 1024; // 10MB default
bool Logger::rotate_logs_ = true;

void Logger::set_level(Level level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    current_level_ = level;
}

void Logger::set_log_file(const std::string& filepath, size_t max_size, bool rotate) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_file_path_ = filepath;
    max_file_size_ = max_size;
    rotate_logs_ = rotate;
}

std::string Logger::level_to_string(Level level) {
    switch (level) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO: return "INFO";
        case Level::WARN: return "WARN";
        case Level::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

void Logger::rotate_log_file() {
    if (!rotate_logs_ || log_file_path_.empty()) {
        return;
    }
    
    try {
        // Check if file exists and get its size
        if (!std::filesystem::exists(log_file_path_)) {
            return;
        }
        
        auto file_size = std::filesystem::file_size(log_file_path_);
        if (file_size < max_file_size_) {
            return;
        }
        
        // Create backup filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream backup_name;
        backup_name << log_file_path_ << "." 
                   << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        
        // Move current log to backup
        std::filesystem::rename(log_file_path_, backup_name.str());
        
        std::cout << "Log file rotated to: " << backup_name.str() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error rotating log file: " << e.what() << std::endl;
    }
}

void Logger::log(Level level, const std::string& message) {
    if (level < current_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    std::string timestamp = get_timestamp();
    std::string level_str = level_to_string(level);
    
    // Format: [TIMESTAMP] [LEVEL] MESSAGE
    std::string log_entry = "[" + timestamp + "] [" + level_str + "] " + message;
    
    // Always output to console
    if (level >= Level::ERROR) {
        std::cerr << log_entry << std::endl;
    } else {
        std::cout << log_entry << std::endl;
    }
    
    // Output to file if configured
    if (!log_file_path_.empty()) {
        // Check if rotation is needed
        rotate_log_file();
        
        std::ofstream log_file(log_file_path_, std::ios::app);
        if (log_file.is_open()) {
            log_file << log_entry << std::endl;
            log_file.close();
        } else {
            std::cerr << "Failed to write to log file: " << log_file_path_ << std::endl;
        }
    }
}

void Logger::debug(const std::string& message) {
    log(Level::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(Level::INFO, message);
}

void Logger::warn(const std::string& message) {
    log(Level::WARN, message);
}

void Logger::error(const std::string& message) {
    log(Level::ERROR, message);
}

void Logger::log_with_context(Level level, const std::string& context, const std::string& message) {
    log(level, "[" + context + "] " + message);
}

void Logger::debug(const std::string& context, const std::string& message) {
    log_with_context(Level::DEBUG, context, message);
}

void Logger::info(const std::string& context, const std::string& message) {
    log_with_context(Level::INFO, context, message);
}

void Logger::warn(const std::string& context, const std::string& message) {
    log_with_context(Level::WARN, context, message);
}

void Logger::error(const std::string& context, const std::string& message) {
    log_with_context(Level::ERROR, context, message);
}