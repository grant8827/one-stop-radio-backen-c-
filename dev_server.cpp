#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <map>

// Simple HTTP server mock for development
class DevServer {
private:
    bool running = false;
    int port;

public:
    DevServer(int p = 8080) : port(p) {}

    void start() {
        running = true;
        std::cout << "🎵 OneStopRadio Dev Server Starting on port " << port << "...\n";
        std::cout << "📡 API endpoints:\n";
        std::cout << "  GET  /api/status\n";
        std::cout << "  GET  /api/video/status\n"; 
        std::cout << "  POST /api/video/start\n";
        std::cout << "  POST /api/video/stop\n";
        std::cout << "  GET  /api/audio/levels\n";
        std::cout << "\n✅ Server ready! Frontend can connect to http://localhost:" << port << "\n";
        std::cout << "Press Ctrl+C to stop...\n\n";

        // Simple server loop
        int requestCount = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            requestCount++;
            
            // Simulate some activity
            if (requestCount % 3 == 0) {
                std::cout << "📊 Simulating API request #" << requestCount 
                         << " - Audio levels: L:75% R:82%" << std::endl;
            }
            
            if (requestCount % 10 == 0) {
                std::cout << "🔄 Server health check - All systems operational" << std::endl;
            }
        }
    }

    void stop() {
        running = false;
        std::cout << "\n🛑 OneStopRadio Dev Server Stopped\n";
    }
};

int main() {
    std::cout << "=== OneStopRadio Development Server ===" << std::endl;
    std::cout << "This mock server simulates the C++ backend API" << std::endl;
    std::cout << "for React frontend development and testing.\n" << std::endl;

    DevServer server(8080);
    
    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}