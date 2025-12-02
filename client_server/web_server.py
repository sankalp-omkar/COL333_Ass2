"""
Web Server for River and Stones Game
Serves web GUI and handles bot connections with integrated game logging

BOT TIMING GUIDE:
================
To ensure fair and accurate time tracking, bots should:

1. Request game state: GET /bot/game_state/<player>
   - Response includes "timestamp" field (server time when state was sent)

2. Calculate thinking time:
   - start_time = response["timestamp"]  # or time.time() when received
   - ... perform computation ...
   - thinking_time = time.time() - start_time

3. Submit move with thinking time: POST /bot/move/<player>
   {
       "move": {...},
       "thinking_time": <float seconds>
   }

If "thinking_time" is provided, ONLY that amount will be deducted from your clock.
If not provided, time since last move will be used (includes network latency - less accurate).
"""

import asyncio
import json
import logging
import time
from pathlib import Path
from typing import Dict, Any, Optional, List
import threading
from datetime import datetime

try:
    from flask import Flask, render_template, request, jsonify
    from flask_socketio import SocketIO, emit
except ImportError:
    print("Flask and Flask-SocketIO required. Install with: pip install flask flask-socketio")
    exit(1)

from gameEngine import (
    default_start_board, score_cols_for, get_win_count, 
    validate_and_apply_move, check_win, board_to_ascii,
    compute_final_scores, Piece
)

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# ============================================================================
# GameLogger Class
# ============================================================================

class GameLogger:
    """
    Efficiently logs game states using compact string representations.
    Each board position is encoded as a single character.
    """
    
    # Encoding scheme: each character represents a piece type
    ENCODING = {
        # Circle pieces
        ('circle', 'stone', None): 'A',
        ('circle', 'stone', 'horizontal'): 'A',
        ('circle', 'stone', 'vertical'): 'A',
        ('circle', 'river', 'horizontal'): 'B',
        ('circle', 'river', 'vertical'): 'C',
        
        # Square pieces
        ('square', 'stone', None): 'a',
        ('square', 'stone', 'horizontal'): 'a',
        ('square', 'stone', 'vertical'): 'a',
        ('square', 'river', 'horizontal'): 'b',
        ('square', 'river', 'vertical'): 'c',
        
        # Empty cell
        (None, None, None): '.'
    }
    
    DECODING = {
        'A': ('circle', 'stone', None),
        'B': ('circle', 'river', 'horizontal'),
        'C': ('circle', 'river', 'vertical'),
        'a': ('square', 'stone', None),
        'b': ('square', 'river', 'horizontal'),
        'c': ('square', 'river', 'vertical'),
        '.': (None, None, None)
    }
    
    def __init__(self, rows: int, cols: int):
        self.rows = rows
        self.cols = cols
        self.moves = []
        self.states = []  # List of compact board state strings
        self.metadata = {
            'rows': rows,
            'cols': cols,
            'timestamp': time.time(),
            'version': '1.0',
            'winner': None,
            'final_scores': None,
            'game_duration': None
        }
    
    def log_move(self, move: Dict[str, Any], player: str, board: List[List[Optional[Piece]]]):
        """Log a move and the resulting board state."""
        board_state = self.encode_board(board)
        self.states.append(board_state)
        self.moves.append({
            'player': player,
            'move': move,
            'state_index': len(self.states) - 1,
            'timestamp': time.time()
        })
    
    def record_game_outcome(self, winner: Optional[str], final_scores: Dict[str, float]):
        """Record the final game outcome and scores."""
        self.metadata['winner'] = winner
        self.metadata['final_scores'] = final_scores
        self.metadata['game_duration'] = time.time() - self.metadata['timestamp']
    
    def encode_board(self, board: List[List[Optional[Piece]]]) -> str:
        """Encode entire board as a compact string."""
        encoded_chars = []
        for row in board:
            for cell in row:
                if cell is None:
                    encoded_chars.append('.')
                else:
                    key = (cell.owner, cell.side, cell.orientation)
                    
                    # Try direct lookup first
                    if key in self.ENCODING:
                        encoded_chars.append(self.ENCODING[key])
                    else:
                        # Fallback: log the unexpected key for debugging
                        print(f"Warning: Unexpected piece configuration: {key}, treating as stone")
                        # Treat as stone regardless of orientation
                        fallback_key = (cell.owner, 'stone', 'horizontal')
                        encoded_chars.append(self.ENCODING.get(fallback_key, '.'))
        
        return ''.join(encoded_chars)
    
    def decode_board(self, encoded: str) -> List[List[Optional[Piece]]]:
        """Decode compact string back to board state."""
        board = []
        chars = list(encoded)
        
        for y in range(self.rows):
            row = []
            for x in range(self.cols):
                idx = y * self.cols + x
                if idx >= len(chars):
                    row.append(None)
                    continue
                    
                char = chars[idx]
                if char == '.':
                    row.append(None)
                elif char in self.DECODING:
                    owner, side, orientation = self.DECODING[char]
                    if owner is None:
                        row.append(None)
                    else:
                        row.append(Piece(owner, side, orientation))
                else:
                    # Unknown character, treat as empty
                    row.append(None)
            board.append(row)
        
        return board
    
    def save(self, filename: str):
        """Save game log to file."""
        data = {
            'metadata': self.metadata,
            'states': self.states,
            'moves': self.moves
        }
        
        try:
            with open(filename, 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=2)
            logger.info(f"Game log saved to {filename}")
            return True
        except Exception as e:
            logger.error(f"Error saving game log: {e}")
            return False
    
    def get_stats(self) -> Dict[str, Any]:
        """Get statistics about the game log."""
        total_storage = len(self.states) * self.rows * self.cols if self.states else 0
        return {
            'total_moves': len(self.moves),
            'total_states': len(self.states),
            'board_dimensions': f"{self.rows}x{self.cols}",
            'total_storage_bytes': total_storage,
            'winner': self.metadata['winner'],
            'final_scores': self.metadata['final_scores'],
            'game_duration_seconds': self.metadata['game_duration']
        }

