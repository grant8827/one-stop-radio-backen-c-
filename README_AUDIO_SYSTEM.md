# OneStopRadio Enhanced Audio System

## Overview

The OneStopRadio platform now features a **comprehensive C++ audio backend** that handles all professional audio processing duties. This system provides professional-grade audio mixing, real-time effects, microphone processing, and streaming capabilities.

## Architecture

### Hybrid Audio System
- **Primary**: C++ Backend with PortAudio for professional real-time audio processing
- **Fallback**: Web Audio API for basic functionality when backend is unavailable
- **Automatic Detection**: System automatically detects and switches between modes

### Audio Backend Stack
```
┌─────────────────────────────────────────┐
│         React Frontend                  │
│  ┌─────────────────────────────────┐    │
│  │     AudioService.ts             │    │
│  │  (Hybrid Web Audio + Backend)   │    │
│  └─────────────────────────────────┘    │
└─────────────────────────────────────────┘
                   │ HTTP API
┌─────────────────────────────────────────┐
│         C++ Audio Backend               │
│  ┌─────────────────────────────────┐    │
│  │      AudioSystem Class          │    │
│  │   - Real-time Processing        │    │
│  │   - Multi-channel Mixing        │    │
│  │   - Audio Effects Chain         │    │
│  │   - Level Metering              │    │
│  │   - Device Management           │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │       PortAudio I/O             │    │
│  │   - Low-latency Audio I/O       │    │
│  │   - Device Enumeration          │    │
│  │   - Real-time Callback         │    │
│  └─────────────────────────────────┘    │
└─────────────────────────────────────────┘
```

## Key Features

### 🎵 Professional Audio Processing
- **Real-time Audio I/O**: PortAudio integration for low-latency processing
- **Multi-channel Support**: Individual channel processing and mixing
- **Crossfader Control**: Professional DJ-style crossfading between channels
- **Master Volume Control**: System-wide volume management

### 🎤 Advanced Microphone Processing
- **Real-time Input**: Live microphone processing with configurable settings
- **Noise Gate**: Automatic background noise suppression
- **Gain Control**: Precise microphone level adjustment
- **Device Selection**: Choose from available audio input devices

### 🎛️ Audio Effects Chain
- **3-Band EQ per Channel**: Bass, Mid, Treble frequency control
- **Dynamic Range Compressor**: Automatic level control
- **Reverb Effect**: Configurable room simulation
- **Delay Effect**: Echo and delay processing
- **Audio Limiter**: Prevent clipping and distortion

### 📊 Real-time Audio Analysis
- **Level Metering**: Peak, RMS, and dB measurements
- **Spectrum Analysis**: FFT-based frequency analysis
- **Clipping Detection**: Automatic distortion monitoring
- **BPM Detection**: Tempo analysis for music synchronization

### 🔧 Device Management
- **Input Device Enumeration**: List all available microphones and line inputs
- **Output Device Selection**: Choose audio output destinations
- **Device Configuration**: Persistent device settings
- **Hot-plug Support**: Dynamic device detection

## Installation

### Prerequisites
```bash
# macOS (Homebrew)
brew install portaudio libsndfile fftw libsamplerate pkg-config cmake

# Ubuntu/Debian
sudo apt-get install libportaudio2-dev libsndfile1-dev libfftw3-dev libsamplerate0-dev pkg-config cmake

# CentOS/RHEL
sudo yum install portaudio-devel libsndfile-devel fftw-devel libsamplerate-devel pkgconfig cmake
```

### Build Instructions

#### Automated Installation
```bash
cd backend-c++
chmod +x install_dependencies.sh
./install_dependencies.sh
```

#### Manual Build
```bash
cd backend-c++
mkdir -p build
cd build
cmake ..
make -j4
```

#### Using Build Script
```bash
cd backend-c++
chmod +x build_audio_server.sh
./build_audio_server.sh
```

## Usage

### Starting the Server
```bash
cd backend-c++/build
./onestop-radio-server
```

The server will start on `http://localhost:8080` with comprehensive audio processing capabilities.

### Frontend Integration

The React frontend automatically detects the C++ backend:

```typescript
// AudioService automatically switches between backend and Web Audio
import { audioService } from './services/AudioService';

// Initialize audio system
await audioService.initialize();

// Check current mode
const status = audioService.getSystemStatus();
console.log('Audio mode:', status.audioMode); // 'backend' or 'webaudio'
```

## API Reference

### Audio Devices
- `GET /api/audio/devices/input` - List input devices
- `GET /api/audio/devices/output` - List output devices

### Microphone Controls
- `POST /api/audio/microphone/enable` - Enable microphone with settings
- `POST /api/audio/microphone/disable` - Disable microphone
- `POST /api/audio/microphone/gain` - Set microphone gain
- `GET /api/audio/microphone/config` - Get microphone configuration

