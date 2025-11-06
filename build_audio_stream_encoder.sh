#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# OneStopRadio Audio Stream Encoder Build Script
echo -e "${BLUE}🎵 OneStopRadio Audio Stream Encoder Build${NC}"
echo "============================================="

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# Create build directory
BUILD_DIR="$PROJECT_ROOT/build"
mkdir -p "$BUILD_DIR"

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo -e "${RED}❌ Error: CMakeLists.txt not found. Please run this script from the C++ backend directory.${NC}"
    exit 1
fi

# Function to check if a package is installed
check_dependency() {
    local package_name="$1"
    local pkg_config_name="$2"
    
    if pkg-config --exists "$pkg_config_name" 2>/dev/null; then
        local version=$(pkg-config --modversion "$pkg_config_name")
        echo -e "${GREEN}✅ $package_name: $version${NC}"
        return 0
    else
        echo -e "${RED}❌ $package_name: NOT FOUND${NC}"
        return 1
    fi
}

# Function to install dependencies on different systems
install_dependencies() {
    echo -e "${YELLOW}📦 Installing dependencies...${NC}"
    
    # Detect OS
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        # Linux (Ubuntu/Debian)
        if command -v apt-get &> /dev/null; then
            sudo apt-get update
            sudo apt-get install -y \
                build-essential cmake pkg-config \
                libavcodec-dev libavformat-dev libavutil-dev \
                libswresample-dev libswscale-dev \
                libshout3-dev \
                libportaudio2 portaudio19-dev \
                libsndfile1-dev \
                libfftw3-dev \
                libsamplerate0-dev \
                libvorbis-dev libogg-dev \
                libopus-dev \
                liblame-dev \
                libboost-system-dev libboost-filesystem-dev libboost-thread-dev \
                libssl-dev
        # CentOS/RHEL/Fedora
        elif command -v yum &> /dev/null || command -v dnf &> /dev/null; then
            local installer="yum"
            if command -v dnf &> /dev/null; then
                installer="dnf"
            fi
            
            sudo $installer install -y \
                gcc-c++ cmake pkgconfig \
                ffmpeg-devel \
                libshout-devel \
                portaudio-devel \
                libsndfile-devel \
                fftw3-devel \
                libsamplerate-devel \
                libvorbis-devel libogg-devel \
                opus-devel \
                lame-devel \
                boost-devel \
                openssl-devel
        fi
        
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        if command -v brew &> /dev/null; then
            brew install cmake pkg-config \
                ffmpeg \
                libshout \
                portaudio \
                libsndfile \
                fftw \
                libsamplerate \
                libvorbis libogg \
                opus \
                lame \
                boost \
                openssl
        else
            echo -e "${RED}❌ Homebrew not found. Please install Homebrew first: https://brew.sh${NC}"
            exit 1
        fi
    else
        echo -e "${YELLOW}⚠️ Unsupported OS. Please install dependencies manually.${NC}"
    fi
}

# Check system dependencies
echo -e "${BLUE}🔍 Checking system dependencies...${NC}"

missing_deps=0

# Core build tools
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}❌ cmake: NOT FOUND${NC}"
    ((missing_deps++))
else
    echo -e "${GREEN}✅ cmake: $(cmake --version | head -n1 | cut -d' ' -f3)${NC}"
fi

if ! command -v pkg-config &> /dev/null; then
    echo -e "${RED}❌ pkg-config: NOT FOUND${NC}"
    ((missing_deps++))
else
    echo -e "${GREEN}✅ pkg-config: $(pkg-config --version)${NC}"
fi

# Audio/Video libraries
check_dependency "FFmpeg (libavcodec)" "libavcodec" || ((missing_deps++))
check_dependency "FFmpeg (libavformat)" "libavformat" || ((missing_deps++))
check_dependency "FFmpeg (libavutil)" "libavutil" || ((missing_deps++))
check_dependency "FFmpeg (libswresample)" "libswresample" || ((missing_deps++))