# ============================================================================
# Game Coordinator with Logging
# ============================================================================

class GameCoordinator:
    """Manages game state and coordinates between web GUI and bot players"""
    
    def __init__(self):
        self.games: Dict[str, Dict[str, Any]] = {}  # game_id -> game_state
        self.current_game_id = "main"
        self.bot_connections = {}  # port -> bot_info
        self.socketio = None
        self.log_directory = Path("game_logs")
        self.log_directory.mkdir(exist_ok=True)
        
    def create_game(self, board_size: str = "small", enable_logging: bool = True) -> str:
        """Create a new game with specified board size"""
        game_id = f"game_{int(time.time())}"
        
        # Determine board dimensions
        if board_size == "small":
            rows, cols = 13, 12
        elif board_size == "medium":
            rows, cols = 15, 14
        elif board_size == "large":
            rows, cols = 17, 16
        else:
            rows, cols = 13, 12
            
        # Initialize game state
        board = default_start_board(rows, cols)
        score_cols = score_cols_for(cols)
        win_count = get_win_count(cols)
        
        # Initialize game logger if logging is enabled
        game_logger = GameLogger(rows, cols) if enable_logging else None
        
        game_state = {
            "id": game_id,
            "board_size": board_size,
            "rows": rows,
            "cols": cols,
            "board": board,
            "score_cols": score_cols,
            "win_count": win_count,
            "current_player": "circle",
            "players": {
                "circle": {"connected": False, "port": 8181, "time_left": 600.0},
                "square": {"connected": False, "port": 8182, "time_left": 600.0}
            },
            "game_status": "waiting",  # waiting, active, finished
            "winner": None,
            "turn_count": 0,
            "last_move_time": time.time(),
            "game_log": [],
            "logger": game_logger,  # GameLogger instance
            "logging_enabled": enable_logging
        }
        
        self.games[game_id] = game_state
        self.current_game_id = game_id
        logger.info(f"Created new game {game_id} with board size {board_size} (logging: {enable_logging})")
        return game_id
    
    def get_game(self, game_id: str = None) -> Optional[Dict[str, Any]]:
        """Get game state by ID"""
        if game_id is None:
            game_id = self.current_game_id
        return self.games.get(game_id)
    
    def connect_bot(self, player: str, port: int, bot_info: Dict[str, Any]) -> bool:
        """Connect a bot player to the game"""
        game = self.get_game()
        if not game:
            return False
            
        if player not in ["circle", "square"]:
            return False
            
        # Update player connection status
        game["players"][player]["connected"] = True
        game["players"][player]["bot_info"] = bot_info
        self.bot_connections[port] = {"player": player, "game_id": game["id"], "info": bot_info}
        
        logger.info(f"Bot connected: {player} on port {port}")
        
        # Check if both players are connected
        if all(p["connected"] for p in game["players"].values()):
            game["game_status"] = "active"
            game["last_move_time"] = time.time()
            logger.info(f"Game {game['id']} started - both players connected")
            self._broadcast_game_update(game)
            
        return True
    
    def disconnect_bot(self, port: int) -> bool:
        """Disconnect a bot player"""
        if port not in self.bot_connections:
            return False
            
        bot_info = self.bot_connections[port]
        game = self.get_game(bot_info["game_id"])
        
        if game:
            player = bot_info["player"]
            game["players"][player]["connected"] = False
            if game["game_status"] == "active":
                game["game_status"] = "paused"
                logger.info(f"Game paused - {player} disconnected")
        
        del self.bot_connections[port]
        logger.info(f"Bot disconnected from port {port}")
        return True
    
    def make_move(self, player: str, move: Dict[str, Any], thinking_time: float = None) -> Dict[str, Any]:
        """Process a move from a bot player
        
        Args:
            player: The player making the move ('circle' or 'square')
            move: The move dictionary
            thinking_time: Optional - actual time spent by bot thinking (in seconds)
                          If not provided, falls back to measuring time since last move
        """
        game = self.get_game()
        if not game:
            return {"success": False, "error": "No active game"}
            
        if game["game_status"] != "active":
            return {"success": False, "error": "Game not active"}
            
        if game["current_player"] != player:
            return {"success": False, "error": "Not your turn"}
        
        # Calculate elapsed time for current player
        if thinking_time is not None:
            elapsed_time = thinking_time
        else:
            elapsed_time = time.time() - game["last_move_time"]
        
        game["players"][player]["time_left"] -= elapsed_time
        
        # Check for timeout
        if game["players"][player]["time_left"] <= 0:
            winner = "square" if player == "circle" else "circle"
            game["winner"] = winner
            game["game_status"] = "finished"
            
            # Record game outcome in logger
            if game["logging_enabled"] and game["logger"]:
                final_scores = compute_final_scores(
                    game["board"], winner, game["rows"], game["cols"], game["score_cols"],
                    remaining_times={
                        "circle": game["players"]["circle"]["time_left"],
                        "square": game["players"]["square"]["time_left"]
                    }
                )
                game["logger"].record_game_outcome(winner, final_scores)
                self._save_game_log(game)
            
            result = {"success": True, "timeout": True, "winner": winner}
            self._broadcast_game_update(game)
            return result
        
        # Validate and apply move
        success, message = validate_and_apply_move(
            game["board"], move, player, 
            game["rows"], game["cols"], game["score_cols"]
        )
        
        if success:
            # Log the move in GameLogger
            if game["logging_enabled"] and game["logger"]:
                game["logger"].log_move(move, player, game["board"])
            
            # Log the move in game history
            game["game_log"].append({
                "turn": game["turn_count"] + 1,
                "player": player,
                "move": move,
                "time": datetime.now().isoformat(),
                "time_left": game["players"][player]["time_left"]
            })
            
            # Check for win condition
            winner = check_win(game["board"], game["rows"], game["cols"], game["score_cols"])
            
            if winner:
                game["winner"] = winner
                game["game_status"] = "finished"
                # Calculate final scores
                final_scores = compute_final_scores(
                    game["board"], winner, game["rows"], game["cols"], game["score_cols"],
                    remaining_times={
                        "circle": game["players"]["circle"]["time_left"],
                        "square": game["players"]["square"]["time_left"]
                    }
                )
                game["final_scores"] = final_scores
                
                # Record game outcome in logger
                if game["logging_enabled"] and game["logger"]:
                    game["logger"].record_game_outcome(winner, final_scores)
                    self._save_game_log(game)
                
                logger.info(f"Game finished! Winner: {winner}")
            else:
                # Check for turn limit
                if game["turn_count"] >= 1000:
                    game["winner"] = None
                    game["game_status"] = "finished"
                    # Calculate final scores for draw
                    final_scores = compute_final_scores(
                        game["board"], None, game["rows"], game["cols"], game["score_cols"],
                        remaining_times={
                            "circle": game["players"]["circle"]["time_left"],
                            "square": game["players"]["square"]["time_left"]
                        }
                    )
                    game["final_scores"] = final_scores
                    
                    # Record game outcome in logger
                    if game["logging_enabled"] and game["logger"]:
                        game["logger"].record_game_outcome(None, final_scores)
                        self._save_game_log(game)
                    
                    logger.info("Turn limit (1000) reached. Game ends in a draw.")
                else:
                    # Switch to next player
                    game["current_player"] = "square" if player == "circle" else "circle"
                    game["turn_count"] += 1
                    game["last_move_time"] = time.time()
            
            # Broadcast update to web clients
            self._broadcast_game_update(game)
            
            return {"success": True, "message": message, "winner": winner}
        else:
            # Invalid move: opponent wins immediately
            winner = "square" if player == "circle" else "circle"
            game["winner"] = winner
            game["game_status"] = "finished"
            # Calculate final scores with player who made invalid move as loser
            final_scores = compute_final_scores(
                game["board"], winner, game["rows"], game["cols"], game["score_cols"],
                remaining_times={
                    "circle": game["players"]["circle"]["time_left"],
                    "square": game["players"]["square"]["time_left"]
                }
            )
            game["final_scores"] = final_scores
            
            # Record game outcome in logger
            if game["logging_enabled"] and game["logger"]:
                game["logger"].record_game_outcome(winner, final_scores)
                self._save_game_log(game)
            
            logger.info(f"Invalid move by {player}: {message}. Winner: {winner}")
            
            # Broadcast update to web clients
            self._broadcast_game_update(game)
            
            return {"success": False, "error": message, "invalid_move": True, "winner": winner}
    
    def _save_game_log(self, game: Dict[str, Any]):
        """Save the game log to a file"""
        if not game["logging_enabled"] or not game["logger"]:
            return
        
        timestamp = datetime.fromtimestamp(game["logger"].metadata['timestamp'])
        filename = f"game_{game['id']}_{timestamp.strftime('%Y%m%d_%H%M%S')}.json"
        filepath = self.log_directory / filename
        
        if game["logger"].save(str(filepath)):
            logger.info(f"Game log saved: {filepath}")
            # Print stats
            stats = game["logger"].get_stats()
            logger.info(f"Game stats: {stats}")
    
    def _broadcast_game_update(self, game: Dict[str, Any]):
        """Broadcast game state update to web clients"""
        if self.socketio:
            # Convert board to serializable format
            serialized_board = []
            for row in game["board"]:
                serialized_row = []
                for cell in row:
                    if cell:
                        serialized_row.append(cell.to_dict())
                    else:
                        serialized_row.append(None)
                serialized_board.append(serialized_row)
            
            update_data = {
                "board": serialized_board,
                "current_player": game["current_player"],
                "game_status": game["game_status"],
                "turn_count": game["turn_count"],
                "players": game["players"],
                "winner": game.get("winner"),
                "final_scores": game.get("final_scores"),
                "board_size": game["board_size"],
                "rows": game["rows"],
                "cols": game["cols"],
                "score_cols": game["score_cols"],
                "win_count": game["win_count"]
            }
            
            self.socketio.emit('game_update', update_data)
    
    def get_game_state_for_bot(self, player: str) -> Optional[Dict[str, Any]]:
        """Get game state formatted for bot consumption"""
        game = self.get_game()
        if not game:
            return None
            
        # Convert board to bot format
        bot_board = []
        for row in game["board"]:
            bot_row = []
            for cell in row:
                if cell:
                    bot_row.append(cell.to_dict())
                else:
                    bot_row.append(None)
            bot_board.append(bot_row)
        
        return {
            "board": bot_board,
            "rows": game["rows"],
            "cols": game["cols"],
            "score_cols": game["score_cols"],
            "current_player": game["current_player"],
            "your_turn": game["current_player"] == player,
            "time_left": game["players"][player]["time_left"],
            "opponent_time": game["players"]["square" if player == "circle" else "circle"]["time_left"],
            "game_status": game["game_status"],
            "turn_count": game["turn_count"],
            "timestamp": time.time()
        }
    
    def get_game_log_stats(self, game_id: str = None) -> Optional[Dict[str, Any]]:
        """Get statistics about the game log"""
        game = self.get_game(game_id)
        if not game or not game["logging_enabled"] or not game["logger"]:
            return None
        return game["logger"].get_stats()

