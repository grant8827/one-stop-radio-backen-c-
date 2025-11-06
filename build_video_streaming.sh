#!/bin/bash

# OneStopRadio Video Streaming Backend Build Script
# This script builds the C++ video streaming system with all dependencies

set -e  # Exit on any error

echo "🎬 OneStopRadio Video Streaming Backend Build"
echo "============================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_ROOT/build"

echo -e "${BLUE}📁 Project root: $PROJECT_ROOT${NC}"

# Check if running on macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo -e "${YELLOW}🍎 Detected macOS - using Homebrew for dependencies${NC}"
    PACKAGE_MANAGER="brew"
else
    echo -e "${YELLOW}🐧 Detected Linux - using apt/pkg-config${NC}"
    PACKAGE_MANAGER="apt"
fi

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to install dependencies on macOS
install_macos_deps() {
    echo -e "${YELLOW}📦 Installing macOS dependencies via Homebrew...${NC}"
    
    # Core development tools
    if ! command_exists cmake; then
        echo "Installing CMake..."
        brew install cmake
    fi
    
    if ! command_exists pkg-config; then
        echo "Installing pkg-config..."
        brew install pkg-config
    fi
    
    # FFmpeg and media libraries
    echo "Installing FFmpeg and media libraries..."
    brew install ffmpeg
    brew install portaudio
    brew install libsndfile
    brew install fftw
    brew install libsamplerate
    brew install libvorbis
    brew install opus
    brew install lame
    
    # Streaming libraries
    brew install libshout
    
    # Network and HTTP libraries
    brew install boost
    brew install openssl
    
    # JSON library
    brew install jsoncpp
    
    echo -e "${GREEN}✅ macOS dependencies installed${NC}"
}

# Function to install dependencies on Linux
install_linux_deps() {
    echo -e "${YELLOW}📦 Installing Linux dependencies...${NC}"
    
    # Update package list
    sudo apt update
    
    # Core development tools
    sudo apt install -y build-essential cmake pkg-config git
    
    # FFmpeg development libraries
    sudo apt install -y \
        libavcodec-dev \
        libavformat-dev \
        libavutil-dev \
        libswresample-dev \
        libswscale-dev
    
    # Audio libraries
    sudo apt install -y \
        portaudio19-dev \
        libsndfile1-dev \
        libfftw3-dev \
        libsamplerate0-dev \
        libvorbis-dev \
        libopus-dev \
        liblame-dev
    
    # Streaming libraries
    sudo apt install -y libshout3-dev
    
    # Network and HTTP libraries
    sudo apt install -y \
        libboost-system-dev \
        libboost-filesystem-dev \
        libboost-thread-dev \
        libssl-dev
    
    # JSON library
    sudo apt install -y libjsoncpp-dev
    
    echo -e "${GREEN}✅ Linux dependencies installed${NC}"
}

# Function to check dependencies
check_dependencies() {
    echo -e "${BLUE}🔍 Checking dependencies...${NC}"
    
    local missing_deps=()
    
    # Check for cmake
    if ! command_exists cmake; then
        missing_deps+=("cmake")
    fi
    
    # Check for pkg-config
    if ! command_exists pkg-config; then
        missing_deps+=("pkg-config")
    fi
    
    # Check for development libraries using pkg-config
    local required_libs=(
        "libavcodec"
        "libavformat" 
        "libavutil"
        "portaudio-2.0"
        "sndfile"
        "fftw3f"
        "samplerate"
        "vorbis"
        "opus"
        "shout"
    )
    
    for lib in "${required_libs[@]}"; do
        if ! pkg-config --exists "$lib" 2>/dev/null; then
            missing_deps+=("$lib")
        fi
    done
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        echo -e "${RED}❌ Missing dependencies: ${missing_deps[*]}${NC}"
        return 1
    else
        echo -e "${GREEN}✅ All dependencies found${NC}"
        return 0
    fi
}

# Function to create build directory
setup_build_dir() {
    echo -e "${BLUE}📁 Setting up build directory...${NC}"
    
    if [ -d "$BUILD_DIR" ]; then
        echo "Cleaning existing build directory..."
        rm -rf "$BUILD_DIR"
    fi
    
    mkdir -p "$BUILD_DIR"
    echo -e "${GREEN}✅ Build directory ready: $BUILD_DIR${NC}"
}

