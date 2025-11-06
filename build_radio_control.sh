#!/bin/bash

# OneStopRadio C++ Backend Build Script with Radio Control System
# Enhanced with comprehensive DJ functionality and database integration

set -e  # Exit on error

echo "🎵 OneStopRadio C++ Backend - Radio Control Build Script"
echo "========================================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Build directory
BUILD_DIR="build"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${BLUE}Project Root: ${PROJECT_ROOT}${NC}"

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to install dependencies on macOS
install_macos_deps() {
    echo -e "${YELLOW}Installing macOS dependencies...${NC}"
    
    if ! command_exists brew; then
        echo -e "${RED}Homebrew not found. Please install Homebrew first.${NC}"
        echo "Visit: https://brew.sh"
        exit 1
    fi
    
    # Core build tools
    brew install cmake ninja
    
    # Audio and video processing
    echo -e "${BLUE}Installing FFmpeg and audio libraries...${NC}"
    brew install ffmpeg portaudio libsndfile fftw libsamplerate
    
    # Audio encoding libraries
    echo -e "${BLUE}Installing audio encoding libraries...${NC}"
    brew install libvorbis libogg opus lame libshout
    
    # Database
    echo -e "${BLUE}Installing SQLite...${NC}"
    brew install sqlite3
    
    # Networking and utilities
    echo -e "${BLUE}Installing networking libraries...${NC}"
    brew install boost openssl nlohmann-json websocketpp
    
    echo -e "${GREEN}macOS dependencies installed successfully!${NC}"
}

# Function to install dependencies on Ubuntu/Debian
install_ubuntu_deps() {
    echo -e "${YELLOW}Installing Ubuntu/Debian dependencies...${NC}"
    
    sudo apt update
    
    # Core build tools
    sudo apt install -y build-essential cmake ninja-build pkg-config
    
    # Audio and video processing
    echo -e "${BLUE}Installing FFmpeg and audio libraries...${NC}"
    sudo apt install -y \
        libavcodec-dev libavformat-dev libavutil-dev \
        libswresample-dev libswscale-dev \
        portaudio19-dev libsndfile1-dev \
        libfftw3-dev libsamplerate0-dev
    
    # Audio encoding libraries  
    echo -e "${BLUE}Installing audio encoding libraries...${NC}"
    sudo apt install -y \
        libvorbis-dev libogg-dev libopus-dev \
        libmp3lame-dev libshout3-dev
    
    # Database
    echo -e "${BLUE}Installing SQLite...${NC}"
    sudo apt install -y libsqlite3-dev
    
    # Networking and utilities
    echo -e "${BLUE}Installing networking libraries...${NC}"
    sudo apt install -y \
        libboost-all-dev libssl-dev \
        nlohmann-json3-dev libwebsocketpp-dev
    
    echo -e "${GREEN}Ubuntu/Debian dependencies installed successfully!${NC}"
}

# Check operating system and install dependencies
if [[ "$1" == "--install-deps" ]]; then
    echo -e "${YELLOW}Installing system dependencies...${NC}"
    
    case "$(uname -s)" in
        Darwin*)
            install_macos_deps
            ;;
        Linux*)
            if command_exists apt; then
                install_ubuntu_deps
            else
                echo -e "${RED}Unsupported Linux distribution. Please install dependencies manually.${NC}"
                echo "Required packages: cmake, ffmpeg-dev, portaudio-dev, libshout-dev, sqlite3-dev, boost-dev, openssl-dev"
                exit 1
            fi
            ;;
        *)
            echo -e "${RED}Unsupported operating system: $(uname -s)${NC}"
            exit 1
            ;;
    esac
    
    echo -e "${GREEN}All dependencies installed!${NC}"
    exit 0
fi

# Verify required tools
echo -e "${BLUE}Checking build tools...${NC}"

required_tools=("cmake" "make" "pkg-config")
for tool in "${required_tools[@]}"; do
    if ! command_exists "$tool"; then
        echo -e "${RED}Required tool not found: $tool${NC}"
        echo -e "${YELLOW}Run: $0 --install-deps${NC}"
        exit 1
    fi
done

# Check for required libraries
echo -e "${BLUE}Checking required libraries...${NC}"

required_libs=("ffmpeg" "portaudio-2.0" "sndfile" "shout" "sqlite3")
missing_libs=()

