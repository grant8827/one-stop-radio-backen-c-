#!/bin/bash

# OneStopRadio C++ Backend Installation Script for macOS
# This script installs all required dependencies and builds the project

set -e  # Exit on any error

echo "🎵 OneStopRadio C++ Backend Setup"
echo "=================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    print_error "This script is designed for macOS. Please see README.md for other platforms."
    exit 1
fi

# Check if Xcode Command Line Tools are installed
print_status "Checking for Xcode Command Line Tools..."
if ! command -v clang++ &> /dev/null; then
    print_warning "Xcode Command Line Tools not found. Installing..."
    xcode-select --install
    print_status "Please complete the Xcode Command Line Tools installation and run this script again."
    exit 0
else
    print_status "✅ Xcode Command Line Tools found"
fi

# Check if Homebrew is installed
print_status "Checking for Homebrew..."
if ! command -v brew &> /dev/null; then
    print_warning "Homebrew not found. Installing..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    
    # Add Homebrew to PATH for M1 Macs
    if [[ -d "/opt/homebrew" ]]; then
        echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
        eval "$(/opt/homebrew/bin/brew shellenv)"
    fi
else
    print_status "✅ Homebrew found"
fi

# Update Homebrew
print_status "Updating Homebrew..."
brew update

# Install dependencies
print_status "Installing dependencies..."

dependencies=(
    "cmake"
    "ffmpeg"
    "boost"
    "libshout"
    "nlohmann-json"
    "websocketpp"
    "openssl"
)

for dep in "${dependencies[@]}"; do
    print_status "Installing $dep..."
    brew install "$dep" || print_warning "Failed to install $dep or already installed"
done

print_status "✅ All dependencies installed"

# Create build directory
print_status "Creating build directory..."
mkdir -p build
cd build

# Configure with CMake
print_status "Configuring project with CMake..."
cmake .. || {
    print_error "CMake configuration failed. Trying with alternative approach..."
    cd ..
    
    # Try using Makefile instead
    print_status "Using Makefile as fallback..."
    make check-deps
    make clean
    make
    
    if [[ -f "radio_server" ]]; then
        print_status "✅ Build successful using Makefile"
        echo ""
        echo "🎉 OneStopRadio C++ Backend built successfully!"
        echo ""
        echo "To run the server:"
        echo "  ./radio_server"
        echo ""
        echo "To install system-wide:"
        echo "  sudo make install"
        echo ""
        exit 0
    else
        print_error "Build failed with both CMake and Makefile"
        exit 1
    fi
}

# Build with make
print_status "Building project..."
make -j$(sysctl -n hw.ncpu) || {
    print_error "Build failed"
    exit 1
}

print_status "✅ Build successful"

# Check if executable was created
if [[ -f "radio_server" ]]; then
    print_status "✅ radio_server executable created"
else
    print_error "radio_server executable not found"
    exit 1
fi

echo ""
echo "🎉 OneStopRadio C++ Backend setup complete!"
echo ""
echo "Next steps:"
echo "1. Run the server:    cd build && ./radio_server"
echo "2. Configure via API: http://localhost:8080"
echo "3. Check logs:        tail -f radio_server.log"
echo ""
echo "For more information, see README.md"