# Initialize game coordinator
coordinator = GameCoordinator()

# Initialize Flask app
app = Flask(__name__)
app.config['SECRET_KEY'] = 'river_stones_secret_key'
socketio = SocketIO(app, cors_allowed_origins="*")
coordinator.socketio = socketio

# ============================================================================
# Web Routes
# ============================================================================

@app.route('/')
def index():
    """Serve the main game interface"""
    return render_template('index.html')

@app.route('/static/<path:filename>')
def static_files(filename):
    """Serve static files"""
    return app.send_static_file(filename)

@app.route('/api/create_game', methods=['POST'])
def create_game():
    """API endpoint to create a new game"""
    data = request.get_json() or {}
    board_size = data.get('board_size', 'small')
    enable_logging = data.get('enable_logging', True)
    
    if board_size not in ['small', 'medium', 'large']:
        return jsonify({"error": "Invalid board size"}), 400
    
    game_id = coordinator.create_game(board_size, enable_logging)
    game = coordinator.get_game(game_id)
    
    return jsonify({
        "success": True,
        "game_id": game_id,
        "board_size": board_size,
        "rows": game["rows"],
        "cols": game["cols"],
        "logging_enabled": enable_logging,
        "ports": {
            "circle": 8181,
            "square": 8182
        }
    })