### Audio Channels
- `POST /api/audio/channels/create` - Create new audio channel
- `GET /api/audio/channels/list` - List active channels
- `POST /api/audio/channel/load` - Load audio file into channel
- `POST /api/audio/channel/play` - Start channel playback
- `POST /api/audio/channel/pause` - Pause channel playback
- `POST /api/audio/channel/stop` - Stop channel playback
- `POST /api/audio/channel/volume` - Set channel volume

### Master Controls
- `POST /api/audio/master/volume` - Set master volume
- `POST /api/audio/crossfader` - Set crossfader position

### Level Monitoring
- `GET /api/audio/levels/master` - Get master audio levels
- `GET /api/audio/levels/microphone` - Get microphone levels
- `POST /api/audio/levels/channel` - Get channel-specific levels

### Audio Effects
- `POST /api/audio/effects/reverb` - Configure reverb effect
- `POST /api/audio/effects/delay` - Configure delay effect

### Analysis Features
- `POST /api/audio/bpm/detect` - Detect BPM of audio channel
- `POST /api/audio/bpm/sync` - Enable BPM sync between channels
- `POST /api/audio/spectrum` - Get spectrum analysis data

### Streaming & Recording
- `POST /api/audio/stream/start` - Start audio streaming
- `POST /api/audio/stream/stop` - Stop audio streaming
- `POST /api/audio/record/start` - Start audio recording
- `POST /api/audio/record/stop` - Stop audio recording

## Technical Details

### Audio Format Configuration
```cpp
AudioFormat format;
format.sample_rate = 48000;    // Professional audio standard
format.channels = 2;           // Stereo
format.bit_depth = 32;         // 32-bit float precision
format.buffer_size = 256;      // Low-latency buffer
```

### Real-time Processing
- **Callback-based Processing**: PortAudio callback for real-time audio
- **Lock-free Design**: Thread-safe audio processing
- **Configurable Latency**: Adjustable buffer sizes
- **Professional Grade**: 32-bit float processing throughout

### Effect Chain Architecture
```cpp
Input → Noise Gate → EQ (3-band) → Compressor → Reverb → Delay → Limiter → Output
```

### Level Metering
- **Peak Detection**: Instantaneous peak levels
- **RMS Calculation**: Average signal power
- **dB Conversion**: Professional dB scale measurements
- **Clipping Detection**: Digital distortion monitoring

## Configuration

### Default Settings
- **Sample Rate**: 48 kHz (professional standard)
- **Bit Depth**: 32-bit float
- **Channels**: 2 (stereo)
- **Buffer Size**: 256 samples (5.3ms latency @ 48kHz)
- **Update Rate**: 50ms for level monitoring

### Microphone Configuration
```json
{
  "enabled": true,
  "gain": 1.0,
  "gate_threshold": -40.0,
  "noise_suppression": true,
  "echo_cancellation": true,
  "auto_gain_control": false,
  "device_id": 0
}
```

## Performance Optimization

### Low-latency Configuration
- Use ASIO drivers on Windows
- Set buffer size to 64-256 samples
- Use exclusive mode when available
- Disable system audio enhancements

### CPU Optimization
- Multi-threaded processing
- SIMD optimizations where available
- Efficient memory management
- Lockless audio processing

## Troubleshooting

### Common Issues

1. **Backend Not Detected**
   ```bash
   # Check if server is running
   curl http://localhost:8080/api/status
   
   # Verify audio dependencies
   pkg-config --exists portaudio-2.0 sndfile fftw3f samplerate
   ```

2. **Audio Device Issues**
   ```bash
   # List available audio devices
   curl -X GET http://localhost:8080/api/audio/devices/input
   curl -X GET http://localhost:8080/api/audio/devices/output
   ```

3. **High Latency**
   - Reduce buffer size in configuration
   - Use dedicated audio drivers (ASIO/Core Audio)
   - Close other audio applications

4. **No Microphone Signal**
   ```bash
   # Check microphone configuration
   curl -X GET http://localhost:8080/api/audio/microphone/config
   
   # Enable microphone with proper settings
   curl -X POST http://localhost:8080/api/audio/microphone/enable \
     -H "Content-Type: application/json" \
     -d '{"gain": 1.0, "device_id": 0}'
   ```

### Debug Mode

Enable debug logging in the C++ backend:
```cpp
// In main.cpp, set log level
Logger::setLevel(Logger::DEBUG);
```

## Contributing

### Development Workflow
1. Make audio system changes in `src/audio_system.cpp`
2. Test with build script: `./build_audio_server.sh`
3. Verify frontend integration
4. Run comprehensive audio tests

### Code Style
- Follow C++17 standards
- Use RAII for resource management
- Implement proper error handling
- Add comprehensive logging

## License

This project is part of the OneStopRadio platform. See main project license for details.