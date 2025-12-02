"""
Student Agent Implementation for River and Stones Game

This file contains the essential utilities and template for implementing your AI agent.
Your task is to complete the StudentAgent class with intelligent move selection.

Game Rules:
- Goal: Get 4 of your stones into the opponent's scoring area
- Pieces can be stones or rivers (horizontal/vertical orientation)
- Actions: move, push, flip (stone↔river), rotate (river orientation)
- Rivers enable flow-based movement across the board

Your Task:
Implement the choose() method in the StudentAgent class to select optimal moves.
You may add any helper methods and modify the evaluation function as needed.
"""
import queue
import random
import copy
from typing import List, Dict, Any, Optional, Tuple
from abc import ABC, abstractmethod


# ==================== GAME UTILITIES ====================
# Essential utility functions for game state analysis

def in_bounds(x: int, y: int, rows: int, cols: int) -> bool:
    """Check if coordinates are within board boundaries."""
    return 0 <= x < cols and 0 <= y < rows


def score_cols_for(cols: int) -> List[int]:
    """Get the column indices for scoring areas."""
    w = 4
    start = max(0, (cols - w) // 2)
    return list(range(start, start + w))


def top_score_row() -> int:
    """Get the row index for Circle's scoring area."""
    return 2


def bottom_score_row(rows: int) -> int:
    """Get the row index for Square's scoring area."""
    return rows - 3


def is_opponent_score_cell(x: int, y: int, player: str, rows: int, cols: int, score_cols: List[int]) -> bool:
    """Check if a cell is in the opponent's scoring area."""
    if player == "circle":
        return (y == bottom_score_row(rows)) and (x in score_cols)
    else:
        return (y == top_score_row()) and (x in score_cols)


def is_own_score_cell(x: int, y: int, player: str, rows: int, cols: int, score_cols: List[int]) -> bool:
    """Check if a cell is in the player's own scoring area."""
    if player == "circle":
        return (y == top_score_row()) and (x in score_cols)
    else:
        return (y == bottom_score_row(rows)) and (x in score_cols)


def get_opponent(player: str) -> str:
    """Get the opponent player identifier."""
    return "square" if player == "circle" else "circle"


# ==================== MOVE GENERATION HELPERS ====================
# equivalent to get_river_flow_destinations from gameEngine
def agent_river_flow(board, rx, ry, sx, sy, player, rows, cols, score_cols, river_push=False):
    destinations = []
    visited = set()
    q = queue.Queue()
    q.put((rx, ry))

    while not q.empty():
        x, y = q.get()
        if (x, y) in visited or not in_bounds(x, y, rows, cols):
            continue
        visited.add((x, y))

        cell = board[sy][sx] if river_push and x == rx and y == ry else board[y][x]

        if not cell:
            if not is_opponent_score_cell(x, y, player, rows, cols, score_cols):
                destinations.append((x, y))
            continue

        if cell.side == "stone":
            continue

        dirs = [(1, 0), (-1, 0)] if cell.orientation == "horizontal" else [(0, 1), (0, -1)]

        for dx, dy in dirs:
            nx, ny = x + dx, y + dy
            while in_bounds(nx, ny, rows, cols):
                if is_opponent_score_cell(nx, ny, player, rows, cols, score_cols):
                    break

                next_cell = board[ny][nx]
                if not next_cell:
                    destinations.append((nx, ny))
                    nx += dx
                    ny += dy
                    continue

                if nx == sx and ny == sy:
                    nx += dx
                    ny += dy
                    continue

                if next_cell.side == "river":
                    q.put((nx, ny))
                break

    out = []
    seen = set()
    for d in destinations:
        if d not in seen:
            seen.add(d)
            out.append(d)

    return out

# provides set of valid moves for a piece
def get_valid_moves_for_piece(board, x: int, y: int, player: str, rows: int, cols: int, score_cols: List[int]) -> List[Dict[str, Any]]:
    dirs = [(1, 0), (-1, 0), (0, 1), (0, -1)]
    moves = set()
    pushes = []

    if not in_bounds(x, y, rows, cols):
        return []

    p = board[y][x]
    if p is None or p.owner != player:
        return []

    for dx, dy in dirs:
        tx, ty = x + dx, y + dy
        if not in_bounds(tx, ty, rows, cols):
            continue
        if is_opponent_score_cell(tx, ty, player, rows, cols, score_cols):
            continue

        target = board[ty][tx]

        if target is None:
            moves.add((tx, ty))
        elif target.side == "river":  # river
            flow = agent_river_flow(board, tx, ty, x, y, player, rows, cols, score_cols)
            for dest in flow:
                moves.add(dest)
        elif target.side == "stone":  # stone
            if p.side == "stone":
                px, py = tx + dx, ty + dy
                pushed_player = target.owner
                if (in_bounds(px, py, rows, cols) and
                        board[py][px] is None and
                        not is_opponent_score_cell(px, py, pushed_player, rows, cols, score_cols)):
                    pushes.append(((tx, ty), (px, py)))
            else:
                pushed_player = target.owner
                flow = agent_river_flow(board, tx, ty, x, y, pushed_player, rows, cols, score_cols, True)
                for dest in flow:
                    if not is_opponent_score_cell(dest[0],dest[1], pushed_player, rows, cols, score_cols):
                        pushes.append(((tx, ty), dest))
    out = []
    for m in moves:
        out.append({'type': 'move', 'from': [x, y], 'to': m})
    for p in pushes:
        out.append({'type': 'push', 'from': [x, y], 'to': p[0], 'pushed_to': p[1]})
    if p.side == "stone":
        out.append({"action": "flip", "from": [x, y], "orientation": "horizontal"})
        out.append({"action": "flip", "from": [x, y], "orientation": "vertical"})
    else:
        out.append({"action": "flip", "from": [x, y], "orientation": None})
        new_orient = "vertical" if p.orientation == "horizontal" else "horizontal"
        out.append({"action": "rotate", "from": [x, y], "orientation": new_orient})
    return out


def generate_all_moves(board: List[List[Any]], player: str, rows: int, cols: int, score_cols: List[int]) -> List[
    Dict[str, Any]]:
    """
    Generate all legal moves for the current player.

    Args:
        board: Current board state
        player: Current player ("circle" or "square")
        rows, cols: Board dimensions
        score_cols: Scoring column indices

    Returns:
        List of all valid move dictionaries
    """
    moves = []
    for y in range(rows):
        for x in range(cols):
            p = board[y][x]
            if not p or p.owner != player:
                continue

            valid_moves = get_valid_moves_for_piece(board, x, y, player, rows, cols, score_cols)
            moves.extend(valid_moves)

    return moves


# ==================== BOARD EVALUATION ====================

def count_stones_in_scoring_area(board: List[List[Any]], player: str, rows: int, cols: int,
                                 score_cols: List[int]) -> int:
    """Count how many stones a player has in their scoring area."""
    count = 0

    if player == "circle":
        score_row = top_score_row()
    else:
        score_row = bottom_score_row(rows)

    for x in score_cols:
        if in_bounds(x, score_row, rows, cols):
            piece = board[score_row][x]
            if piece and piece.owner == player and piece.side == "stone":
                count += 1

    return count


def basic_evaluate_board(board: List[List[Any]], player: str, rows: int, cols: int, score_cols: List[int]) -> float:
    """
    Basic board evaluation function.

    Returns a score where higher values are better for the given player.
    Students can use this as a starting point and improve it.
    """
    score = 0.0
    opponent = get_opponent(player)

    # Count stones in scoring areas
    player_scoring_stones = count_stones_in_scoring_area(board, player, rows, cols, score_cols)
    opponent_scoring_stones = count_stones_in_scoring_area(board, opponent, rows, cols, score_cols)

    score += player_scoring_stones * 100
    score -= opponent_scoring_stones * 100

    # Count total pieces and positional factors
    for y in range(rows):
        for x in range(cols):
            piece = board[y][x]
            if piece and piece.owner == player and piece.side == "stone":
                # Basic positional scoring
                if player == "circle":
                    score += (rows - y) * 0.1
                else:
                    score += y * 0.1

    return score


def simulate_move(board: List[List[Any]], move: Dict[str, Any], player: str, rows: int, cols: int,
                  score_cols: List[int]) -> Tuple[bool, Any]:
    """
    Simulate a move on a copy of the board.

    Args:
        board: Current board state
        move: Move to simulate
        player: Player making the move
        rows, cols: Board dimensions
        score_cols: Scoring column indices

    Returns:
        (success: bool, new_board_state or error_message)
    """
    # Import the game engine's move validation function
    try:
        from gameEngine import validate_and_apply_move
        board_copy = copy.deepcopy(board)
        success, message = validate_and_apply_move(board_copy, move, player, rows, cols, score_cols)
        return success, board_copy if success else message
    except ImportError:
        # Fallback to basic simulation if game engine not available
        return True, copy.deepcopy(board)


# ==================== BASE AGENT CLASS ====================

class BaseAgent(ABC):
    """
    Abstract base class for all agents.
    """

    def __init__(self, player: str):
        """Initialize agent with player identifier."""
        self.player = player
        self.opponent = get_opponent(player)

    @abstractmethod
    def choose(self, board: List[List[Any]], rows: int, cols: int, score_cols: List[int], current_player_time: float,
               opponent_time: float) -> Optional[Dict[str, Any]]:
        """
        Choose the best move for the current board state.

        Args:
            board: 2D list representing the game board
            rows, cols: Board dimensions
            score_cols: List of column indices for scoring areas

        Returns:
            Dictionary representing the chosen move, or None if no moves available
        """
        pass


# ==================== STUDENT AGENT IMPLEMENTATION ====================

class StudentAgent(BaseAgent):
    """
    Student Agent Implementation

    TODO: Implement your AI agent for the River and Stones game.
    The goal is to get 4 of your stones into the opponent's scoring area.

    You have access to these utility functions:
    - generate_all_moves(): Get all legal moves for current player
    - basic_evaluate_board(): Basic position evaluation
    - simulate_move(): Test moves on board copy
    - count_stones_in_scoring_area(): Count stones in scoring positions
    """

    def __init__(self, player: str):
        super().__init__(player)
        # TODO: Add any initialization you need

    def choose(self, board: List[List[Any]], rows: int, cols: int, score_cols: List[int], current_player_time: float,
               opponent_time: float) -> Optional[Dict[str, Any]]:
        """
        Choose the best move for the current board state.

        Args:
            board: 2D list representing the game board
            rows, cols: Board dimensions
            score_cols: Column indices for scoring areas

        Returns:
            Dictionary representing your chosen move
        """
        moves = generate_all_moves(board, self.player, rows, cols, score_cols)

        if not moves:
            return None

        # TODO: Replace random selection with your AI algorithm
        return random.choice(moves)


# ==================== TESTING HELPERS ====================

def test_student_agent():
    """
    Basic test to verify the student agent can be created and make moves.
    """
    print("Testing StudentAgent...")

    try:
        from gameEngine import default_start_board, DEFAULT_ROWS, DEFAULT_COLS

        rows, cols = DEFAULT_ROWS, DEFAULT_COLS
        score_cols = score_cols_for(cols)
        board = default_start_board(rows, cols)

        agent = StudentAgent("circle")
        move = agent.choose(board, rows, cols, score_cols, 1.0, 1.0)

        if move:
            print("✓ Agent successfully generated a move")
        else:
            print("✗ Agent returned no move")

    except ImportError:
        agent = StudentAgent("circle")
        print("✓ StudentAgent created successfully")


if __name__ == "__main__":
    # Run basic test when file is executed directly
    test_student_agent()