check_dependency "libshout" "shout" || ((missing_deps++))
check_dependency "PortAudio" "portaudio-2.0" || ((missing_deps++))
check_dependency "libsndfile" "sndfile" || ((missing_deps++))
check_dependency "FFTW3F" "fftw3f" || ((missing_deps++))
check_dependency "libsamplerate" "samplerate" || ((missing_deps++))

# Codec libraries
check_dependency "Vorbis" "vorbis" || ((missing_deps++))
check_dependency "VorbisEnc" "vorbisenc" || ((missing_deps++))
check_dependency "OGG" "ogg" || ((missing_deps++))
check_dependency "Opus" "opus" || ((missing_deps++))

# LAME might not have pkg-config on some systems
if pkg-config --exists "lame" 2>/dev/null; then
    check_dependency "LAME" "lame"
elif [ -f "/usr/include/lame/lame.h" ] || [ -f "/usr/local/include/lame/lame.h" ]; then
    echo -e "${GREEN}✅ LAME: found (header-based detection)${NC}"
else
    echo -e "${RED}❌ LAME: NOT FOUND${NC}"
    ((missing_deps++))
fi

# Check for Boost (might not have pkg-config)
if [ -d "/usr/include/boost" ] || [ -d "/usr/local/include/boost" ]; then
    echo -e "${GREEN}✅ Boost: found${NC}"
else
    echo -e "${RED}❌ Boost: NOT FOUND${NC}"
    ((missing_deps++))
fi

# Install dependencies if missing
if [ $missing_deps -gt 0 ]; then
    echo -e "${YELLOW}⚠️ Found $missing_deps missing dependencies.${NC}"
    read -p "Would you like to install missing dependencies? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        install_dependencies
    else
        echo -e "${RED}❌ Please install missing dependencies and try again.${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}✅ All dependencies satisfied!${NC}"
echo

# Configure build
echo -e "${BLUE}🔧 Configuring build...${NC}"
cd "$BUILD_DIR"

if ! cmake .. -DCMAKE_BUILD_TYPE=Release; then
    echo -e "${RED}❌ CMake configuration failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Configuration complete!${NC}"

# Build the project
echo -e "${BLUE}🔨 Building project...${NC}"

# Get number of CPU cores for parallel build
if command -v nproc &> /dev/null; then
    CORES=$(nproc)
elif [[ "$OSTYPE" == "darwin"* ]]; then
    CORES=$(sysctl -n hw.ncpu)
else
    CORES=4
fi

echo -e "${BLUE}Using $CORES parallel jobs...${NC}"

if ! make -j$CORES; then
    echo -e "${RED}❌ Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Build successful!${NC}"

# Check if executable was created
EXECUTABLE="$BUILD_DIR/onestop-radio-server"
if [ -f "$EXECUTABLE" ]; then
    echo -e "${GREEN}🎉 Executable created: $EXECUTABLE${NC}"
    echo -e "${BLUE}📊 File info:${NC}"
    ls -lh "$EXECUTABLE"
    
    # Show linking info
    echo -e "${BLUE}🔗 Linked libraries:${NC}"
    if command -v ldd &> /dev/null; then
        ldd "$EXECUTABLE" | head -n 10
    elif command -v otool &> /dev/null; then
        otool -L "$EXECUTABLE" | head -n 10
    fi
    
    echo
    echo -e "${GREEN}🚀 To run the server:${NC}"
    echo -e "${BLUE}   cd $PROJECT_ROOT${NC}"
    echo -e "${BLUE}   ./build/onestop-radio-server${NC}"
    echo
    echo -e "${YELLOW}📋 Available API endpoints:${NC}"
    echo -e "   ${BLUE}Stream Encoder:${NC} http://localhost:8080/api/audio/stream/*"
    echo -e "   ${BLUE}Audio System:${NC}   http://localhost:8080/api/audio/*"
    echo -e "   ${BLUE}Video Stream:${NC}   http://localhost:8080/api/video/*"
    echo -e "   ${BLUE}Server Status:${NC}  http://localhost:8080/api/status"
    
else
    echo -e "${RED}❌ Executable not found!${NC}"
    exit 1
fi

echo -e "${GREEN}🎵 OneStopRadio Audio Stream Encoder ready!${NC}"