# OneStopRadio Video Streaming System

## Architecture Overview

The OneStopRadio video streaming system consists of three main components:

### 1. React Frontend (VideoStreamingControls.tsx)
- Comprehensive video streaming interface
- Camera controls, image/slideshow management
- Social media platform configuration
- Real-time streaming controls
- Text overlay management
- Live audio/video monitoring

### 2. Node.js Mock API Server (backend-nd/server.js)
- Development/testing API server
- Complete video streaming endpoint simulation
- Real-time statistics generation
- Platform-specific configuration handling
- CORS-enabled for frontend development

### 3. C++ Production Backend (backend-c++/)
- High-performance video encoding/streaming
- Real camera capture and processing
- Hardware-accelerated video encoding
- RTMP streaming to multiple platforms
- Production-ready media pipeline

## API Endpoints

### Video Source Control
```bash
# Enable camera
POST /api/video/camera/on

# Disable camera  
POST /api/video/camera/off

# Update camera settings
POST /api/video/camera/settings
{
  "device_id": "camera_id",
  "width": 1920,
  "height": 1080,
  "fps": 30
}

# Set static image
POST /api/video/image
{
  "image_path": "/path/to/image.jpg"
}

# Start slideshow
POST /api/video/slideshow/start
{
  "images": ["/path/to/image1.jpg", "/path/to/image2.jpg"],
  "duration": 5,
  "loop": true,
  "transition": "fade"
}

# Stop slideshow
POST /api/video/slideshow/stop

# Navigate slides
POST /api/video/slideshow/next
POST /api/video/slideshow/previous
```

### Social Media Streaming
```bash
# Configure YouTube streaming
POST /api/video/stream/youtube
{
  "stream_key": "your_youtube_stream_key",
  "title": "Live DJ Set"
}

# Configure Twitch streaming
POST /api/video/stream/twitch
{
  "stream_key": "live_xxxxx_xxxxxxxx",
  "title": "OneStopRadio Live"
}

# Configure Facebook streaming
POST /api/video/stream/facebook
{
  "stream_key": "FB-123456789-0-AbCdEfGhIjKlMnOp",
  "title": "Live Radio Show"
}

# Start multi-platform streaming
POST /api/video/stream/start
{
  "platforms": ["youtube", "twitch", "facebook"]
}

# Stop streaming
POST /api/video/stream/stop
{
  "platforms": ["youtube"] // Optional: stop specific platforms
}
```

### Custom RTMP Streaming
```bash
# Add custom RTMP stream
POST /api/video/rtmp/add
{
  "name": "Custom Server",
  "rtmp_url": "rtmp://your-server.com/live",
  "stream_key": "your_stream_key"
}

# Remove custom RTMP stream
DELETE /api/video/rtmp/{rtmp_id}
```

### Text Overlay
```bash
# Add text overlay
POST /api/video/overlay/text
{
  "text": "Now Playing: Artist - Track",
  "x": 50,
  "y": 950,
  "font": "Arial",
  "font_size": 24
}

# Clear text overlay
POST /api/video/overlay/clear
```

### Status and Statistics
```bash
# Get video streaming status
GET /api/video/status

# Get detailed statistics
GET /api/video/stats
```

## Platform-Specific Configuration

### YouTube Live
- **RTMP URL**: `rtmp://a.rtmp.youtube.com/live2`
- **Stream Key**: Available in YouTube Studio > Create > Go Live
- **Recommended Settings**: 1920x1080 @ 30fps, 4.5 Mbps
- **Requirements**: Channel verification, no recent live streaming restrictions

### Twitch
- **RTMP URL**: `rtmp://live.twitch.tv/app` (or regional servers)
- **Stream Key**: Available in Twitch Creator Dashboard > Settings > Stream
- **Recommended Settings**: 1920x1080 @ 60fps, 6 Mbps
- **Requirements**: Twitch account in good standing

### Facebook Live
- **RTMP URL**: `rtmps://live-api-s.facebook.com:443/rtmp`
- **Stream Key**: Generated in Facebook Live Producer
- **Recommended Settings**: 1280x720 @ 30fps, 4 Mbps
- **Requirements**: Facebook Page or Profile with live streaming enabled

### TikTok Live
- **RTMP URL**: `rtmp://push.live.tiktok.com/live`
- **Stream Key**: Available in TikTok Live Studio
- **Recommended Settings**: 1080x1920 (vertical) @ 30fps, 3 Mbps
- **Requirements**: TikTok account with 1000+ followers

### Instagram Live
- **RTMP URL**: `rtmp://live-upload.instagram.com/rtmp`
- **Stream Key**: Available through Instagram API or third-party tools
- **Recommended Settings**: 1080x1080 (square) @ 30fps, 3.5 Mbps
- **Requirements**: Instagram Business account, third-party streaming app

## Backend Servers

### Development Server (Node.js)
```bash
# Start Node.js mock API server
cd backend-nd
node server.js

# Server runs on http://localhost:8080
# Provides complete API simulation for frontend development
```

### Production Server (C++)
```bash
# Build C++ video streaming backend
cd backend-c++
./build_video_streaming.sh --install-deps --install

# Start video API server
./bin/video-api-server

# Server runs on http://localhost:8081
# Provides real video encoding/streaming capabilities
```

## Camera Settings and Configuration

### Supported Camera Settings
- **Resolution**: 480p, 720p, 1080p, 4K (device dependent)
- **Frame Rate**: 15fps, 24fps, 30fps, 60fps
- **Video Codec**: H.264 (baseline, main, high profiles)
- **Audio Codec**: AAC, Opus
- **Bitrate**: Adaptive based on platform requirements

