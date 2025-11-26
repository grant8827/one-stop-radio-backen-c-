#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <map>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sndfile.h>

// Simple JSON response helper
std::string createJsonResponse(const std::string& status, const std::string& message) {
    std::string success = (status == "success") ? "true" : "false";
    return "{\"success\":" + success + ",\"message\":\"" + message + "\"}";
}

// Simple HTTP response helper
std::string createHttpResponse(const std::string& body, const std::string& contentType = "application/json") {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: " + contentType + "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response << "Access-Control-Allow-Headers: Content-Type\r\n";
    response << "\r\n";
    response << body;
    return response.str();
}

// Audio file loading simulation
class SimpleAudioSystem {
private:
    std::map<std::string, std::string> loadedFiles;
    std::map<std::string, bool> channelPlaying;
    std::map<std::string, float> channelVolume;
    
public:
    SimpleAudioSystem() {
        channelPlaying["A"] = false;
        channelPlaying["B"] = false;
        channelVolume["A"] = 0.75f;
        channelVolume["B"] = 0.75f;
    }
    
    bool loadAudioFile(const std::string& channelId, const std::string& filePath) {
        std::cout << "Loading audio file '" << filePath << "' into channel " << channelId << std::endl;
        
        // Test file with libsndfile
        SF_INFO fileInfo;
        memset(&fileInfo, 0, sizeof(fileInfo));
        
        SNDFILE* file = sf_open(filePath.c_str(), SFM_READ, &fileInfo);
        if (!file) {
            std::cerr << "Failed to open audio file: " << filePath << std::endl;
            return false;
        }
        
        loadedFiles[channelId] = filePath;
        sf_close(file);
        
        std::cout << "✅ Successfully loaded " << filePath << " into channel " << channelId << std::endl;
        std::cout << "   Sample Rate: " << fileInfo.samplerate << " Hz" << std::endl;
        std::cout << "   Channels: " << fileInfo.channels << std::endl;
        std::cout << "   Duration: " << (fileInfo.frames / fileInfo.samplerate) << " seconds" << std::endl;
        
        return true;
    }
    
    bool setChannelPlayback(const std::string& channelId, bool play) {
        if (loadedFiles.find(channelId) == loadedFiles.end()) {
            std::cerr << "No audio file loaded in channel " << channelId << std::endl;
            return false;
        }
        
        // Reset state on stop for clean replay
        if (!play) {
            channelPlaying[channelId] = false;
            std::cout << "Channel " << channelId << " STOPPED and reset for replay" << std::endl;
        } else {
            channelPlaying[channelId] = true;
            std::cout << "Channel " << channelId << " PLAYING" << std::endl;
        }
        return true;
    }
    
    bool resetChannel(const std::string& channelId) {
        if (loadedFiles.find(channelId) != loadedFiles.end()) {
            channelPlaying[channelId] = false;
            std::cout << "Channel " << channelId << " reset to beginning" << std::endl;
            return true;
        }
        return false;
    }
    
    bool setChannelVolume(const std::string& channelId, float volume) {
        channelVolume[channelId] = volume;
        std::cout << "Channel " << channelId << " volume: " << (volume * 100) << "%" << std::endl;
        return true;
    }
    
    std::string getStatus() {
        std::ostringstream status;
        status << "Audio System Status:\n";
        status << "Channel A: " << (loadedFiles.count("A") ? loadedFiles["A"] : "No file loaded");
        status << " [" << (channelPlaying["A"] ? "PLAYING" : "STOPPED") << "]\n";
        status << "Channel B: " << (loadedFiles.count("B") ? loadedFiles["B"] : "No file loaded");
        status << " [" << (channelPlaying["B"] ? "PLAYING" : "STOPPED") << "]\n";
        return status.str();
    }
};