@app.route('/api/game_state')
def get_game_state():
    """Get current game state for web interface"""
    game = coordinator.get_game()
    if not game:
        return jsonify({"error": "No active game"}), 404
    
    # Convert board for web display
    serialized_board = []
    for row in game["board"]:
        serialized_row = []
        for cell in row:
            if cell:
                serialized_row.append(cell.to_dict())
            else:
                serialized_row.append(None)
        serialized_board.append(serialized_row)
    
    return jsonify({
        "id": game["id"],
        "board": serialized_board,
        "board_size": game["board_size"],
        "rows": game["rows"],
        "cols": game["cols"],
        "score_cols": game["score_cols"],
        "win_count": game["win_count"],
        "current_player": game["current_player"],
        "players": game["players"],
        "game_status": game["game_status"],
        "turn_count": game["turn_count"],
        "winner": game.get("winner"),
        "final_scores": game.get("final_scores"),
        "logging_enabled": game["logging_enabled"]
    })

@app.route('/api/game_log_stats')
def get_game_log_stats():
    """Get statistics about the current game log"""
    stats = coordinator.get_game_log_stats()
    if stats:
        return jsonify(stats)
    else:
        return jsonify({"error": "No game log available"}), 404

# ============================================================================
# Bot API Endpoints
# ============================================================================

