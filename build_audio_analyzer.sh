#!/bin/bash

# Build script for OneStopRadio C++ Audio Analyzer
# This script builds the audio analyzer with all necessary dependencies

set -e  # Exit on any error

echo "========================================"
echo "OneStopRadio C++ Audio Analyzer Builder"
echo "========================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BUILD_DIR="$SCRIPT_DIR/build"
SRC_DIR="$SCRIPT_DIR"

# Parse command line arguments
BUILD_TYPE="Release"
CLEAN_BUILD=false
INSTALL=false
RUN_EXAMPLE=false
EXAMPLE_AUDIO=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -i|--install)
            INSTALL=true
            shift
            ;;
        -r|--run)
            RUN_EXAMPLE=true
            shift
            ;;
        -f|--file)
            EXAMPLE_AUDIO="$2"
            RUN_EXAMPLE=true
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -d, --debug      Build in debug mode"
            echo "  -c, --clean      Clean build directory before building"
            echo "  -i, --install    Install after building"
            echo "  -r, --run        Run example after building"
            echo "  -f, --file FILE  Run example with specific audio file"
            echo "  -h, --help       Show this help message"
            echo ""
            echo "Dependencies required:"
            echo "  - libfftw3-dev (FFTW3 single precision)"
            echo "  - libsndfile1-dev (libsndfile)" 
            echo "  - libjsoncpp-dev or nlohmann-json3-dev"
            echo "  - cmake (>= 3.16)"
            echo "  - build-essential"
            echo ""
            echo "Install dependencies on Ubuntu/Debian:"
            echo "  sudo apt update"
            echo "  sudo apt install build-essential cmake libfftw3-dev libsndfile1-dev libjsoncpp-dev"
            echo ""
            echo "Install dependencies on macOS:"
            echo "  brew install fftw libsndfile jsoncpp cmake"
            exit 0
            ;;
        *)
            echo "Unknown option $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Function to print colored messages
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

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check dependencies
print_status "Checking dependencies..."

if ! command_exists cmake; then
    print_error "cmake not found. Please install cmake (>= 3.16)"
    exit 1
fi

if ! command_exists pkg-config; then
    print_error "pkg-config not found. Please install pkg-config"
    exit 1
fi

# Check for required libraries
check_library() {
    if ! pkg-config --exists "$1"; then
        print_error "Library $1 not found. Please install the corresponding development package."
        return 1
    else
        print_success "Found $1 $(pkg-config --modversion $1)"
        return 0
    fi
}

MISSING_LIBS=false

if ! check_library "fftw3f"; then
    print_error "Please install libfftw3-dev (Ubuntu/Debian) or fftw (macOS)"
    MISSING_LIBS=true
fi

if ! check_library "sndfile"; then
    print_error "Please install libsndfile1-dev (Ubuntu/Debian) or libsndfile (macOS)"
    MISSING_LIBS=true
fi

# Check for JSON library (jsoncpp or nlohmann-json)
if ! check_library "jsoncpp"; then
    print_warning "jsoncpp not found, will try to use nlohmann-json"
    if ! command_exists "nlohmann_json" && ! find /usr -name "nlohmann" -type d 2>/dev/null | head -1; then
        print_warning "nlohmann-json also not found, but build may still work"
    fi
fi

if [ "$MISSING_LIBS" = true ]; then
    print_error "Missing required libraries. Please install them and try again."
    exit 1
fi

print_success "All dependencies found!"

# Clean build if requested
if [ "$CLEAN_BUILD" = true ]; then
    print_status "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
print_status "Creating build directory..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
print_status "Configuring build with CMake..."
print_status "Build type: $BUILD_TYPE"

cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      "$SRC_DIR"

if [ $? -ne 0 ]; then
    print_error "CMake configuration failed!"
    exit 1
fi

# Build
print_status "Building audio analyzer..."
CPU_COUNT=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
print_status "Using $CPU_COUNT parallel jobs"

make -j"$CPU_COUNT" audio_analyzer audio_analyzer_example

if [ $? -ne 0 ]; then
    print_error "Build failed!"
    exit 1
fi

print_success "Build completed successfully!"

# Install if requested
if [ "$INSTALL" = true ]; then
    print_status "Installing..."
    sudo make install
    print_success "Installation completed!"
fi

# Show build results
echo ""
print_success "Build Summary:"
echo "  Build directory: $BUILD_DIR"
echo "  Audio analyzer library: $BUILD_DIR/libaudio_analyzer.a"
echo "  Example executable: $BUILD_DIR/audio_analyzer_example"
echo ""

# Run example if requested
if [ "$RUN_EXAMPLE" = true ]; then
    if [ -f "$BUILD_DIR/audio_analyzer_example" ]; then
        if [ -n "$EXAMPLE_AUDIO" ]; then
            if [ -f "$EXAMPLE_AUDIO" ]; then
                print_status "Running example with audio file: $EXAMPLE_AUDIO"
                echo "========================================"
                "$BUILD_DIR/audio_analyzer_example" "$EXAMPLE_AUDIO"
                echo "========================================"
            else
                print_error "Audio file not found: $EXAMPLE_AUDIO"
                exit 1
            fi
        else
            print_warning "No audio file specified. Usage:"
            echo "  $BUILD_DIR/audio_analyzer_example <audio_file>"
            echo ""
            echo "Supported formats: WAV, FLAC, OGG, MP3, AAC, M4A, AIFF, AU"
        fi
    else
        print_error "Example executable not found!"
        exit 1
    fi
fi

echo ""
print_success "Audio analyzer build completed!"
echo "To analyze an audio file:"
echo "  $BUILD_DIR/audio_analyzer_example /path/to/your/audio/file.wav"
echo ""
echo "The analyzer will generate:"
echo "  - <audio_file>.waveform.json (for React frontend)"
echo "  - <audio_file>.waveform.osrwf (binary format for faster loading)"