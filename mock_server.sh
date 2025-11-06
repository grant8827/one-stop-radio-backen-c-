#!/bin/bash

# OneStopRadio Mock C++ Server - Shell Implementation
# Simple HTTP response server using netcat

PORT=8080
echo "🎵 OneStopRadio C++ Media Server Mock (Shell)"
echo "============================================="
echo "🚀 Starting simple HTTP server on port $PORT"
echo ""
echo "✅ Ready for React frontend connections!"
echo "🔄 Providing basic /api/health endpoint..."
echo ""
echo "Press Ctrl+C to stop server"

# Create HTTP response function
create_health_response() {
    cat << 'EOF'
HTTP/1.1 200 OK
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Content-Type: application/json
Content-Length: 151

{
  "status": "ok",
  "service": "C++ Media Server Mock",
  "version": "1.0.0",
  "uptime": 42,
  "timestamp": "2024-01-01T12:00:00.000Z"
}
EOF
}

create_cors_response() {
    cat << 'EOF'
HTTP/1.1 200 OK
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Content-Length: 0

EOF
}

# Start listening
while true; do
    {
        # Read the request
        read -r REQUEST_LINE
        while read -r HEADER && [ "$HEADER" != $'\r' ]; do
            case "$HEADER" in
                Host:*) HOST_HEADER="$HEADER" ;;
            esac
        done
        
        # Parse request
        METHOD=$(echo "$REQUEST_LINE" | cut -d' ' -f1)
        PATH=$(echo "$REQUEST_LINE" | cut -d' ' -f2)
        
        echo "📡 $METHOD $PATH"
        
        # Handle request
        case "$METHOD" in
            "OPTIONS")
                create_cors_response
                ;;
            "GET")
                case "$PATH" in
                    "/api/health")
                        create_health_response
                        ;;
                    *)
                        create_health_response  # Default to health response
                        ;;
                esac
                ;;
            *)
                create_health_response  # Default response
                ;;
        esac
    } | nc -l -p $PORT -q 1
    
    # Check if we should continue (basic signal handling)
    if [ $? -ne 0 ]; then
        echo ""
        echo "🛑 Server stopped"
        break
    fi
done