int main() {
    const int PORT = 8081;
    SimpleAudioSystem audioSystem;
    
    std::cout << "🎵 OneStopRadio Audio Server Starting..." << std::endl;
    std::cout << "Port: " << PORT << std::endl;
    
    // Create socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }
    
    // Allow socket reuse
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind socket to port " << PORT << std::endl;
        return 1;
    }
    
    // Listen for connections
    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Audio server listening on http://localhost:" << PORT << std::endl;
    std::cout << "Ready to handle audio file loading requests from React frontend!" << std::endl;
    
    while (true) {
        struct sockaddr_in clientAddress;
        socklen_t clientLen = sizeof(clientAddress);
        
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientLen);
        if (clientSocket < 0) {
            continue;
        }
        
        // Read request
        char buffer[4096];
        memset(buffer, 0, sizeof(buffer));
        read(clientSocket, buffer, sizeof(buffer) - 1);
        
        std::string request(buffer);
        std::cout << "\n📨 Received request:\n" << request.substr(0, 200) << "..." << std::endl;
        
        std::string response;
        
        // Parse HTTP method and path
        size_t methodEnd = request.find(' ');
        size_t pathEnd = request.find(' ', methodEnd + 1);
        
        if (methodEnd != std::string::npos && pathEnd != std::string::npos) {
            std::string method = request.substr(0, methodEnd);
            std::string path = request.substr(methodEnd + 1, pathEnd - methodEnd - 1);
            
            std::cout << "Method: " << method << ", Path: " << path << std::endl;
            
            // Handle OPTIONS for CORS
            if (method == "OPTIONS") {
                response = createHttpResponse("", "text/plain");
            }
            // Handle audio file loading
            else if (method == "POST" && path.find("/api/radio/audio/load") == 0) {
                // Extract channel and file from request body (simplified)
                size_t bodyStart = request.find("\r\n\r\n");
                if (bodyStart != std::string::npos) {
                    std::string body = request.substr(bodyStart + 4);
                    std::cout << "Request body: " << body << std::endl;
                    
                    // Simple JSON parsing (for demo)
                    std::string channelId = "A"; // Default
                    std::string filePath = "/tmp/test_audio.wav"; // Default test file
                    
                    if (body.find("\"channel\":\"B\"") != std::string::npos) {
                        channelId = "B";
                    }
                    
                    size_t filePathPos = body.find("\"filePath\":\"");
                    if (filePathPos != std::string::npos) {
                        size_t startPos = filePathPos + 12;
                        size_t endPos = body.find("\"", startPos);
                        if (endPos != std::string::npos) {
                            filePath = body.substr(startPos, endPos - startPos);
                        }
                    }
                    
                    bool success = audioSystem.loadAudioFile(channelId, filePath);
                    std::string jsonResponse = createJsonResponse(
                        success ? "success" : "error",
                        success ? "Audio file loaded successfully" : "Failed to load audio file"
                    );
                    response = createHttpResponse(jsonResponse);
                }
            }
            // Handle channel playback control - OPTIMIZED
            else if (method == "POST" && path.find("/api/radio/channel/play") == 0) {
                size_t bodyStart = request.find("\r\n\r\n");
                if (bodyStart != std::string::npos) {
                    std::string body = request.substr(bodyStart + 4);
                    
                    std::string channelId = "A";
                    bool play = true;
                    
                    if (body.find("\"channel\":\"B\"") != std::string::npos) {
                        channelId = "B";
                    }
                    if (body.find("\"play\":false") != std::string::npos) {
                        play = false;
                    }
                    
                    bool success = audioSystem.setChannelPlayback(channelId, play);
                    std::string jsonResponse = createJsonResponse(
                        success ? "success" : "error",
                        success ? "Channel playback updated" : "Failed to update playback"
                    );
                    response = createHttpResponse(jsonResponse);
                }
            }
            // Handle channel reset for replay - NEW
            else if (method == "POST" && path.find("/api/radio/channel/reset") == 0) {
                size_t bodyStart = request.find("\r\n\r\n");
                if (bodyStart != std::string::npos) {
                    std::string body = request.substr(bodyStart + 4);
                    std::string channelId = "A";
                    
                    if (body.find("\"channel\":\"B\"") != std::string::npos) {
                        channelId = "B";
                    }
                    
                    bool success = audioSystem.resetChannel(channelId);
                    std::string jsonResponse = createJsonResponse(
                        success ? "success" : "error",
                        success ? "Channel reset for replay" : "Failed to reset channel"
                    );
                    response = createHttpResponse(jsonResponse);
                }
            }
            // Handle volume control
            else if (method == "POST" && path.find("/api/radio/channel/volume") == 0) {
                size_t bodyStart = request.find("\r\n\r\n");
                if (bodyStart != std::string::npos) {
                    std::string body = request.substr(bodyStart + 4);
                    
                    std::string channelId = "A";
                    float volume = 0.75f;
                    
                    if (body.find("\"channel\":\"B\"") != std::string::npos) {
                        channelId = "B";
                    }
                    
                    size_t volumePos = body.find("\"volume\":");
                    if (volumePos != std::string::npos) {
                        size_t startPos = volumePos + 9;
                        size_t endPos = body.find_first_of(",}", startPos);
                        if (endPos != std::string::npos) {
                            std::string volumeStr = body.substr(startPos, endPos - startPos);
                            volume = std::stof(volumeStr);
                        }
                    }
                    
                    bool success = audioSystem.setChannelVolume(channelId, volume);
                    std::string jsonResponse = createJsonResponse(
                        success ? "success" : "error",
                        success ? "Channel volume updated" : "Failed to update volume"
                    );
                    response = createHttpResponse(jsonResponse);
                }
            }
            // Status endpoint
            else if (method == "GET" && path == "/api/status") {
                std::string status = audioSystem.getStatus();
                response = createHttpResponse(status, "text/plain");
            }
            // Default response
            else {
                std::string jsonResponse = createJsonResponse("error", "Endpoint not found");
                response = createHttpResponse(jsonResponse);
            }
        }
        
        // Send response
        write(clientSocket, response.c_str(), response.length());
        close(clientSocket);
        
        std::cout << "📤 Response sent" << std::endl;
    }
    
    close(serverSocket);
    return 0;
}