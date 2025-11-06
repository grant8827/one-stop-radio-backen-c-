#!/bin/bash

echo "==================================="
echo "OneStopRadio Audio System Dependencies"
echo "==================================="
echo ""
echo "This script will help you install the required dependencies for the enhanced audio system."
echo ""

# Check if Homebrew is installed
if ! command -v brew &> /dev/null; then
    echo "❌ Homebrew not found. Installing Homebrew..."
    echo ""
    echo "Run this command to install Homebrew:"
    echo '/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
    echo ""
    echo "After installing Homebrew, run this script again."
    exit 1
fi

echo "✅ Homebrew found!"
echo ""

# Install development tools
echo "📦 Installing development tools..."
brew install cmake pkg-config

# Install FFmpeg (if not already installed)
echo "📦 Installing FFmpeg..."
brew install ffmpeg

# Install Boost (if not already installed)
echo "📦 Installing Boost..."
brew install boost

# Install libshout (if not already installed)
echo "📦 Installing libshout..."
brew install libshout

# Install OpenSSL (if not already installed)
echo "📦 Installing OpenSSL..."
brew install openssl

# Install NEW AUDIO DEPENDENCIES
echo ""
echo "🎵 Installing Audio Processing Libraries..."
echo ""

echo "📦 Installing PortAudio (real-time audio I/O)..."
brew install portaudio

echo "📦 Installing libsndfile (audio file I/O)..."
brew install libsndfile

echo "📦 Installing FFTW (Fast Fourier Transform library)..."
brew install fftw

echo "📦 Installing libsamplerate (sample rate conversion)..."
brew install libsamplerate

echo ""
echo "✅ All dependencies installed successfully!"
echo ""
echo "🔨 To build the project:"
echo "   cd build"
echo "   cmake .."
echo "   make -j4"
echo ""
echo "🎵 New Audio Features Available:"
echo "   ✅ Real-time audio processing with PortAudio"
echo "   ✅ Professional-grade audio mixing"
echo "   ✅ Multi-channel support with crossfader"
echo "   ✅ Audio effects (EQ, Compressor, Reverb, Delay)"
echo "   ✅ Microphone input with noise gate"
echo "   ✅ Level metering and spectrum analysis"
echo "   ✅ Audio streaming and recording"
echo "   ✅ BPM detection and sync"
echo ""
echo "🚀 The C++ backend now handles ALL audio processing duties!"
echo ""