### Camera Device Selection
The system supports multiple camera inputs:
- Built-in webcam
- USB cameras
- Capture cards
- Virtual cameras (OBS Virtual Camera, etc.)

### Video Encoding Options
- **Hardware Acceleration**: Intel Quick Sync, NVIDIA NVENC, AMD VCE
- **Software Encoding**: x264, x265
- **Quality Presets**: Fast, Medium, Slow (trade-off between speed and quality)
- **Rate Control**: CBR (Constant Bitrate), VBR (Variable Bitrate)

## Image and Slideshow Features

### Supported Image Formats
- JPEG (.jpg, .jpeg)
- PNG (.png)
- BMP (.bmp)
- GIF (.gif) - static images only
- TIFF (.tiff, .tif)

### Slideshow Capabilities
- **Multiple Images**: Support for unlimited images in queue
- **Timing Control**: 1-60 seconds per slide
- **Transitions**: Fade, dissolve, slide, zoom effects
- **Loop Mode**: Continuous slideshow or one-time playback
- **Manual Control**: Next/previous slide navigation
- **Image Scaling**: Automatic fit to stream resolution

### Image Processing
- **Automatic Scaling**: Images scaled to match stream resolution
- **Aspect Ratio**: Maintain aspect ratio with letterboxing/pillarboxing
- **Image Quality**: Optimized encoding for streaming
- **Memory Management**: Efficient image loading and caching

## Audio Integration

### Audio Sources
- **Master Mix**: Audio from DJ mixer channels
- **Microphone**: Live commentary/announcements  
- **System Audio**: Computer audio capture
- **External Audio**: Line-in, audio interfaces

### Audio Processing
- **Real-time Mixing**: Combine multiple audio sources
- **Audio Effects**: EQ, compression, noise gate
- **Level Monitoring**: Visual audio meters
- **Audio Routing**: Send audio to streaming encoder

### Audio Synchronization
- **Lip Sync**: Automatic audio/video synchronization
- **Latency Compensation**: Minimize audio delay
- **Buffer Management**: Stable audio streaming

## Streaming Statistics

### Real-time Metrics
- **Upload Speed**: Current bitrate to each platform
- **Frame Rate**: Actual vs target frame rate
- **Dropped Frames**: Network or encoding issues
- **Stream Health**: Connection quality indicators

### Platform Analytics
- **Viewer Count**: Live audience numbers (where available)
- **Stream Duration**: Total streaming time
- **Data Usage**: Total bytes transmitted
- **Connection Status**: Platform-specific health

### Performance Monitoring
- **CPU Usage**: Encoding performance impact
- **Memory Usage**: Video processing memory consumption
- **Network Latency**: Delay to streaming servers
- **Error Tracking**: Connection failures and retries

## Error Handling and Recovery

### Network Issues
- **Automatic Reconnection**: Retry failed connections
- **Adaptive Bitrate**: Reduce quality during network issues
- **Buffer Management**: Handle temporary disconnections
- **Failover Servers**: Switch to backup RTMP endpoints

### Hardware Issues
- **Camera Disconnection**: Detect and handle camera loss
- **Audio Device Changes**: Adapt to device hot-swapping
- **Encoding Errors**: Fallback encoding methods
- **Resource Limits**: Manage CPU/memory constraints

### Platform-Specific Errors
- **Invalid Stream Keys**: Validation and user feedback
- **Platform Restrictions**: Handle streaming violations
- **API Limits**: Rate limiting and quota management
- **Authentication Issues**: Token refresh and re-auth

## Security and Privacy

### Stream Key Protection
- **Secure Storage**: Encrypted stream key storage
- **Environment Variables**: Keep sensitive data out of code
- **Access Control**: User authentication for streaming controls
- **Audit Logging**: Track streaming activities

### Content Protection
- **DMCA Compliance**: Copyright violation detection
- **Content Filtering**: Inappropriate content screening
- **Age Restrictions**: Platform-appropriate content ratings
- **Privacy Controls**: User data protection

## Development Workflow

### Frontend Development
1. Start Node.js mock server: `cd backend-nd && node server.js`
2. Start React frontend: `cd frontend && npm start`
3. Test video streaming UI components
4. Verify API integration

### Backend Development  
1. Install C++ dependencies: `./build_video_streaming.sh --install-deps`
2. Build video streaming system: `./build_video_streaming.sh`
3. Test with video API server: `./bin/video-api-server`
4. Integrate with frontend

### Production Deployment
1. Build optimized C++ backend
2. Configure platform stream keys
3. Set up monitoring and logging
4. Deploy to production servers
5. Configure load balancing and redundancy

## Troubleshooting

### Common Issues
- **"Camera not found"**: Check camera permissions and device connections
- **"Stream key invalid"**: Verify stream key format and platform settings
- **"Connection failed"**: Check network connectivity and firewall settings
- **"Poor video quality"**: Adjust bitrate and encoding settings
- **"Audio sync issues"**: Check audio buffer settings and latency

### Performance Optimization
- **Hardware Acceleration**: Enable GPU encoding when available
- **Resolution Scaling**: Lower resolution for better performance
- **Frame Rate Adjustment**: Reduce FPS for stability
- **Bitrate Optimization**: Balance quality vs bandwidth
- **Background Processes**: Close unnecessary applications

### Network Optimization
- **Upload Bandwidth**: Ensure adequate upload speed
- **Network Stability**: Use wired connection when possible
- **QoS Settings**: Prioritize streaming traffic
- **Port Configuration**: Open required ports for RTMP
- **Regional Servers**: Use closest streaming endpoints

This video streaming system provides a comprehensive solution for live streaming to multiple social media platforms simultaneously, with professional-grade features for audio/video production and real-time broadcasting.