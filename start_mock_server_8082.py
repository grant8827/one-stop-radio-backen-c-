#!/usr/bin/env python3
"""
OneStopRadio C++ Media Server Mock - Port 8082
Modified version of mock_cpp_server.py to run on port 8082
"""

import json
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
import urllib.parse as urlparse
from datetime import datetime
import threading
import random

# Mock server state
server_state = {
    'status': 'running',
    'uptime': 0,
    'start_time': time.time(),
    'mixer': {
        'crossfader': 0.5,
        'master_volume': 0.8,
        'channels': [
            {
                'id': 1,
                'volume': 0.7,
                'loaded_track': None,
                'is_playing': False,
                'position': 0.0,
                'eq': {'low': 0.0, 'mid': 0.0, 'high': 0.0}
            },
            {
                'id': 2,
                'volume': 0.7,
                'loaded_track': None,
                'is_playing': False,
                'position': 0.0,
                'eq': {'low': 0.0, 'mid': 0.0, 'high': 0.0}
            }
        ]
    },
    'microphone': {
        'enabled': False,
        'muted': False,
        'gain': 0.5,
        'talkover': False
    },
    'audio_levels': {
        'master_left': 0.0,
        'master_right': 0.0,
        'channel_1_left': 0.0,
        'channel_1_right': 0.0,
        'channel_2_left': 0.0,
        'channel_2_right': 0.0,
        'microphone': 0.0
    }
}

