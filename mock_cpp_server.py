#!/usr/bin/env python3
"""
OneStopRadio C++ Media Server Mock - Python Implementation
Provides HTTP endpoints that the frontend expects for the C++ backend service
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
            
        elif path == '/api/stats':
            self.send_json_response({
                'success': True,
                'stats': {
                    'uptime': server_state['uptime'],
                    'cpu_usage': round(random.uniform(10, 30), 1),
                    'memory_usage': round(random.uniform(40, 80), 1),
                    'audio_buffer_health': 'good',
                    'active_connections': random.randint(0, 5)
                }
            })
            
        elif path == '/api/mixer/status':
            # Simulate realistic audio levels
            if server_state['mixer']['channels'][0]['is_playing']:
                server_state['audio_levels']['channel_1_left'] = random.uniform(0.3, 0.9)
                server_state['audio_levels']['channel_1_right'] = random.uniform(0.3, 0.9)
            else:
                server_state['audio_levels']['channel_1_left'] = 0.0
                server_state['audio_levels']['channel_1_right'] = 0.0
                
            if server_state['mixer']['channels'][1]['is_playing']:
                server_state['audio_levels']['channel_2_left'] = random.uniform(0.3, 0.9)
                server_state['audio_levels']['channel_2_right'] = random.uniform(0.3, 0.9)
            else:
                server_state['audio_levels']['channel_2_left'] = 0.0
                server_state['audio_levels']['channel_2_right'] = 0.0
                
            # Master levels based on crossfader and channel volumes
            crossfader = server_state['mixer']['crossfader']
            ch1_vol = server_state['mixer']['channels'][0]['volume']
            ch2_vol = server_state['mixer']['channels'][1]['volume']
            master_vol = server_state['mixer']['master_volume']
            
            # Simple crossfader calculation
            ch1_output = server_state['audio_levels']['channel_1_left'] * ch1_vol * (1.0 - crossfader) * master_vol
            ch2_output = server_state['audio_levels']['channel_2_left'] * ch2_vol * crossfader * master_vol
            server_state['audio_levels']['master_left'] = max(ch1_output, ch2_output)
            server_state['audio_levels']['master_right'] = max(ch1_output, ch2_output)
            
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
            
        elif path == '/api/audio/levels':
            self.send_json_response({
                'success': True,
                'levels': server_state['audio_levels']
            })
            
        else:
            self.send_json_response({
                'success': False,
                'error': 'Endpoint not found',
                'available_endpoints': [
                    'GET /api/health - Server health check',
                    'GET /api/stats - Server statistics',
                    'GET /api/mixer/status - Mixer status and levels',
                    'GET /api/mixer/microphone/status - Microphone status',
                    'GET /api/audio/levels - Real-time audio levels',
                    'POST /api/mixer/crossfader - Set crossfader position',
                    'POST /api/mixer/channel/{id}/volume - Set channel volume',
                    'POST /api/mixer/channel/{id}/load - Load track to channel',
                    'POST /api/mixer/channel/{id}/playback - Control playback',
                    'POST /api/mixer/microphone/toggle - Toggle microphone'
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
        
        if path == '/api/mixer/crossfader':
            value = body.get('value', 0.5)
            server_state['mixer']['crossfader'] = max(0.0, min(1.0, float(value)))
            print(f"🎚️ Crossfader set to {server_state['mixer']['crossfader']}")
            
            self.send_json_response({
                'success': True,
                'action': 'crossfader_set',
                'value': server_state['mixer']['crossfader']
            })
            
        elif path.startswith('/api/mixer/channel/') and path.endswith('/volume'):
            # Extract channel ID
            parts = path.split('/')
            try:
                channel_id = int(parts[4]) - 1  # Convert to 0-based index
                if 0 <= channel_id < 2:
                    value = body.get('value', 0.7)
                    server_state['mixer']['channels'][channel_id]['volume'] = max(0.0, min(1.0, float(value)))
                    print(f"🔊 Channel {channel_id + 1} volume set to {server_state['mixer']['channels'][channel_id]['volume']}")
                    
                    self.send_json_response({
                        'success': True,
                        'action': 'channel_volume_set',
                        'channel': channel_id + 1,
                        'value': server_state['mixer']['channels'][channel_id]['volume']
                    })
                else:
                    self.send_json_response({
                        'success': False,
                        'error': 'Invalid channel ID'
                    }, status_code=400)
            except (ValueError, IndexError):
                self.send_json_response({
                    'success': False,
                    'error': 'Invalid channel ID in path'
                }, status_code=400)
                
        elif path.startswith('/api/mixer/channel/') and path.endswith('/load'):
            # Extract channel ID
            parts = path.split('/')
            try:
                channel_id = int(parts[4]) - 1  # Convert to 0-based index
                if 0 <= channel_id < 2:
                    track_name = body.get('name', f'Track_{int(time.time())}')
                    server_state['mixer']['channels'][channel_id]['loaded_track'] = track_name
                    server_state['mixer']['channels'][channel_id]['position'] = 0.0
                    print(f"💿 Track '{track_name}' loaded to Channel {channel_id + 1}")
                    
                    self.send_json_response({
                        'success': True,
                        'action': 'track_loaded',
                        'channel': channel_id + 1,
                        'track': track_name
                    })
                else:
                    self.send_json_response({
                        'success': False,
                        'error': 'Invalid channel ID'
                    }, status_code=400)
            except (ValueError, IndexError):
                self.send_json_response({
                    'success': False,
                    'error': 'Invalid channel ID in path'
                }, status_code=400)
                
        elif path.startswith('/api/mixer/channel/') and path.endswith('/playback'):
            # Extract channel ID
            parts = path.split('/')
            try:
                channel_id = int(parts[4]) - 1  # Convert to 0-based index
                if 0 <= channel_id < 2:
                    action = body.get('action', 'play')  # play, pause, stop
                    
                    if action == 'play':
                        server_state['mixer']['channels'][channel_id]['is_playing'] = True
                        print(f"▶️ Channel {channel_id + 1} playing")
                    elif action == 'pause':
                        server_state['mixer']['channels'][channel_id]['is_playing'] = False
                        print(f"⏸️ Channel {channel_id + 1} paused")
                    elif action == 'stop':
                        server_state['mixer']['channels'][channel_id]['is_playing'] = False
                        server_state['mixer']['channels'][channel_id]['position'] = 0.0
                        print(f"⏹️ Channel {channel_id + 1} stopped")
                    
                    self.send_json_response({
                        'success': True,
                        'action': f'playback_{action}',
                        'channel': channel_id + 1,
                        'is_playing': server_state['mixer']['channels'][channel_id]['is_playing']
                    })
                else:
                    self.send_json_response({
                        'success': False,
                        'error': 'Invalid channel ID'
                    }, status_code=400)
            except (ValueError, IndexError):
                self.send_json_response({
                    'success': False,
                    'error': 'Invalid channel ID in path'
                }, status_code=400)
                
        elif path == '/api/mixer/microphone/toggle':
            server_state['microphone']['enabled'] = not server_state['microphone']['enabled']
            print(f"🎤 Microphone {'ON' if server_state['microphone']['enabled'] else 'OFF'}")
            
            self.send_json_response({
                'success': True,
                'action': 'microphone_toggle',
                'enabled': server_state['microphone']['enabled']
            })
            
        elif path == '/api/mixer/master/volume':
            value = body.get('value', 0.8)
            server_state['mixer']['master_volume'] = max(0.0, min(1.0, float(value)))
            print(f"🔊 Master volume set to {server_state['mixer']['master_volume']}")
            
            self.send_json_response({
                'success': True,
                'action': 'master_volume_set',
                'value': server_state['mixer']['master_volume']
            })
            
        else:
            self.send_json_response({
                'success': False,
                'error': f'POST endpoint not implemented: {path}'
            }, status_code=404)
    
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
    """Start the mock C++ media server"""
    server_address = ('localhost', 8080)
    httpd = HTTPServer(server_address, MockCppHandler)
    
    print('🎵 OneStopRadio C++ Media Server Mock (Python)')
    print('=' * 50)
    print(f'🚀 Server running on http://localhost:8080')
    print('')
    print('📡 Available endpoints:')
    print('  GET  /api/health - Health check')
    print('  GET  /api/stats - Server statistics')  
    print('  GET  /api/mixer/status - Mixer status')
    print('  GET  /api/mixer/microphone/status - Mic status')
    print('  POST /api/mixer/crossfader - Set crossfader')
    print('  POST /api/mixer/channel/{id}/volume - Channel volume')
    print('  POST /api/mixer/channel/{id}/load - Load track')
    print('  POST /api/mixer/channel/{id}/playback - Control playback')
    print('  POST /api/mixer/microphone/toggle - Toggle mic')
    print('')
    print('✅ Ready for React frontend connections!')
    print('🔄 Simulating realistic DJ mixer behavior...')
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