for lib in "${required_libs[@]}"; do
    if ! pkg-config --exists "$lib"; then
        missing_libs+=("$lib")
    fi
done

if [ ${#missing_libs[@]} -ne 0 ]; then
    echo -e "${RED}Missing required libraries:${NC}"
    printf ' - %s\n' "${missing_libs[@]}"
    echo -e "${YELLOW}Run: $0 --install-deps${NC}"
    exit 1
fi

echo -e "${GREEN}All required libraries found!${NC}"

# Create and enter build directory
echo -e "${BLUE}Setting up build directory...${NC}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure build
echo -e "${BLUE}Configuring build with CMake...${NC}"

BUILD_TYPE="${BUILD_TYPE:-Release}"
echo -e "${YELLOW}Build Type: $BUILD_TYPE${NC}"

cmake_options=(
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    "-DCMAKE_VERBOSE_MAKEFILE=ON"
)

# Add macOS specific options
if [[ "$(uname -s)" == "Darwin" ]]; then
    echo -e "${BLUE}Configuring for macOS...${NC}"
    
    # Find Homebrew prefix
    if command_exists brew; then
        HOMEBREW_PREFIX="$(brew --prefix)"
        cmake_options+=(
            "-DCMAKE_PREFIX_PATH=$HOMEBREW_PREFIX"
            "-DOPENSSL_ROOT_DIR=$HOMEBREW_PREFIX/opt/openssl"
            "-DBoost_ROOT=$HOMEBREW_PREFIX"
        )
    fi
fi

# Run CMake configuration
if ! cmake "${cmake_options[@]}" ..; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# Build the project
echo -e "${BLUE}Building OneStopRadio C++ Backend...${NC}"

# Determine number of cores for parallel build
if command_exists nproc; then
    CORES=$(nproc)
elif command_exists sysctl; then
    CORES=$(sysctl -n hw.ncpu)
else
    CORES=4
fi

echo -e "${YELLOW}Building with $CORES parallel jobs...${NC}"

if ! make -j"$CORES"; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

# Build summary
echo -e "${GREEN}Build completed successfully!${NC}"
echo ""
echo -e "${BLUE}Generated executables:${NC}"

executables=("onestop-radio-server" "video-api-server" "test-server")
for exe in "${executables[@]}"; do
    if [ -f "$exe" ]; then
        echo -e "  ✅ $exe"
        ls -lh "$exe" | awk '{print "     Size: "$5", Modified: "$6" "$7" "$8}'
    else
        echo -e "  ❌ $exe (not found)"
    fi
done

echo ""

# Configuration check
echo -e "${BLUE}Configuration files:${NC}"
config_files=("../config/config.json")
for config in "${config_files[@]}"; do
    if [ -f "$config" ]; then
        echo -e "  ✅ $config"
    else
        echo -e "  ⚠️  $config (missing - will use defaults)"
    fi
done

# Database setup
echo ""
echo -e "${BLUE}Database setup:${NC}"
if [ -f "radio_database.db" ]; then
    echo -e "  ✅ radio_database.db exists"
    echo -e "     Size: $(ls -lh radio_database.db | awk '{print $5}')"
else
    echo -e "  ℹ️  radio_database.db will be created on first run"
fi

# Runtime instructions
echo ""
echo -e "${YELLOW}🚀 Ready to run OneStopRadio C++ Backend!${NC}"
echo ""
echo -e "${BLUE}To start the main radio server:${NC}"
echo "  cd $BUILD_DIR"
echo "  ./onestop-radio-server"
echo ""
echo -e "${BLUE}To start with custom config:${NC}"
echo "  ./onestop-radio-server ../config/config.json"
echo ""
echo -e "${BLUE}API Endpoints will be available at:${NC}"
echo "  • Main API: http://localhost:8080/api/"
echo "  • Radio Control: http://localhost:8080/api/radio/"
echo "  • WebRTC: ws://localhost:8081"
echo ""
echo -e "${BLUE}Key Radio Control Features:${NC}"
echo "  • Track Management: Add, remove, search, analyze tracks"
echo "  • DJ Decks: Load tracks, play, pause, mix controls"
echo "  • Mixer: Crossfader, volume, EQ controls"
echo "  • Broadcasting: Start/stop live streaming"
echo "  • Database: Persistent storage with SQLite"
echo "  • Audio Levels: Real-time monitoring"
echo ""
echo -e "${GREEN}Build and setup complete! 🎵${NC}"