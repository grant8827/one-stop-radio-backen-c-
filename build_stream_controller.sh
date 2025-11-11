#!/bin/bash

# OneStopRadio Stream Controller Build Script
# Builds the C++ Stream Controller API service

set -e  # Exit on any error

echo "======================================"
echo "Building OneStopRadio Stream Controller"
echo "======================================"

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
if [[ ! -f "CMakeLists.txt" ]]; then
    print_error "CMakeLists.txt not found. Are you in the backend-c++ directory?"
    exit 1
fi

print_status "Current directory: $(pwd)"

# Check for required dependencies
print_status "Checking dependencies..."

# Check for libshout
if ! pkg-config --exists shout; then
    print_error "libshout not found. Please install libshout-dev:"
    echo "  Ubuntu/Debian: sudo apt-get install libshout3-dev"
    echo "  macOS: brew install shout"
    echo "  CentOS/RHEL: sudo yum install libshout-devel"
    exit 1
fi
print_success "libshout found: $(pkg-config --modversion shout)"

# Check for Boost
if ! ldconfig -p | grep -q libboost_system; then
    print_warning "Boost libraries might not be installed"
    echo "  Ubuntu/Debian: sudo apt-get install libboost-all-dev"
    echo "  macOS: brew install boost"
    echo "  CentOS/RHEL: sudo yum install boost-devel"
fi

# Check for nlohmann/json
if ! pkg-config --exists nlohmann_json; then
    print_warning "nlohmann/json not found via pkg-config"
    echo "  Ubuntu/Debian: sudo apt-get install nlohmann-json3-dev"
    echo "  macOS: brew install nlohmann-json"
    echo "  Manual install: https://github.com/nlohmann/json"
fi

# Create build directory
print_status "Creating build directory..."
mkdir -p build
cd build

# Configure with CMake
print_status "Configuring CMake..."
if cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; then
    print_success "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

# Build the project
print_status "Building Stream Controller..."
if make stream-controller-api -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4); then
    print_success "Build completed successfully"
else
    print_error "Build failed"
    exit 1
fi

# Check if executable was created
if [[ -f "stream-controller-api" ]]; then
    print_success "Executable created: $(pwd)/stream-controller-api"
    
    # Show file info
    file_size=$(du -h stream-controller-api | cut -f1)
    print_status "Binary size: $file_size"
    
    # Test basic functionality
    print_status "Testing executable..."
    if ./stream-controller-api --help > /dev/null 2>&1; then
        print_success "Executable runs correctly"
    else
        print_warning "Executable might have runtime issues"
    fi
    
    echo ""
    echo "======================================"
    echo -e "${GREEN}Build Complete!${NC}"
    echo "======================================"
    echo "Executable location: $(pwd)/stream-controller-api"
    echo ""
    echo "To run the service:"
    echo "  ./stream-controller-api --port 8083"
    echo ""
    echo "Available options:"
    echo "  --port <port>     HTTP API port (default: 8083)"
    echo "  --config <file>   Configuration file path"
    echo "  --help            Show help message"
    echo ""
    echo "API endpoints will be available at:"
    echo "  http://localhost:8083/health"
    echo "  http://localhost:8083/api/v1/streams"
    echo "======================================"
else
    print_error "Executable not created"
    exit 1
fi