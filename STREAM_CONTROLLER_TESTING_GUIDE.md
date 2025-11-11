# 🎵 C++ Stream Controller Service - Testing Guide

## 🚀 **Quick Start Testing**

### **1. Build the Service**
```bash
cd backend-c++
./build_stream_controller.sh
```

### **2. Start the Service**
```bash
# Start with default settings (port 8083)
./build/stream-controller-api

# Start with custom port and config
./build/stream-controller-api --port 8083 --config config/stream-controller.json
```

### **3. Health Check**
```bash
curl http://localhost:8083/health
```

Expected response:
```json
{
  "healthy": true,
  "status": "Healthy - 0 streams configured, 0 active",
  "service": "StreamController API",
  "version": "1.0.0",
  "timestamp": 1699747200
}
```

---

## 📋 **API Testing Commands**

### **Create a Stream**
```bash
curl -X POST http://localhost:8083/api/v1/streams \
  -H "Content-Type: application/json" \
  -d '{
    "stream_id": "test-stream-001",
    "user_id": "user123",
    "mount_point": "/test-radio",
    "source_password": "secure123",
    "station_name": "Test Radio Station",
    "description": "A test radio stream for development",
    "genre": "Electronic",
    "quality": 128,
    "max_listeners": 50,
    "server_host": "localhost",
    "server_port": 8000,
    "protocol": "icecast",
    "format": "MP3",
    "public_stream": true
  }'
```

Expected response:
```json
{
  "success": true,
  "message": "Stream created successfully",
  "stream_id": "test-stream-001",
  "mount_point": "/test-radio"
}
```

### **Activate a Stream**
```bash
curl -X POST http://localhost:8083/api/v1/streams/test-stream-001/activate
```

Expected response:
```json
{
  "success": true,
  "message": "Stream activated successfully",
  "stream_id": "test-stream-001"
}
```

### **Get Stream Status**
```bash
curl http://localhost:8083/api/v1/streams/test-stream-001/status
```

Expected response:
```json
{
  "stream_id": "test-stream-001",
  "status": 2,
  "status_name": "ACTIVE",
  "is_connected": true,
  "current_listeners": 0,
  "peak_listeners": 0,
  "bytes_sent": 0,
  "uptime_seconds": 42.5,
  "start_time": 1699747200,
  "last_update": 1699747242
}
```

### **Update Stream Metadata**
```bash
curl -X POST http://localhost:8083/api/v1/streams/test-stream-001/metadata \
  -H "Content-Type: application/json" \
  -d '{
    "title": "Amazing Track",
    "artist": "Cool Artist"
  }'
```

### **List All Streams**
```bash
curl http://localhost:8083/api/v1/streams
```

### **Update Stream Configuration**
```bash
curl -X PUT http://localhost:8083/api/v1/streams/test-stream-001 \
  -H "Content-Type: application/json" \
  -d '{
    "station_name": "Updated Station Name",
    "description": "Updated description",
    "max_listeners": 100,
    "quality": 192
  }'
```

### **Deactivate Stream**
```bash
curl -X POST http://localhost:8083/api/v1/streams/test-stream-001/deactivate
```

### **Delete Stream**
```bash
curl -X DELETE http://localhost:8083/api/v1/streams/test-stream-001
```

### **Reload Server Configuration**
```bash
curl -X POST http://localhost:8083/api/v1/reload
```

---

## 🧪 **Integration Testing with FastAPI**

### **Test Communication Between Services**

1. **Start FastAPI Service** (port 8002)
2. **Start C++ Stream Controller** (port 8083)
3. **Test FastAPI → C++ Communication:**

```python
# In FastAPI service
import httpx

async def test_cpp_service():
    async with httpx.AsyncClient() as client:
        # Create stream via C++ service
        response = await client.post(
            "http://localhost:8083/api/v1/streams",
            json={
                "stream_id": "fastapi-test-001",
                "user_id": "user456",
                "mount_point": "/fastapi-test",
                "source_password": "test123",
                "station_name": "FastAPI Test Stream",
                "description": "Integration test stream",
                "genre": "Test",
                "quality": 128,
                "max_listeners": 25,
                "server_host": "localhost", 
                "server_port": 8000,
                "protocol": "icecast",
                "format": "MP3",
                "public_stream": true
            }
        )
        print(f"Create stream: {response.status_code} - {response.text}")
        
        # Activate stream
        response = await client.post(
            "http://localhost:8083/api/v1/streams/fastapi-test-001/activate"
        )
        print(f"Activate stream: {response.status_code} - {response.text}")
        
        # Get status
        response = await client.get(
            "http://localhost:8083/api/v1/streams/fastapi-test-001/status"
        )
        print(f"Stream status: {response.status_code} - {response.text}")
```

