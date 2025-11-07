# OneStopRadio Audio Backend

Mock Audio Streaming Service for Railway deployment.

## Overview

This service provides a Node.js mock implementation of the audio streaming backend while the C++ system is under development. It simulates all the audio streaming endpoints that the React frontend expects.

## Endpoints

- `GET /api/audio/stream/status` - Get streaming status and statistics
- `POST /api/audio/stream/connect` - Connect to streaming server
- `POST /api/audio/stream/disconnect` - Disconnect from streaming server  
- `POST /api/audio/stream/start` - Start live streaming
- `POST /api/audio/stream/stop` - Stop live streaming
- `POST /api/audio/stream/metadata` - Update track metadata

## Railway Deployment

This service is configured to deploy on Railway as a Node.js application using the mock server implementation.

### Environment Variables

- `PORT` - Server port (set automatically by Railway)

### Startup Command

```bash
node mock-stream-server.js
```

## Development

For local development:

```bash
npm start
# or
node mock-stream-server.js
```

## Future C++ Integration

This mock service will be replaced with the full C++ audio streaming system when ready. The API endpoints are designed to match the planned C++ implementation.