@app.route('/bot/connect/<player>', methods=['POST'])
def connect_bot(player):
    """Endpoint for bots to connect"""
    data = request.get_json() or {}
    bot_info = {
        "name": data.get("name", f"{player}_bot"),
        "strategy": data.get("strategy", "random"),
        "version": data.get("version", "1.0")
    }
    
    # Create game if it doesn't exist
    if not coordinator.get_game():
        board_size = data.get("board_size", "small")
        enable_logging = data.get("enable_logging", True)
        coordinator.create_game(board_size, enable_logging)
        logger.info(f"Auto-created game with board size: {board_size}, logging: {enable_logging}")
    
    port = 8181 if player == "circle" else 8182
    success = coordinator.connect_bot(player, port, bot_info)
    
    if success:
        return jsonify({"success": True, "message": f"Bot connected as {player}"})
    else:
        return jsonify({"error": "Failed to connect bot"}), 400

@app.route('/bot/disconnect/<player>', methods=['POST'])
def disconnect_bot(player):
    """Endpoint for bots to disconnect"""
    port = 8181 if player == "circle" else 8182
    success = coordinator.disconnect_bot(port)
    
    if success:
        return jsonify({"success": True, "message": f"Bot disconnected"})
    else:
        return jsonify({"error": "Bot not found"}), 404