---

## 🔧 **Troubleshooting**

### **Common Issues & Solutions**

#### **1. Service Won't Start**
```bash
# Check if port is in use
lsof -i :8083

# Check dependencies
ldd ./build/stream-controller-api

# Check logs
./build/stream-controller-api --help
```

#### **2. libshout Errors**
```bash
# Ubuntu/Debian
sudo apt-get install libshout3-dev icecast2

# macOS  
brew install shout icecast2

# Test libshout installation
pkg-config --modversion shout
```

#### **3. Icecast Configuration Issues**
```bash
# Check Icecast status
sudo systemctl status icecast2

# Start Icecast manually
sudo icecast2 -c /etc/icecast2/icecast.xml

# Check Icecast logs
tail -f /var/log/icecast2/error.log
```

#### **4. Permission Errors**
```bash
# Create required directories
sudo mkdir -p /var/log/onestopradio
sudo chown $USER:$USER /var/log/onestopradio

# Fix Icecast config permissions
sudo chown $USER:$USER /etc/icecast2/icecast.xml
```

#### **5. JSON Parsing Errors**
- Ensure nlohmann/json is installed
- Check Content-Type headers in requests
- Validate JSON syntax in request bodies

---

## 📊 **Performance Testing**

### **Load Test with Multiple Streams**
```bash
# Create 10 test streams
for i in {1..10}; do
  curl -X POST http://localhost:8083/api/v1/streams \
    -H "Content-Type: application/json" \
    -d "{
      \"stream_id\": \"load-test-$i\",
      \"user_id\": \"user$i\",
      \"mount_point\": \"/load-test-$i\",
      \"source_password\": \"test$i\",
      \"station_name\": \"Load Test Stream $i\",
      \"description\": \"Load testing stream\",
      \"genre\": \"Test\",
      \"quality\": 128,
      \"max_listeners\": 10,
      \"server_host\": \"localhost\",
      \"server_port\": 8000,
      \"protocol\": \"icecast\",
      \"format\": \"MP3\",
      \"public_stream\": true
    }"
done

# Activate all streams
for i in {1..10}; do
  curl -X POST http://localhost:8083/api/v1/streams/load-test-$i/activate
done

# Check service health
curl http://localhost:8083/health

# Get all streams status
curl http://localhost:8083/api/v1/streams
```

### **Memory and CPU Monitoring**
```bash
# Monitor resource usage
top -p $(pgrep stream-controller-api)

# Memory usage
ps aux | grep stream-controller-api

# Network connections
netstat -tulpn | grep :8083
```

---

## 🎯 **Expected Behavior**

### **Service Lifecycle**
1. **Initialization**: Service starts, initializes libshout, sets up HTTP routes
2. **Stream Creation**: Creates mount point entry, updates Icecast config
3. **Stream Activation**: Establishes shout connection, enables streaming
4. **Monitoring**: Tracks connection status, listener count, bandwidth
5. **Deactivation**: Closes connections gracefully, updates status
6. **Cleanup**: Removes mount points, cleans configuration

### **Error Handling**
- Invalid stream configurations return 400 Bad Request
- Missing streams return 404 Not Found  
- Service errors return 500 Internal Server Error
- All errors include descriptive error messages

### **Status Codes**
- `0` PENDING - Stream configuration created
- `1` READY - Mount point ready, not yet activated
- `2` ACTIVE - Currently streaming
- `3` INACTIVE - Temporarily stopped
- `4` ERROR - Error state requiring attention
- `5` SUSPENDED - Administratively suspended
- `6` DELETED - Soft deleted for audit trail

---

**Generated**: November 11, 2025 - OneStopRadio C++ Stream Controller Testing Guide