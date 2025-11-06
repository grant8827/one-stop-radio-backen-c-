#pragma once
#include <string>
#include <memory>

class WebRTCServer {
public:
    explicit WebRTCServer(int port);
    ~WebRTCServer();
    
    bool initialize();
    void run();
    void stop();
    
    // WebRTC stream handling
    void handle_offer(const std::string& offer);
    std::string create_answer();
    void add_ice_candidate(const std::string& candidate);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};