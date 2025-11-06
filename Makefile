# Simple Makefile for systems without CMake
# This is a fallback build system

CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

# Include directories (adjust paths based on your system)
INCLUDES = -Iinclude \
           -I/usr/local/include \
           -I/opt/homebrew/include \
           -I/usr/include/boost

# Library directories  
LIBDIRS = -L/usr/local/lib \
          -L/opt/homebrew/lib \
          -L/usr/lib

# Libraries to link
LIBS = -lavcodec -lavformat -lavutil -lswresample -lswscale \
       -lportaudio -lsndfile \
       -lboost_system -lboost_thread -lboost_filesystem \
       -lshout -lssl -lcrypto -lpthread -lsqlite3

# Source files
SRCDIR = src
SOURCES = $(SRCDIR)/main.cpp \
          $(SRCDIR)/http_server.cpp \
          $(SRCDIR)/webrtc_server.cpp \
          $(SRCDIR)/stream_manager.cpp \
          $(SRCDIR)/audio_system.cpp \
          $(SRCDIR)/audio_encoder.cpp \
          $(SRCDIR)/audio_stream_encoder.cpp \
          $(SRCDIR)/video_stream_manager.cpp \
          $(SRCDIR)/radio_control.cpp \
          $(SRCDIR)/database_manager.cpp \
          $(SRCDIR)/config_manager.cpp \
          $(SRCDIR)/utils/logger.cpp

# Object files
OBJDIR = obj
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

# Target executable
TARGET = radio_server

# Default target
all: $(TARGET)

# Create object directory
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Compile source files to objects
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Link executable
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LIBDIRS) $(LIBS) -o $@

# Clean build files
clean:
	rm -rf $(OBJDIR) $(TARGET)

# Install target (copies to /usr/local/bin)
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

# Development target with debug symbols
debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

# Check if dependencies are available
check-deps:
	@echo "Checking for required dependencies..."
	@pkg-config --exists libavcodec || echo "❌ FFmpeg libavcodec not found"
	@pkg-config --exists libavformat || echo "❌ FFmpeg libavformat not found"  
	@pkg-config --exists libavutil || echo "❌ FFmpeg libavutil not found"
	@pkg-config --exists libswresample || echo "❌ FFmpeg libswresample not found"
	@test -f /usr/local/include/boost/beast.hpp || test -f /opt/homebrew/include/boost/beast.hpp || echo "❌ Boost.Beast not found"
	@test -f /usr/local/include/shout/shout.h || test -f /opt/homebrew/include/shout/shout.h || echo "❌ libshout not found"
	@test -f /usr/local/include/nlohmann/json.hpp || test -f /opt/homebrew/include/nlohmann/json.hpp || echo "❌ nlohmann/json not found"
	@echo "✅ Dependency check complete"

# Help target
help:
	@echo "OneStopRadio C++ Backend Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build the radio server (default)"
	@echo "  debug      - Build with debug symbols"
	@echo "  clean      - Remove build files"
	@echo "  install    - Install to /usr/local/bin"
	@echo "  check-deps - Check for required dependencies"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make           # Build the server"
	@echo "  make debug     # Build with debugging"
	@echo "  make clean     # Clean build files"

.PHONY: all clean install debug check-deps help