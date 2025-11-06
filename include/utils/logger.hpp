#pragma once
#include <string>

class Logger {
public:
    enum Level {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };
    
    static void initialize(const std::string& log_file = "");
    static void set_level(Level level);
    
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
    
private:
    static Level current_level_;
    static std::string log_file_;
    static void log(Level level, const std::string& message);
};