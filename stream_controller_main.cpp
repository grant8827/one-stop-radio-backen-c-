#include "stream_controller_api.hpp"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>

using namespace onestopradio;

// Global pointer for signal handling
StreamControllerAPI* g_api_server = nullptr;

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ". Shutting down gracefully..." << std::endl;
    
    if (g_api_server) {
        g_api_server->Stop();
    }
    
    exit(0);
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port <port>     HTTP API port (default: 8083)" << std::endl;
    std::cout << "  -c, --config <file>   Configuration file path" << std::endl;
    std::cout << "  -h, --help            Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " --port 8083" << std::endl;
    std::cout << "  " << program_name << " --config /etc/onestopradio/stream-controller.json" << std::endl;
}

int main(int argc, char* argv[]) {
    int port = 8083;
    std::string config_file = "";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: --port requires a port number" << std::endl;
                return 1;
            }
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                std::cerr << "Error: --config requires a file path" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown argument " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Validate port range
    if (port < 1024 || port > 65535) {
        std::cerr << "Error: Port must be between 1024 and 65535" << std::endl;
        return 1;
    }
    
    std::cout << "=====================================" << std::endl;
    std::cout << "OneStopRadio Stream Controller API" << std::endl;
    std::cout << "Version 1.0.0" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "Port: " << port << std::endl;
    if (!config_file.empty()) {
        std::cout << "Config: " << config_file << std::endl;
    }
    std::cout << "=====================================" << std::endl;
    
    try {
        // Create and initialize API server
        StreamControllerAPI api_server(port);
        g_api_server = &api_server;
        
        // Set up signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        // Initialize the server
        if (!api_server.Initialize(config_file)) {
            std::cerr << "Failed to initialize Stream Controller API" << std::endl;
            return 1;
        }
        
        std::cout << "Stream Controller API starting..." << std::endl;
        std::cout << "Available endpoints:" << std::endl;
        std::cout << "  POST   /api/v1/streams                    - Create stream" << std::endl;
        std::cout << "  GET    /api/v1/streams                    - List all streams" << std::endl;
        std::cout << "  GET    /api/v1/streams/{id}               - Get stream status" << std::endl;
        std::cout << "  PUT    /api/v1/streams/{id}               - Update stream" << std::endl;
        std::cout << "  DELETE /api/v1/streams/{id}               - Delete stream" << std::endl;
        std::cout << "  POST   /api/v1/streams/{id}/activate      - Activate stream" << std::endl;
        std::cout << "  POST   /api/v1/streams/{id}/deactivate    - Deactivate stream" << std::endl;
        std::cout << "  GET    /api/v1/streams/{id}/status        - Get detailed status" << std::endl;
        std::cout << "  POST   /api/v1/streams/{id}/metadata      - Update metadata" << std::endl;
        std::cout << "  POST   /api/v1/reload                     - Reload configuration" << std::endl;
        std::cout << "  GET    /health                             - Health check" << std::endl;
        std::cout << "=====================================" << std::endl;
        std::cout << "Server ready. Press Ctrl+C to stop." << std::endl;
        
        // Run the server (blocking call)
        api_server.Run();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Stream Controller API shutdown complete." << std::endl;
    return 0;
}