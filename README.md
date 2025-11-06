# OneStopRadio C++ Backend

A high-performance radio streaming server built with C++ using FFmpeg for audio processing, Boost.Beast for HTTP/WebSocket handling, and libshout for streaming to Icecast/SHOUTcast servers.

## Features

- **HTTP API Server**: RESTful endpoints for stream configuration and control
- **WebRTC Server**: Real-time audio ingestion from browser clients
- **Audio Encoding**: FFmpeg-powered MP3 encoding with configurable quality
- **Stream Management**: Multi-stream support for Icecast and SHOUTcast servers
- **Configuration Management**: JSON-based configuration with validation
- **Comprehensive Logging**: File rotation and multiple log levels

## Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   React Frontend │────│  HTTP API Server │────│ Stream Manager  │
│   (Web Audio)   │    │   (Port 8080)    │    │   (libshout)    │
└─────────────────┘    └──────────────────┘    └─────────────────┘
                              │                          │
                              │                          │
                       ┌──────────────────┐    ┌─────────────────┐
                       │ WebRTC Server    │────│ Audio Encoder   │
                       │  (Port 8081)     │    │   (FFmpeg)      │
                       └──────────────────┘    └─────────────────┘
```

## Prerequisites

### macOS Installation

1. **Install Xcode Command Line Tools**:
   ```bash
   xcode-select --install
   ```

2. **Install Homebrew** (if not already installed):
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

3. **Install Required Dependencies**:
   ```bash
   brew install cmake
   brew install ffmpeg
   brew install boost
   brew install libshout
   brew install nlohmann-json
   brew install websocketpp
   brew install openssl
   ```

### Ubuntu/Debian Installation

```bash
sudo apt update
sudo apt install build-essential cmake
sudo apt install libavcodec-dev libavformat-dev libswresample-dev
sudo apt install libboost-all-dev
sudo apt install libshout3-dev
sudo apt install nlohmann-json3-dev
sudo apt install libwebsocketpp-dev
sudo apt install libssl-dev
```

### CentOS/RHEL Installation

```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake
sudo yum install ffmpeg-devel
sudo yum install boost-devel
sudo yum install libshout-devel
sudo yum install openssl-devel
# Note: nlohmann-json and websocketpp may need to be built from source
```

## Build Instructions

1. **Clone and navigate to backend directory**:
   ```bash
   cd backend-c++
   ```

2. **Create build directory**:
   ```bash
   mkdir build && cd build
   ```

3. **Configure with CMake**:
   ```bash
   cmake ..
   ```

4. **Build the project**:
   ```bash
   make -j$(nproc)
   ```

5. **Run the server**:
   ```bash
   ./radio_server
   ```

## Configuration

The server uses a JSON configuration file (`config.json`). If not found, it will use sensible defaults:

```json
{
  "server": {
    "http_port": 8080,
    "webrtc_port": 8081,
    "host": "0.0.0.0",
    "max_connections": 100
  },
  "audio": {
    "sample_rate": 44100,
    "channels": 2,
    "bitrate": 128000,
    "buffer_size": 1024
  },
  "streaming": {
    "max_streams": 10,
    "default_format": "mp3",
    "reconnect_attempts": 3,
    "reconnect_delay": 5000
  },
  "logging": {
    "level": "info",
    "file": "radio_server.log",
    "max_size": 10485760,
    "rotate": true
  }
}
```

## API Endpoints

### Stream Management
- `POST /api/streams` - Create new stream
- `POST /api/streams/:id/start` - Start streaming
- `POST /api/streams/:id/stop` - Stop streaming
- `GET /api/streams/:id/status` - Get stream status
- `GET /api/streams` - List all streams

### Server Status
- `GET /api/status` - Server health check
- `GET /api/config` - Current configuration
- `GET /api/connections` - Active WebRTC connections

## WebRTC Signaling

The WebRTC server handles browser audio ingestion:

### Message Types
- `offer` - WebRTC offer from client
- `answer` - WebRTC answer response
- `ice-candidate` - ICE candidates for NAT traversal
- `start-stream` - Begin audio streaming
- `stop-stream` - End audio streaming

## Integration with React Frontend

The C++ backend is designed to integrate with the React frontend:

1. **Audio Capture**: Browser captures audio via Web Audio API
2. **WebRTC Streaming**: Audio sent to C++ server via WebRTC
3. **Encoding**: FFmpeg encodes audio to MP3
4. **Broadcasting**: libshout streams to Icecast/SHOUTcast servers
5. **API Control**: React controls streaming via HTTP API

## Development Workflow

1. **Start Backend Server**:
   ```bash
   cd backend-c++/build
   ./radio_server
   ```

2. **Start Frontend Development Server**:
   ```bash
   cd frontend
   npm start
   ```

3. **Configure Streaming**:
   - Use React frontend to configure Icecast/SHOUTcast servers
   - Start audio capture and begin streaming
   - Monitor via server logs and API endpoints

## Troubleshooting

### Build Issues
- Ensure all dependencies are installed
- Check CMake output for missing libraries
- Verify compiler supports C++17

### Runtime Issues
- Check log file for detailed error messages
- Verify streaming server credentials
- Ensure ports 8080 and 8081 are available

### WebRTC Issues
- Enable HTTPS for production (WebRTC requires secure context)
- Check browser console for WebRTC errors
- Verify NAT/firewall configuration for ICE candidates

## Performance Tuning

- Adjust `buffer_size` for lower latency vs stability
- Configure `bitrate` based on available bandwidth
- Monitor CPU usage during encoding
- Use production builds for optimal performance

## License

This project is part of the OneStopRadio platform.