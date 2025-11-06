#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <map>
#include <sstream>
#include <cstdlib>
#include <csignal>
#include <atomic>

// Simple HTTP response helper
std::string create_json_response(const std::string& content, int status = 200) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
    response << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    response << "Content-Length: " << content.length() << "\r\n";
    response << "\r\n";
    response << content;
    return response.str();
}

// Simple mock API server for AudioStreamEncoder
class MockStreamServer {
private:
    std::atomic<bool> running{false};
    int port;
    std::string stream_status = "disconnected";
    int listener_count = 0;
    long bytes_sent = 0;
    std::string current_artist = "Unknown Artist";
    std::string current_title = "Unknown Track";

public:
    MockStreamServer(int p = 8080) : port(p) {}

    std::string handle_status_request() {
        return R"({
            "success": true,
            "stats": {
                "status": ")" + stream_status + R"(",
                "statusMessage": ")" + (stream_status == "streaming" ? "Live streaming active" : "Not streaming") + R"(",
                "connectedTime": 12450000,
                "bytesSent": )" + std::to_string(bytes_sent) + R"(,
                "currentBitrate": 128.0,
                "peakLevelLeft": 0.75,
                "peakLevelRight": 0.82,
                "currentListeners": )" + std::to_string(listener_count) + R"(,
                "reconnectCount": 0
            }
        })";
    }

    std::string handle_connect_request() {
        stream_status = "connected";
        return R"({
            "success": true,
            "action": "stream_connect",
            "status": "connected"
        })";
    }

    std::string handle_disconnect_request() {
        stream_status = "disconnected";
        listener_count = 0;
        return R"({
            "success": true,
            "action": "stream_disconnect", 
            "status": "disconnected"
        })";
    }

    std::string handle_start_streaming() {
        if (stream_status == "connected") {
            stream_status = "streaming";
            listener_count = 15 + (rand() % 10);
            return R"({
                "success": true,
                "action": "streaming_start",
                "status": "streaming"
            })";
        }
        return R"({
            "success": false,
            "action": "streaming_start",
            "error": "Not connected to server"
        })";
    }

    std::string handle_stop_streaming() {
        if (stream_status == "streaming") {
            stream_status = "connected";
            listener_count = 0;
            return R"({
                "success": true,
                "action": "streaming_stop",
                "status": "connected"
            })";
        }
        return R"({
            "success": false,
            "action": "streaming_stop", 
            "error": "Not currently streaming"
        })";
    }

    std::string handle_metadata_update(const std::string& artist, const std::string& title) {
        current_artist = artist.empty() ? "Unknown Artist" : artist;
        current_title = title.empty() ? "Unknown Track" : title;
        
        return R"({
            "success": true,
            "action": "metadata_update",
            "artist": ")" + current_artist + R"(",
            "title": ")" + current_title + R"("
        })";
    }

    void start() {
        running = true;
        
        std::cout << "🎵 OneStopRadio Mock Stream Server v2.0" << std::endl;
        std::cout << "=======================================" << std::endl;
        std::cout << "🚀 Starting on port " << port << std::endl;
        std::cout << std::endl;
        
        std::cout << "📡 Available Stream Encoder API endpoints:" << std::endl;
        std::cout << "  POST http://localhost:" << port << "/api/audio/stream/connect" << std::endl;
        std::cout << "  POST http://localhost:" << port << "/api/audio/stream/disconnect" << std::endl;
        std::cout << "  POST http://localhost:" << port << "/api/audio/stream/start" << std::endl;
        std::cout << "  POST http://localhost:" << port << "/api/audio/stream/stop" << std::endl;
        std::cout << "  GET  http://localhost:" << port << "/api/audio/stream/status" << std::endl;
        std::cout << "  POST http://localhost:" << port << "/api/audio/stream/metadata" << std::endl;
        std::cout << std::endl;
        
        std::cout << "✅ Mock server ready! React AudioStreamEncoder can now connect." << std::endl;
        std::cout << "🔄 Simulating realistic streaming server responses..." << std::endl;
        std::cout << "📊 Current status: " << stream_status << std::endl;
        std::cout << std::endl;
        std::cout << "Press Ctrl+C to stop server" << std::endl;
        std::cout << "===========================================" << std::endl;

        // Simulation loop
        int tick = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            tick++;
            
            // Simulate streaming activity
            if (stream_status == "streaming") {
                bytes_sent += 1024 * 16; // ~16KB per tick
                
                if (tick % 5 == 0) {
                    listener_count += (rand() % 3) - 1; // Random listener changes
                    if (listener_count < 0) listener_count = 0;
                    if (listener_count > 50) listener_count = 50;
                }
                
                std::cout << "🔴 LIVE: " << listener_count << " listeners, " 
                         << (bytes_sent / 1024) << " KB sent" << std::endl;
            }
            
            // Periodic status update  
            if (tick % 10 == 0) {
                std::cout << "💡 Server Status: " << stream_status 
                         << " | Now Playing: " << current_artist 
                         << " - " << current_title << std::endl;
            }
        }
    }

    void stop() {
        running = false;
        std::cout << std::endl << "🛑 OneStopRadio Mock Stream Server Stopped" << std::endl;
    }
};

// Global server instance for signal handling
MockStreamServer* g_server = nullptr;

void signal_handler(int signal) {
    std::cout << std::endl << "🛑 Received signal " << signal << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    std::srand(std::time(nullptr));
    
    // Setup signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    MockStreamServer server(port);
    g_server = &server;
    
    std::cout << "🎵 OneStopRadio Stream Encoder Mock API Server" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "This server mocks the C++ backend API calls" << std::endl;
    std::cout << "to test the React AudioStreamEncoder component." << std::endl;
    std::cout << std::endl;

    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "❌ Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}