# Function to configure with CMake
configure_project() {
    echo -e "${BLUE}⚙️  Configuring project with CMake...${NC}"
    
    cd "$BUILD_DIR"
    
    # Set build type
    local build_type="${BUILD_TYPE:-Release}"
    echo "Build type: $build_type"
    
    # Configure with CMake
    cmake -DCMAKE_BUILD_TYPE="$build_type" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          "$PROJECT_ROOT"
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✅ CMake configuration successful${NC}"
    else
        echo -e "${RED}❌ CMake configuration failed${NC}"
        exit 1
    fi
}

# Function to build the project
build_project() {
    echo -e "${BLUE}🔨 Building video streaming system...${NC}"
    
    cd "$BUILD_DIR"
    
    # Determine number of cores for parallel build
    if [[ "$OSTYPE" == "darwin"* ]]; then
        CORES=$(sysctl -n hw.ncpu)
    else
        CORES=$(nproc)
    fi
    
    echo "Building with $CORES parallel jobs..."
    
    # Build all targets
    make -j"$CORES"
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✅ Build successful${NC}"
        
        # List built executables
        echo -e "${BLUE}📋 Built executables:${NC}"
        ls -la onestop-radio-server video-api-server test-server 2>/dev/null || true
        
    else
        echo -e "${RED}❌ Build failed${NC}"
        exit 1
    fi
}

# Function to run tests
run_tests() {
    echo -e "${BLUE}🧪 Running tests...${NC}"
    
    cd "$BUILD_DIR"
    
    if [ -x "./test-server" ]; then
        echo "Running test server..."
        ./test-server --test
    else
        echo -e "${YELLOW}⚠️  Test server not built${NC}"
    fi
}

# Function to install executables
install_executables() {
    echo -e "${BLUE}📦 Installing executables...${NC}"
    
    cd "$BUILD_DIR"
    
    # Create local bin directory if it doesn't exist
    local bin_dir="$PROJECT_ROOT/bin"
    mkdir -p "$bin_dir"
    
    # Copy executables
    for exe in onestop-radio-server video-api-server test-server; do
        if [ -x "./$exe" ]; then
            cp "./$exe" "$bin_dir/"
            echo "Installed: $bin_dir/$exe"
        fi
    done
    
    echo -e "${GREEN}✅ Installation complete${NC}"
    echo -e "${BLUE}💡 Executables available in: $bin_dir${NC}"
}

# Main build process
main() {
    echo -e "${BLUE}🚀 Starting build process...${NC}"
    
    # Parse command line arguments
    local install_deps=false
    local run_tests_flag=false
    local install_flag=false
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            --install-deps)
                install_deps=true
                shift
                ;;
            --test)
                run_tests_flag=true
                shift
                ;;
            --install)
                install_flag=true
                shift
                ;;
            --help)
                echo "Usage: $0 [OPTIONS]"
                echo "Options:"
                echo "  --install-deps    Install system dependencies"
                echo "  --test           Run tests after building"
                echo "  --install        Install executables to bin/"
                echo "  --help           Show this help message"
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                exit 1
                ;;
        esac
    done
    
    # Install dependencies if requested
    if [ "$install_deps" = true ]; then
        if [[ "$OSTYPE" == "darwin"* ]]; then
            install_macos_deps
        else
            install_linux_deps
        fi
    fi
    
    # Check dependencies
    if ! check_dependencies; then
        echo -e "${YELLOW}💡 Run with --install-deps to install missing dependencies${NC}"
        exit 1
    fi
    
    # Build process
    setup_build_dir
    configure_project
    build_project
    
    # Optional steps
    if [ "$run_tests_flag" = true ]; then
        run_tests
    fi
    
    if [ "$install_flag" = true ]; then
        install_executables
    fi
    
    echo -e "${GREEN}🎉 Video streaming backend build complete!${NC}"
    echo ""
    echo -e "${BLUE}Next steps:${NC}"
    echo "1. Start the video API server: ./build/video-api-server"
    echo "2. Test with: curl http://localhost:8081/api/health"
    echo "3. Configure your React frontend to use: http://localhost:8081"
    echo ""
    echo -e "${YELLOW}📚 Available servers:${NC}"
    echo "• video-api-server (port 8081) - Video streaming API"
    echo "• onestop-radio-server (port 8080) - Main audio/video server"
    echo "• test-server - Development testing"
}

# Run main function with all arguments
main "$@"