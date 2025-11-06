#!/bin/bash

echo "==================================="
echo "OneStopRadio Server Build System"
echo "==================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    print_error "CMakeLists.txt not found. Please run this script from the backend-c++ directory."
    exit 1
fi

print_status "Starting build process for OneStopRadio Server with Enhanced Audio System..."
echo ""

# Check dependencies
print_status "Checking dependencies..."

# Check for CMake
if ! command -v cmake &> /dev/null; then
    print_error "CMake not found. Please install CMake first."
    echo "Run: ./install_dependencies.sh"
    exit 1
fi

# Check for pkg-config
if ! command -v pkg-config &> /dev/null; then
    print_error "pkg-config not found. Please install pkg-config first."
    echo "Run: ./install_dependencies.sh"
    exit 1
fi

# Check for required audio libraries
print_status "Checking audio libraries..."

if ! pkg-config --exists portaudio-2.0; then
    print_warning "PortAudio not found. Audio I/O functionality may not work."
    echo "Install with: brew install portaudio"
fi

if ! pkg-config --exists sndfile; then
    print_warning "libsndfile not found. Audio file I/O may not work."
    echo "Install with: brew install libsndfile"
fi

if ! pkg-config --exists fftw3f; then
    print_warning "FFTW3 not found. Spectrum analysis may not work."
    echo "Install with: brew install fftw"
fi

if ! pkg-config --exists samplerate; then
    print_warning "libsamplerate not found. Sample rate conversion may not work."
    echo "Install with: brew install libsamplerate"
fi

# Create build directory
print_status "Creating build directory..."
mkdir -p build
cd build

# Configure with CMake
print_status "Configuring project with CMake..."
if ! cmake ..; then
    print_error "CMake configuration failed!"
    exit 1
fi

print_success "CMake configuration completed!"

# Build the project
print_status "Building OneStopRadio Server..."
if ! make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4); then
    print_error "Build failed!"
    exit 1
fi

print_success "Build completed successfully!"
echo ""

# Check if executable was created
if [ -f "onestop-radio-server" ]; then
    print_success "Executable created: onestop-radio-server"
    
    # Display file info
    ls -la onestop-radio-server
    echo ""
    
    # Display audio system capabilities
    echo "🎵 =================================="
    echo "🎵   AUDIO SYSTEM CAPABILITIES"
    echo "🎵 =================================="
    echo ""
    echo "✅ Real-time Audio Processing (PortAudio)"
    echo "✅ Professional Audio Mixing"
    echo "✅ Multi-channel Support"
    echo "✅ Crossfader Control"
    echo "✅ Audio Effects Chain"
    echo "   - 3-band EQ per channel"
    echo "   - Dynamic Range Compressor"
    echo "   - Reverb Effect"
    echo "   - Delay Effect"
    echo "   - Audio Limiter"
    echo "✅ Microphone Input"
    echo "   - Noise Gate"
    echo "   - Gain Control"
    echo "   - Real-time Processing"
    echo "✅ Level Metering"
    echo "   - Peak/RMS/dB measurements"
    echo "   - Per-channel and master levels"
    echo "   - Clipping detection"
    echo "✅ Spectrum Analysis (FFT)"
    echo "✅ Audio Streaming"
    echo "✅ Audio Recording"
    echo "✅ BPM Detection"
    echo "✅ Device Management"
    echo "   - Input/Output device selection"
    echo "   - Device enumeration"
    echo "   - Configuration persistence"
    echo ""
    echo "🚀 ENHANCED API ENDPOINTS:"
    echo ""
    echo "📡 Audio Devices:"
    echo "   GET  /api/audio/devices/input"
    echo "   GET  /api/audio/devices/output"
    echo ""
    echo "🎤 Microphone Controls:"
    echo "   POST /api/audio/microphone/enable"
    echo "   POST /api/audio/microphone/disable"
    echo "   POST /api/audio/microphone/gain"
    echo "   GET  /api/audio/microphone/config"
    echo ""
    echo "🎵 Audio Channels:"
    echo "   POST /api/audio/channels/create"
    echo "   GET  /api/audio/channels/list"
    echo "   POST /api/audio/channel/load"
    echo "   POST /api/audio/channel/play"
    echo "   POST /api/audio/channel/pause"
    echo "   POST /api/audio/channel/stop"
    echo "   POST /api/audio/channel/volume"
    echo ""
    echo "🎚️ Master Controls:"
    echo "   POST /api/audio/master/volume"
    echo "   POST /api/audio/crossfader"
    echo ""
    echo "📊 Level Monitoring:"
    echo "   GET  /api/audio/levels/master"
    echo "   GET  /api/audio/levels/microphone"
    echo "   POST /api/audio/levels/channel"
    echo "   GET  /api/audio/levels (legacy)"
    echo ""
    echo "🎛️ Audio Effects:"
    echo "   POST /api/audio/effects/reverb"
    echo "   POST /api/audio/effects/delay"
    echo ""
    echo "📊 Analysis:"
    echo "   POST /api/audio/bpm/detect"
    echo "   POST /api/audio/bpm/sync"
    echo "   POST /api/audio/spectrum"
    echo ""
    echo "📡 Streaming & Recording:"
    echo "   POST /api/audio/stream/start"
    echo "   POST /api/audio/stream/stop"
    echo "   POST /api/audio/record/start"
    echo "   POST /api/audio/record/stop"
    echo ""
    echo "🚀 To run the server:"
    echo "   ./onestop-radio-server"
    echo ""
    print_success "Build process completed! Your C++ backend now handles ALL audio processing duties."
    
else
    print_error "Executable not found after build!"
    exit 1
fi