class MockCppHandler(BaseHTTPRequestHandler):
    
    def do_OPTIONS(self):
        """Handle CORS preflight requests"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type, Authorization')
        self.end_headers()
    
    def do_GET(self):
        """Handle GET requests"""
        parsed_path = urlparse.urlparse(self.path)
        path = parsed_path.path
        
        print(f"🎛️ GET {path}")
        
        # Update uptime
        server_state['uptime'] = int(time.time() - server_state['start_time'])
        
        if path == '/api/health':
            self.send_json_response({
                'status': 'ok',
                'service': 'C++ Media Server Mock',
                'version': '1.0.0',
                'uptime': server_state['uptime'],
                'timestamp': datetime.now().isoformat()
            })
            
        elif path == '/api/mixer/status':
            # Simulate realistic audio levels
            self.simulate_audio_levels()
            
            self.send_json_response({
                'success': True,
                'mixer': server_state['mixer'],
                'audio_levels': server_state['audio_levels']
            })
            
        elif path == '/api/mixer/microphone/status':
            self.send_json_response({
                'success': True,
                'microphone': server_state['microphone']
            })
            
        else:
            self.send_json_response({
                'success': False,
                'error': 'Endpoint not found',
                'available_endpoints': [
                    'GET /api/health - Server health check',
                    'GET /api/mixer/status - Mixer status and levels',
                    'GET /api/mixer/microphone/status - Microphone status',
                    'POST /api/mixer/microphone/start - Start microphone',
                    'POST /api/mixer/microphone/stop - Stop microphone',
                    'POST /api/mixer/microphone/gain - Set microphone gain'
                ]
            }, status_code=404)
    
    def do_POST(self):
        """Handle POST requests"""
        parsed_path = urlparse.urlparse(self.path)
        path = parsed_path.path
        
        print(f"🎛️ POST {path}")
        
        # Read request body
        content_length = int(self.headers.get('Content-Length', 0))
        body = {}
        if content_length > 0:
            try:
                body_raw = self.rfile.read(content_length)
                body = json.loads(body_raw.decode('utf-8'))
            except:
                pass
        
        if path == '/api/mixer/microphone/start':
            gain = body.get('gain', 75.0)
            server_state['microphone']['enabled'] = True
            server_state['microphone']['gain'] = max(0.0, min(100.0, float(gain))) / 100.0
            server_state['microphone']['talkover'] = True  # Auto-enable talkover
            print(f"🎤 Microphone STARTED - Gain: {gain}% - Talkover: ON")
            
            self.send_json_response({
                'success': True,
                'action': 'microphone_started',
                'gain': gain,
                'talkover_enabled': True,
                'message': 'Microphone started with auto-talkover'
            })
            
        elif path == '/api/mixer/microphone/stop':
            server_state['microphone']['enabled'] = False
            server_state['microphone']['talkover'] = False
            print(f"🎤 Microphone STOPPED - Talkover: OFF")
            
            self.send_json_response({
                'success': True,
                'action': 'microphone_stopped',
                'talkover_enabled': False,
                'message': 'Microphone stopped, talkover disabled'
            })
            
        elif path == '/api/mixer/microphone/gain':
            gain = body.get('gain', 75.0)
            server_state['microphone']['gain'] = max(0.0, min(100.0, float(gain))) / 100.0
            print(f"🎤 Microphone gain set to {gain}%")
            
            self.send_json_response({
                'success': True,
                'action': 'microphone_gain_set',
                'gain': gain,
                'normalized_gain': server_state['microphone']['gain']
            })
            
        else:
            self.send_json_response({
                'success': False,
                'error': f'POST endpoint not implemented: {path}'
            }, status_code=404)
    
    def simulate_audio_levels(self):
        """Simulate realistic audio levels"""
        # Microphone levels
        if server_state['microphone']['enabled']:
            server_state['audio_levels']['microphone'] = random.uniform(0.3, 0.8) * server_state['microphone']['gain']
        else:
            server_state['audio_levels']['microphone'] = 0.0
            
        # Channel levels
        for i, channel in enumerate(server_state['mixer']['channels']):
            if channel['is_playing']:
                server_state['audio_levels'][f'channel_{i+1}_left'] = random.uniform(0.3, 0.9)
                server_state['audio_levels'][f'channel_{i+1}_right'] = random.uniform(0.3, 0.9)
            else:
                server_state['audio_levels'][f'channel_{i+1}_left'] = 0.0
                server_state['audio_levels'][f'channel_{i+1}_right'] = 0.0
                
        # Master levels (simplified mixing)
        server_state['audio_levels']['master_left'] = max(
            server_state['audio_levels']['channel_1_left'] * server_state['mixer']['channels'][0]['volume'],
            server_state['audio_levels']['channel_2_left'] * server_state['mixer']['channels'][1]['volume'],
            server_state['audio_levels']['microphone']
        ) * server_state['mixer']['master_volume']
        
        server_state['audio_levels']['master_right'] = server_state['audio_levels']['master_left']
    
    def send_json_response(self, data, status_code=200):
        """Send JSON response with CORS headers"""
        self.send_response(status_code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type, Authorization')
        self.end_headers()
        
        response = json.dumps(data, indent=2)
        self.wfile.write(response.encode('utf-8'))
    
    def log_message(self, format, *args):
        """Custom logging to reduce noise"""
        pass  # Suppress default HTTP logging

def run_server():
    """Start the mock C++ media server on port 8082"""
    server_address = ('localhost', 8082)
    httpd = HTTPServer(server_address, MockCppHandler)
    
    print('🎵 OneStopRadio C++ Media Server Mock (Python)')
    print('=' * 50)
    print(f'🚀 Server running on http://localhost:8082')
    print('')
    print('📡 Available endpoints:')
    print('  GET  /api/health - Health check')
    print('  GET  /api/mixer/status - Mixer status and levels')
    print('  GET  /api/mixer/microphone/status - Microphone status')
    print('  POST /api/mixer/microphone/start - Start microphone with gain')
    print('  POST /api/mixer/microphone/stop - Stop microphone')
    print('  POST /api/mixer/microphone/gain - Set microphone gain')
    print('')
    print('✅ Ready for React frontend connections!')
    print('🔄 Simulating realistic DJ mixer behavior...')
    print('🎤 Auto-talkover enabled when microphone starts')
    print('')
    print('Press Ctrl+C to stop server')
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print('\n🛑 Shutting down C++ Mock Server...')
        httpd.server_close()
        print('✅ Server stopped gracefully')

if __name__ == '__main__':
    run_server()