@app.route('/bot/game_state/<player>')
def get_bot_game_state(player):
    """Get game state for bot player"""
    game_state = coordinator.get_game_state_for_bot(player)
    if game_state:
        return jsonify(game_state)
    else:
        return jsonify({"error": "No active game"}), 404

@app.route('/bot/move/<player>', methods=['POST'])
def bot_make_move(player):
    """Endpoint for bots to make moves"""
    data = request.get_json()
    if not data or 'move' not in data:
        return jsonify({"error": "No move provided"}), 400
    
    # Extract thinking time if provided by bot
    thinking_time = data.get('thinking_time')
    
    result = coordinator.make_move(player, data['move'], thinking_time)
    return jsonify(result)

# ============================================================================
# WebSocket Events
# ============================================================================

@socketio.on('connect')
def on_connect():
    """Handle web client connection"""
    logger.info("Web client connected")
    game = coordinator.get_game()
    if game:
        coordinator._broadcast_game_update(game)

@socketio.on('disconnect')
def on_disconnect():
    """Handle web client disconnection"""
    logger.info("Web client disconnected")

@socketio.on('create_game')
def on_create_game(data):
    """Handle game creation from web client"""
    board_size = data.get('board_size', 'small')
    enable_logging = data.get('enable_logging', True)
    game_id = coordinator.create_game(board_size, enable_logging)
    game = coordinator.get_game(game_id)
    coordinator._broadcast_game_update(game)
    emit('game_created', {
        "game_id": game_id, 
        "board_size": board_size,
        "logging_enabled": enable_logging
    })

# ============================================================================
# Main
# ============================================================================

if __name__ == '__main__':
    import sys
    
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    
    print(f"""
🎮 River and Stones Web Server Starting...

Web Interface: http://localhost:{port}
Game Logs Directory: {coordinator.log_directory.absolute()}

Bot Connections:
  - Circle Player: http://localhost:{port}/bot/connect/circle
  - Square Player: http://localhost:{port}/bot/connect/square

Bot API Endpoints:
  - Connect: POST /bot/connect/<player>
  - Game State: GET /bot/game_state/<player>
  - Make Move: POST /bot/move/<player>
  - Disconnect: POST /bot/disconnect/<player>

Web API Endpoints:
  - Create Game: POST /api/create_game
  - Game State: GET /api/game_state
  - Log Stats: GET /api/game_log_stats
    """)
    
    socketio.run(app, host='0.0.0.0', port=port, debug=True, allow_unsafe_werkzeug=True)