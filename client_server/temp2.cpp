#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <deque>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <memory>
#include <tuple>
#include <iostream>

#include "learned_weights.h"

namespace py = pybind11;
// ==================== UTILITY STRUCTURES ====================

struct Position {
    int x, y;
    Position() : x(0), y(0) {}
    Position(int x_, int y_) : x(x_), y(y_) {}
    
    bool operator==(const Position& other) const {
        return x == other.x && y == other.y;
    }
    
    bool operator!=(const Position& other) const {
        return !(*this == other);
    }
};

// Hash function for Position
namespace std {
    template <>
    struct hash<Position> {
        size_t operator()(const Position& p) const {
            return hash<int>()(p.x) ^ (hash<int>()(p.y) << 1);
        }
    };
}

struct Cell {
    std::string owner;       // "circle" or "square" or ""
    std::string side;        // "stone" or "river" or ""
    std::string orientation; // "horizontal" or "vertical" or ""
    
    Cell() : owner(""), side(""), orientation("") {}
    
    bool isEmpty() const {
        return owner.empty();
    }
};

struct Move {
    std::string action;              // "move", "push", "flip", "rotate"
    std::vector<int> from_pos;       // [x, y]
    std::vector<int> to_pos;         // [x, y]
    std::vector<int> pushed_to;      // [x, y] for push actions
    std::string orientation;         // for flip actions
    
    Move() : action(""), from_pos(2, 0), to_pos(2, 0), pushed_to(2, 0), orientation("") {}
};

// ==================== UTILITY FUNCTIONS ====================

inline bool in_bounds(int x, int y, int rows, int cols) {
    return x >= 0 && x < cols && y >= 0 && y < rows;
}

inline std::vector<int> score_cols_for(int cols) {
    int w = 4;
    int start = std::max(0, (cols - w) / 2);
    std::vector<int> result;
    for (int i = start; i < start + w; i++) {
        result.push_back(i);
    }
    return result;
}

inline int top_score_row() {
    return 2;
}

inline int bottom_score_row(int rows) {
    return rows - 3;
}

inline std::string get_opponent(const std::string& player) {
    return (player == "circle") ? "square" : "circle";
}

inline bool is_opponent_score_cell(int x, int y, const std::string& player, 
                                   int rows, int cols, const std::vector<int>& score_cols) {
    int target_row = (player == "circle") ? bottom_score_row(rows) : top_score_row();
    if (y != target_row) return false;
    return std::find(score_cols.begin(), score_cols.end(), x) != score_cols.end();
}

inline bool is_my_score_cell(int x, int y, const std::string& player,
                             int rows, int cols, const std::vector<int>& score_cols) {
    int target_row = (player == "circle") ? top_score_row() : bottom_score_row(rows);
    if (y != target_row) return false;
    return std::find(score_cols.begin(), score_cols.end(), x) != score_cols.end();
}

static std::string make_bfs_key(int sx, int sy, const std::vector<Position>& goals, bool use_rivers, const std::string& player) {
    std::ostringstream oss;
    oss << sx << "," << sy << ":" << (use_rivers ? "R1" : "R0") << ":" << player << ":";
    for (const auto &g : goals) {
        oss << g.x << "," << g.y << ";";
    }
    return oss.str();
}

int get_win_count(int board_size) {
    if (board_size == 15) return 5;
    if (board_size == 17) return 6;
    return 4;
}

std::string check_win(const std::vector<std::vector<Cell>>& board, 
                     int rows, int cols, const std::vector<int>& score_cols) {
    const int WIN_COUNT = get_win_count(board.size());
    int top = top_score_row();
    int bot = bottom_score_row(rows);
    int ccount = 0, scount = 0;
    
    for (int x : score_cols) {
        if (in_bounds(x, top, rows, cols)) {
            const Cell& p = board[top][x];
            if (!p.isEmpty() && p.owner == "circle" && p.side == "stone") ccount++;
        }
        if (in_bounds(x, bot, rows, cols)) {
            const Cell& q = board[bot][x];
            if (!q.isEmpty() && q.owner == "square" && q.side == "stone") scount++;
        }
    }
    
    if (ccount >= WIN_COUNT) return "circle";
    if (scount >= WIN_COUNT) return "square";
    return "";
}

// ==================== RIVER FLOW COMPUTATION ====================

std::vector<Position> get_river_flow_destinations(
    const std::vector<std::vector<Cell>>& board,
    int rx, int ry, int sx, int sy, const std::string& player,
    int rows, int cols, const std::vector<int>& score_cols,
    bool river_push = false
) {
    std::vector<Position> destinations;
    std::unordered_set<Position> visited;
    std::deque<Position> queue;
    queue.push_back(Position(rx, ry));
    
    while (!queue.empty()) {
        Position pos = queue.front();
        queue.pop_front();
        
        if (visited.count(pos) || !in_bounds(pos.x, pos.y, rows, cols)) continue;
        visited.insert(pos);
        
        const Cell* cell = &board[pos.y][pos.x];
        if (river_push && pos.x == rx && pos.y == ry) {
            cell = &board[sy][sx];
        }
        
        if (cell->isEmpty()) {
            if (!is_opponent_score_cell(pos.x, pos.y, player, rows, cols, score_cols)) {
                destinations.push_back(pos);
            }
            continue;
        }
        
        if (cell->side != "river") continue;
        
        std::vector<std::pair<int, int>> dirs;
        if (cell->orientation == "horizontal") {
            dirs = {{1, 0}, {-1, 0}};
        } else {
            dirs = {{0, 1}, {0, -1}};
        }
        
        for (auto [dx, dy] : dirs) {
            int nx = pos.x + dx;
            int ny = pos.y + dy;
            
            while (in_bounds(nx, ny, rows, cols)) {
                if (is_opponent_score_cell(nx, ny, player, rows, cols, score_cols)) {
                    break;
                }
                
                const Cell& next_cell = board[ny][nx];
                
                if (next_cell.isEmpty()) {
                    destinations.push_back(Position(nx, ny));
                    nx += dx;
                    ny += dy;
                    continue;
                }
                
                if (nx == sx && ny == sy) {
                    nx += dx;
                    ny += dy;
                    continue;
                }
                
                if (next_cell.side == "river") {
                    queue.push_back(Position(nx, ny));
                    break;
                }
                break;
            }
        }
    }
    
    // Remove duplicates
    std::vector<Position> out;
    std::unordered_set<Position> seen;
    for (const auto& d : destinations) {
        if (!seen.count(d)) {
            seen.insert(d);
            out.push_back(d);
        }
    }
    return out;
}

// ==================== BFS PATHFINDING ====================

struct PathResult {
    double distance;
    std::vector<Position> path;
    
    PathResult() : distance(std::numeric_limits<double>::infinity()) {}
    PathResult(double d, const std::vector<Position>& p) : distance(d), path(p) {}
};

static std::unordered_map<std::string, PathResult> GLOBAL_BFS_CACHE;

PathResult bfs_distance_to_goals(
    const std::vector<std::vector<Cell>>& board,
    int start_x, int start_y,
    const std::vector<Position>& goal_cells,
    const std::string& player,
    int rows, int cols,
    const std::vector<int>& score_cols,
    bool use_rivers = true
) {
    Position start(start_x, start_y);
    
    // Check if already at goal
    for (const auto& goal : goal_cells) {
        if (start == goal) {
            return PathResult(0.0, {start});
        }
    }
    
    struct QueueNode {
        Position pos;
        int dist;
        std::vector<Position> path;
    };
    
    std::deque<QueueNode> queue;
    std::unordered_set<Position> visited;
    
    queue.push_back({start, 0, {start}});
    visited.insert(start);
    
    while (!queue.empty()) {
        QueueNode node = queue.front();
        queue.pop_front();
        
        std::vector<std::pair<int, int>> dirs = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        
        for (auto [dx, dy] : dirs) {
            int nx = node.pos.x + dx;
            int ny = node.pos.y + dy;
            Position next_pos(nx, ny);
            
            if (!in_bounds(nx, ny, rows, cols)) continue;
            if (visited.count(next_pos)) continue;
            if (is_opponent_score_cell(nx, ny, player, rows, cols, score_cols)) continue;
            
            const Cell& cell = board[ny][nx];
            std::vector<Position> new_path = node.path;
            new_path.push_back(next_pos);
            
            // Empty cell - can move here
            if (cell.isEmpty()) {
                for (const auto& goal : goal_cells) {
                    if (next_pos == goal) {
                        return PathResult(node.dist + 1, new_path);
                    }
                }
                visited.insert(next_pos);
                queue.push_back({next_pos, node.dist + 1, new_path});
            }
            // River cell - can flow through if use_rivers
            else if (use_rivers && cell.side == "river") {
                auto flow_dests = get_river_flow_destinations(
                    board, nx, ny, node.pos.x, node.pos.y, player, rows, cols, score_cols
                );
                
                for (const auto& flow_pos : flow_dests) {
                    if (!visited.count(flow_pos)) {
                        std::vector<Position> flow_path = new_path;
                        flow_path.push_back(flow_pos);
                        
                        for (const auto& goal : goal_cells) {
                            if (flow_pos == goal) {
                                return PathResult(node.dist + 1, flow_path);
                            }
                        }
                        visited.insert(flow_pos);
                        queue.push_back({flow_pos, node.dist + 1, flow_path});
                    }
                }
            }
        }
    }
    
    return PathResult(); // No path found
}

PathResult bfs_distance_to_goals_cached(
    const std::vector<std::vector<Cell>>& board,
    int start_x, int start_y,
    const std::vector<Position>& goal_cells,
    const std::string& player,
    int rows, int cols,
    const std::vector<int>& score_cols,
    bool use_rivers = true
) {
    std::string key = make_bfs_key(start_x, start_y, goal_cells, use_rivers, player);
    auto it = GLOBAL_BFS_CACHE.find(key);
    if (it != GLOBAL_BFS_CACHE.end()) {
        return it->second;
    }
    PathResult res = bfs_distance_to_goals(board, start_x, start_y, goal_cells, player, rows, cols, score_cols, use_rivers);
    GLOBAL_BFS_CACHE.emplace(key, res);
    return res;
}

std::pair<double, std::string> bfs_distance_with_flip(
    const std::vector<std::vector<Cell>>& board,
    int start_x, int start_y,
    const std::vector<Position>& goal_cells,
    const std::string& player,
    int rows, int cols,
    const std::vector<int>& score_cols
) {
    const Cell& piece = board[start_y][start_x];
    if (piece.isEmpty() || piece.owner != player) {
        return {std::numeric_limits<double>::infinity(), "none"};
    }
    
    auto current_result = bfs_distance_to_goals_cached(board, start_x, start_y, goal_cells, player, rows, cols, score_cols);
    double current_dist = current_result.distance;
    
    if (piece.side == "stone") {
        double best_dist = current_dist;
        std::string best_orient = "none";
        
        // Try horizontal river
        auto board_copy = board;
        board_copy[start_y][start_x].side = "river";
        board_copy[start_y][start_x].orientation = "horizontal";
        auto h_result = bfs_distance_to_goals_cached(board_copy, start_x, start_y, goal_cells, player, rows, cols, score_cols);
        if (h_result.distance < best_dist) {
            best_dist = h_result.distance;
            best_orient = "horizontal";
        }
        
        // Try vertical river
        board_copy[start_y][start_x].orientation = "vertical";
        auto v_result = bfs_distance_to_goals_cached(board_copy, start_x, start_y, goal_cells, player, rows, cols, score_cols);
        if (v_result.distance < best_dist) {
            best_dist = v_result.distance;
            best_orient = "vertical";
        }
        
        return {best_dist, best_orient};
    }
    
    return {current_dist, "none"};
}

// ==================== STUDENT AGENT CLASS ====================

class StudentAgent {
private:
    std::string player;
    std::string opponent;
    int MAX_DEPTH;
    int moves;
    std::vector<std::unordered_map<std::string, std::string>> last_moves;
    int repetition_limit;
    std::mt19937 rng;

public:
    StudentAgent(const std::string& player_name) 
        : player(player_name), 
          opponent(get_opponent(player_name)),
          MAX_DEPTH(2),
          moves(0),
          repetition_limit(2),
          rng(std::random_device{}())
    {
    }
    
    std::vector<Position> get_my_goal_cells(int rows, int cols, const std::vector<int>& score_cols) {
        int goal_row = (player == "circle") ? top_score_row() : bottom_score_row(rows);
        std::vector<Position> goals;
        for (int x : score_cols) {
            goals.push_back(Position(x, goal_row));
        }
        return goals;
    }
    
    std::vector<Position> get_opponent_goal_cells(int rows, int cols, const std::vector<int>& score_cols) {
        int goal_row = (player == "circle") ? bottom_score_row(rows) : top_score_row();
        std::vector<Position> goals;
        for (int x : score_cols) {
            goals.push_back(Position(x, goal_row));
        }
        return goals;
    }
    
    struct RiverOpportunity {
        std::string action;
        int from_x, from_y;
        std::string orientation;
        double value;
        bool defensive;
        
        RiverOpportunity() : action(""), from_x(0), from_y(0), orientation(""), value(0.0), defensive(false) {}
    };
    
    std::vector<RiverOpportunity> find_river_creation_opportunities(
        const std::vector<std::vector<Cell>>& board,
        int rows, int cols,
        const std::vector<int>& score_cols
    ) {
        std::vector<RiverOpportunity> opportunities;
        auto my_goals = get_my_goal_cells(rows, cols, score_cols);
        
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                const Cell& cell = board[y][x];
                if (!cell.isEmpty() && cell.owner == player && cell.side == "stone") {
                    auto [dist_with_flip, best_orient] = bfs_distance_with_flip(
                        board, x, y, my_goals, player, rows, cols, score_cols
                    );
                    
                    auto current_result = bfs_distance_to_goals_cached(
                        board, x, y, my_goals, player, rows, cols, score_cols
                    );
                    double current_dist = current_result.distance;
                    
                    if (best_orient != "none" && dist_with_flip < current_dist - 1) {
                        RiverOpportunity opp;
                        opp.action = "flip";
                        opp.from_x = x;
                        opp.from_y = y;
                        opp.orientation = best_orient;
                        opp.value = (current_dist - dist_with_flip) * 1000.0;
                        opp.defensive = false;
                        opportunities.push_back(opp);
                    }
                }
            }
        }
        
        std::sort(opportunities.begin(), opportunities.end(),
                 [](const RiverOpportunity& a, const RiverOpportunity& b) {
                     return a.value > b.value;
                 });
        
        return opportunities;
    }
    
    std::vector<RiverOpportunity> find_defensive_river_placements(
        const std::vector<std::vector<Cell>>& board,
        int rows, int cols,
        const std::vector<int>& score_cols
    ) {
        std::vector<RiverOpportunity> defensive_moves;
        auto opp_goals = get_opponent_goal_cells(rows, cols, score_cols);
        auto my_goals = get_my_goal_cells(rows, cols, score_cols);
        
        // Find opponent threats
        struct Threat {
            int x, y;
            double dist;
            std::vector<Position> path;
        };
        std::vector<Threat> opp_threats;
        
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                const Cell& cell = board[y][x];
                if (!cell.isEmpty() && cell.owner == opponent && cell.side == "stone") {
                    auto result = bfs_distance_to_goals_cached(board, x, y, opp_goals, opponent, rows, cols, score_cols);
                    if (result.distance < 6) {
                        opp_threats.push_back({x, y, result.distance, result.path});
                    }
                }
            }
        }
        
        // For each threat, find blocking positions
        for (const auto& threat : opp_threats) {
            for (size_t i = 1; i < threat.path.size() - 1; i++) {
                Position p = threat.path[i];
                const Cell& cell = board[p.y][p.x];
                
                if (!cell.isEmpty() && cell.owner == player && cell.side == "stone") {
                    for (const std::string& orient : {"horizontal", "vertical"}) {
                        auto board_copy = board;
                        board_copy[p.y][p.x].side = "river";
                        board_copy[p.y][p.x].orientation = orient;
                        
                        auto new_result = bfs_distance_to_goals_cached(
                            board_copy, threat.x, threat.y, opp_goals, opponent, rows, cols, score_cols
                        );
                        
                        bool blocks_us = false;
                        for (int my_y = 0; my_y < rows && !blocks_us; my_y++) {
                            for (int my_x = 0; my_x < cols && !blocks_us; my_x++) {
                                const Cell& my_cell = board[my_y][my_x];
                                if (!my_cell.isEmpty() && my_cell.owner == player && my_cell.side == "stone") {
                                    auto my_before = bfs_distance_to_goals_cached(
                                        board, my_x, my_y, my_goals, player, rows, cols, score_cols
                                    );
                                    auto my_after = bfs_distance_to_goals_cached(
                                        board_copy, my_x, my_y, my_goals, player, rows, cols, score_cols
                                    );
                                    if (my_after.distance > my_before.distance + 2) {
                                        blocks_us = true;
                                    }
                                }
                            }
                        }
                        
                        if (new_result.distance > threat.dist + 1 && !blocks_us) {
                            RiverOpportunity def;
                            def.action = "flip";
                            def.from_x = p.x;
                            def.from_y = p.y;
                            def.orientation = orient;
                            def.value = (new_result.distance - threat.dist) * 2000.0;
                            def.defensive = true;
                            defensive_moves.push_back(def);
                        }
                    }
                }
            }
        }
        
        std::sort(defensive_moves.begin(), defensive_moves.end(),
                 [](const RiverOpportunity& a, const RiverOpportunity& b) {
                     return a.value > b.value;
                 });
        
        return defensive_moves;
    }
    
    std::vector<std::unordered_map<std::string, std::string>> generate_all_valid_moves(
        const std::vector<std::vector<Cell>>& board,
        const std::string& current_player,
        int rows, int cols,
        const std::vector<int>& score_cols
    ) {
        std::vector<std::unordered_map<std::string, std::string>> moves;
        
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                const Cell& p = board[y][x];
                if (!p.isEmpty() && p.owner == current_player) {
                    // Compute valid targets (moves and pushes)
                    std::vector<Position> move_targets;
                    std::vector<std::tuple<Position, Position>> push_targets;
                    
                    std::vector<std::pair<int, int>> dirs = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (auto [dx, dy] : dirs) {
                        int tx = x + dx;
                        int ty = y + dy;
                        
                        if (!in_bounds(tx, ty, rows, cols)) continue;
                        if (is_opponent_score_cell(tx, ty, current_player, rows, cols, score_cols)) continue;
                        
                        const Cell& target = board[ty][tx];
                        
                        if (target.isEmpty()) {
                            move_targets.push_back(Position(tx, ty));
                        } else if (target.side == "river") {
                            auto flow = get_river_flow_destinations(board, tx, ty, x, y, current_player, rows, cols, score_cols);
                            for (const auto& d : flow) {
                                move_targets.push_back(d);
                            }
                        } else {
                            if (p.side == "stone") {
                                int px = tx + dx;
                                int py = ty + dy;
                                std::string pushed_player = target.owner;
                                
                                if (in_bounds(px, py, rows, cols) && board[py][px].isEmpty() &&
                                    !is_opponent_score_cell(px, py, p.owner, rows, cols, score_cols) &&
                                    !is_opponent_score_cell(px, py, pushed_player, rows, cols, score_cols)) {
                                    push_targets.push_back({Position(tx, ty), Position(px, py)});
                                }
                            } else {
                                std::string pushed_player = target.owner;
                                auto flow = get_river_flow_destinations(board, tx, ty, x, y, pushed_player, rows, cols, score_cols, true);
                                for (const auto& d : flow) {
                                    if (!is_opponent_score_cell(d.x, d.y, current_player, rows, cols, score_cols)) {
                                        push_targets.push_back({Position(tx, ty), d});
                                    }
                                }
                            }
                        }
                    }
                    
                    // Add move actions
                    for (const auto& target : move_targets) {
                        std::unordered_map<std::string, std::string> move;
                        move["action"] = "move";
                        move["from_x"] = std::to_string(x);
                        move["from_y"] = std::to_string(y);
                        move["to_x"] = std::to_string(target.x);
                        move["to_y"] = std::to_string(target.y);
                        moves.push_back(move);
                    }
                    
                    // Add push actions
                    for (const auto& [to_pos, pushed_pos] : push_targets) {
                        std::unordered_map<std::string, std::string> move;
                        move["action"] = "push";
                        move["from_x"] = std::to_string(x);
                        move["from_y"] = std::to_string(y);
                        move["to_x"] = std::to_string(to_pos.x);
                        move["to_y"] = std::to_string(to_pos.y);
                        move["pushed_x"] = std::to_string(pushed_pos.x);
                        move["pushed_y"] = std::to_string(pushed_pos.y);
                        moves.push_back(move);
                    }
                    
                    // Add flip and rotate actions
                    if (p.side == "stone") {
                        std::unordered_map<std::string, std::string> flip_h, flip_v;
                        flip_h["action"] = "flip";
                        flip_h["from_x"] = std::to_string(x);
                        flip_h["from_y"] = std::to_string(y);
                        flip_h["orientation"] = "horizontal";
                        moves.push_back(flip_h);
                        
                        flip_v["action"] = "flip";
                        flip_v["from_x"] = std::to_string(x);
                        flip_v["from_y"] = std::to_string(y);
                        flip_v["orientation"] = "vertical";
                        moves.push_back(flip_v);
                    } else {
                        std::unordered_map<std::string, std::string> flip, rotate;
                        flip["action"] = "flip";
                        flip["from_x"] = std::to_string(x);
                        flip["from_y"] = std::to_string(y);
                        moves.push_back(flip);
                        
                        rotate["action"] = "rotate";
                        rotate["from_x"] = std::to_string(x);
                        rotate["from_y"] = std::to_string(y);
                        moves.push_back(rotate);
                    }
                }
            }
        }
        
        return moves;
    }
    
double evaluate_board(
    const std::vector<std::vector<Cell>>& board,
    int rows, int cols,
    const std::vector<int>& score_cols
) {
    double score = 0.0;
    const double WIN_SCORE = 1e9;
    const double LOSE_SCORE = -1e8;
    const double INF = std::numeric_limits<double>::infinity();
    
    auto my_goals = get_my_goal_cells(rows, cols, score_cols);
    auto opp_goals = get_opponent_goal_cells(rows, cols, score_cols);
    int my_score_row = my_goals[0].y;
    int opp_score_row = opp_goals[0].y;
    
    // ========== FEATURE 1: SCORING STONES ==========
    int my_scoring_stones = 0;
    int opp_scoring_stones = 0;
    
    for (int x : score_cols) {
        const Cell& cell_my = board[my_score_row][x];
        if (!cell_my.isEmpty() && cell_my.owner == player && cell_my.side == "stone") {
            my_scoring_stones++;
        }
        const Cell& cell_opp = board[opp_score_row][x];
        if (!cell_opp.isEmpty() && cell_opp.owner == opponent && cell_opp.side == "stone") {
            opp_scoring_stones++;
        }
    }
    
    // Winning conditions
    if (my_scoring_stones >= get_win_count(rows)) return WIN_SCORE;
    if (opp_scoring_stones >= get_win_count(rows)) return LOSE_SCORE;
    
    // Apply learned weights for scoring stones
    score += my_scoring_stones * LearnedWeights::MY_SCORING_STONES * 1e7;
    score -= opp_scoring_stones * LearnedWeights::OPP_SCORING_STONES * 1e7;
    // score += (my_scoring_stones - opp_scoring_stones) * LearnedWeights::SCORING_STONE_DIFF * 1e7;
    
    // ========== COLLECT STONE POSITIONS AND CALCULATE FEATURES ==========
    struct StoneInfo {
        int x, y;
        double dist;
        std::vector<Position> path;
    };
    std::vector<StoneInfo> my_stone_distances;
    std::vector<StoneInfo> opp_stone_distances;
    
    // Distance bucket counters
    int my_stones_within_2 = 0;
    int my_stones_within_4 = 0;
    int my_stones_within_6 = 0;
    // int my_stones_within_8 = 0;
    int opp_stones_within_2 = 0;
    int opp_stones_within_4 = 0;
    int opp_stones_within_6 = 0;
    // int opp_stones_within_8 = 0;
    
    // Row-based distance counters
    int my_stones_within_1_rows = 0;
    // int my_stones_within_8_rows = 0;
    // int my_stones_within_10_rows = 0;
    
    // River features
    int my_river_count = 0;
    int opp_river_count = 0;
    int my_rivers_near_goal = 0;
    int rivers_near_us = 0;
    int opp_rivers_near_goal = 0;
    int my_rivers_horizontal = 0;
    int my_rivers_vertical = 0;
    
    // Control features
    // int my_board_control = 0;
    // int opp_board_control = 0;
    // int center_control = 0;
    // int center_x = cols / 2;
    // int center_y = rows / 2;
    
    // Mobility features
    int my_mobility = 0;
    int opp_mobility = 0;
    
    // Formation features
    int my_horizontal_connections = 0;
    int my_vertical_connections = 0;
    
    // Blocking features
    int opp_stones_blocked = 0;
    int my_blocking_pieces = 0;
    
    // Stone positions for clustering
    std::vector<std::pair<int, int>> my_stones;
    std::vector<std::pair<int, int>> opp_stones;
    
    // Proximity scores
    double my_proximity_score = 0.0;
    double opp_proximity_score = 0.0;
    
    // Min distances
    double my_min_distance = 999.0;
    double opp_min_distance = 999.0;
    
    // Average distances
    std::vector<double> my_distances;
    std::vector<double> opp_distances;
    
    // Tempo/advancement
    int my_advancement = 0;
    int opp_advancement = 0;
    
    // ========== MAIN BOARD SCAN ==========
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            const Cell& cell = board[y][x];
            
            // === MY STONES ===
            if (cell.side == "stone" && cell.owner == player) {
                my_stones.push_back({x, y});
                // my_board_control++;
                
                // Center control
                // if (std::abs(x - center_x) <= 2 && std::abs(y - center_y) <= 2) {
                //     center_control++;
                // }
                
                // Row-based distance
                int row_dist = std::abs(y - my_score_row);
                if (row_dist <= 1) my_stones_within_1_rows++;
                // if (row_dist <= 8) my_stones_within_8_rows++;
                // if (row_dist <= 10) my_stones_within_10_rows++;
                
                // Advancement (tempo)
                my_advancement += rows - row_dist;
                
                // BFS distance to goals
                auto result = bfs_distance_to_goals_cached(
                    board, x, y, my_goals, player, rows, cols, score_cols, true
                );
                
                if (result.distance < INF) {
                    my_stone_distances.push_back({x, y, result.distance, result.path});
                    my_distances.push_back(result.distance);
                    
                    // Min distance
                    if (result.distance < my_min_distance) {
                        my_min_distance = result.distance;
                    }
                    
                    // Distance buckets
                    if (result.distance <= 2) my_stones_within_2++;
                    if (result.distance <= 4) my_stones_within_4++;
                    if (result.distance <= 6) my_stones_within_6++;
                    // if (result.distance <= 8) my_stones_within_8++;
                    
                    // Proximity score
                    // if (result.distance < 15) {
                        // double effective_dist = std::min(result.distance, 15.0);
                    my_proximity_score += std::pow(25.0 - std::min(result.distance, 25.0), 3) * 1000.0;
                    // }
                    if (result.distance <= 2) {
                            my_proximity_score += 2e6 + my_scoring_stones * 5e5;
                        } else if (result.distance <= 4) {
                            my_proximity_score += 1e5 + my_scoring_stones * 5e4;
                        }
                        
                        if (result.path.size() > static_cast<size_t>(result.distance + 1)) {
                            my_proximity_score += 200000.0;
                        }
                }
                
                // Mobility
                std::vector<std::pair<int, int>> dirs = {{1,0}, {-1,0}, {0,1}, {0,-1}};
                for (auto [dx, dy] : dirs) {
                    int nx = x + dx, ny = y + dy;
                    if (in_bounds(nx, ny, rows, cols) && board[ny][nx].isEmpty()) {
                        my_mobility++;
                    }
                }
                
                // Formation: horizontal connections
                if ((x+1 < cols && !board[y][x+1].isEmpty() && 
                     board[y][x+1].owner == player && board[y][x+1].side == "stone") ||
                    (x-1 >= 0 && !board[y][x-1].isEmpty() && 
                     board[y][x-1].owner == player && board[y][x-1].side == "stone")) {
                    my_horizontal_connections++;
                }
                
                // Formation: vertical connections
                if ((y+1 < rows && !board[y+1][x].isEmpty() && 
                     board[y+1][x].owner == player && board[y+1][x].side == "stone") ||
                    (y-1 >= 0 && !board[y-1][x].isEmpty() && 
                     board[y-1][x].owner == player && board[y-1][x].side == "stone")) {
                    my_vertical_connections++;
                }
            }
            // === OPPONENT STONES ===
            else if (cell.side == "stone" && cell.owner == opponent) {
                opp_stones.push_back({x, y});
                // opp_board_control++;
                
                // Center control
                // if (std::abs(x - center_x) <= 2 && std::abs(y - center_y) <= 2) {
                //     center_control--;
                // }
                
                // Advancement (tempo)
                int row_dist = std::abs(y - opp_score_row);
                opp_advancement += rows - row_dist;
                
                // BFS distance
                auto result = bfs_distance_to_goals_cached(
                    board, x, y, opp_goals, opponent, rows, cols, score_cols, true
                );
                
                if (result.distance < INF) {
                    opp_stone_distances.push_back({x, y, result.distance, result.path});
                    opp_distances.push_back(result.distance);
                    
                    // Min distance
                    if (result.distance < opp_min_distance) {
                        opp_min_distance = result.distance;
                    }
                    
                    // Distance buckets
                    if (result.distance <= 2) opp_stones_within_2++;
                    if (result.distance <= 4) opp_stones_within_4++;
                    if (result.distance <= 6) opp_stones_within_6++;
                    // if (result.distance <= 8) opp_stones_within_8++;
                    
                    // Proximity score
                    // if (result.distance < 15) {
                    //     double effective_dist = std::min(result.distance, 15.0);
                    opp_proximity_score += std::pow(20.0 - std::min(result.distance, 20.0),2) * 500.0;
                    // }
                    if (result.distance <= 2) {
                            opp_proximity_score -= 80000.0;
                        } else if (result.distance <= 4) {
                            opp_proximity_score -= 30000.0;
                        }
                }
                
                // Check if blocked by my pieces
                std::vector<std::pair<int, int>> dirs = {{1,0}, {-1,0}, {0,1}, {0,-1}};
                bool is_blocked = false;
                for (auto [dx, dy] : dirs) {
                    int nx = x + dx, ny = y + dy;
                    if (in_bounds(nx, ny, rows, cols)) {
                        const Cell& neighbor = board[ny][nx];
                        if (!neighbor.isEmpty() && neighbor.owner == player) {
                            is_blocked = true;
                        }
                        if (neighbor.isEmpty()) {
                            opp_mobility++;
                        }
                    }
                }
                if (is_blocked) {
                    opp_stones_blocked++;
                }
            }
            // === RIVERS ===
            else if (cell.side == "river") {
                if (cell.owner == player) {
                    my_river_count++;
                    // my_board_control++;
                    
                    if (std::abs(y - my_score_row) <= 3) {
                        my_rivers_near_goal++;
                    }
                    
                    if (cell.orientation == "horizontal") {
                        my_rivers_horizontal++;
                    } else if (cell.orientation == "vertical") {
                        my_rivers_vertical++;
                    }

                    for (const auto& stone : my_stone_distances) {
                        if (std::abs(x - stone.x) <= 2 && std::abs(y - stone.y) <= 2) {
                            rivers_near_us ++;
                        }
                    }
                    
                    for (const auto& opp_stone : opp_stone_distances) {
                        bool in_path = false;
                        for (const auto& p : opp_stone.path) {
                            if (p.x == x && p.y == y) {
                                in_path = true;
                                break;
                            }
                        }
                        if (in_path) {
                            rivers_near_us ++;
                        }
                    }
                    
                    if (std::abs(y - my_score_row) <= 3) {
                        rivers_near_us ++;
                    }

                } else if (cell.owner == opponent) {
                    opp_river_count++;
                    // opp_board_control++;
                    
                    if (std::abs(y - opp_score_row) <= 3) {
                        opp_rivers_near_goal++;
                    }
                    for (const auto& stone : my_stone_distances) {
                        if (std::abs(x - stone.x) <= 2 && std::abs(y - stone.y) <= 2) {
                            rivers_near_us++;
                        }
                    }
                }
            }
        }
    }
    
    // ========== CALCULATE DERIVED FEATURES ==========
    
    // Average distances
    // double my_avg_distance = my_distances.empty() ? 15.0 : 
    //     std::accumulate(my_distances.begin(), my_distances.end(), 0.0) / my_distances.size();
    // double opp_avg_distance = opp_distances.empty() ? 15.0 :
    //     std::accumulate(opp_distances.begin(), opp_distances.end(), 0.0) / opp_distances.size();
    
    // Clustering (average distance between stones)
    // double my_clustering = 0.0;
    // if (my_stones.size() >= 2) {
    //     double total_dist = 0;
    //     int count = 0;
    //     for (size_t i = 0; i < std::min(my_stones.size(), size_t(10)); i++) {
    //         for (size_t j = i + 1; j < std::min(my_stones.size(), size_t(10)); j++) {
    //             total_dist += std::abs(my_stones[i].first - my_stones[j].first) + 
    //                           std::abs(my_stones[i].second - my_stones[j].second);
    //             count++;
    //         }
    //     }
    //     my_clustering = count > 0 ? total_dist / count : 10.0;
    // } else {
    //     my_clustering = 10.0; // default when few stones
    // }
    
    // double opp_clustering = 0.0;
    // if (opp_stones.size() >= 2) {
    //     double total_dist = 0;
    //     int count = 0;
    //     for (size_t i = 0; i < std::min(opp_stones.size(), size_t(10)); i++) {
    //         for (size_t j = i + 1; j < std::min(opp_stones.size(), size_t(10)); j++) {
    //             total_dist += std::abs(opp_stones[i].first - opp_stones[j].first) + 
    //                           std::abs(opp_stones[i].second - opp_stones[j].second);
    //             count++;
    //         }
    //     }
    //     opp_clustering = count > 0 ? total_dist / count : 10.0;
    // } else {
    //     opp_clustering = 10.0;
    // }
    
    // Path diversity (clear paths to goal)
    int my_clear_paths_to_goal = 0;
    for (int x : score_cols) {
        bool has_obstacle = false;
        for (int y = 0; y < rows; y++) {
            if (!board[y][x].isEmpty() && board[y][x].owner == opponent) {
                has_obstacle = true;
                break;
            }
        }
        if (!has_obstacle) {
            my_clear_paths_to_goal++;
        }
    }
    
    // Blocking pieces (tactical)
    int direction = (player == "circle") ? 1 : -1;
    for (int x : score_cols) {
        int y = opp_score_row;
        while (y != my_score_row && y >= 0 && y < rows) {
            if (!board[y][x].isEmpty() && board[y][x].owner == player) {
                my_blocking_pieces++;
                break;
            }
            y += direction;
        }
    }
    
    // ========== APPLY LEARNED WEIGHTS ==========
    
    // Distance Features
    score += my_stones_within_2 * LearnedWeights::MY_STONES_WITHIN_2 * 1e6;
    score += my_stones_within_4 * LearnedWeights::MY_STONES_WITHIN_4 * 1e6;
    score += my_stones_within_6 * LearnedWeights::MY_STONES_WITHIN_6 * 1e6;
    // score += my_stones_within_8 * LearnedWeights::MY_STONES_WITHIN_8 * 1e6;
    
    score -= opp_stones_within_2 * LearnedWeights::OPP_STONES_WITHIN_2 * 1e6;
    score -= opp_stones_within_4 * LearnedWeights::OPP_STONES_WITHIN_4 * 1e6;
    score -= opp_stones_within_6 * LearnedWeights::OPP_STONES_WITHIN_6 * 1e6;
    // score -= opp_stones_within_8 * LearnedWeights::OPP_STONES_WITHIN_8 * 1e6;
    
    // Proximity scores
    score += my_proximity_score * LearnedWeights::MY_PROXIMITY_SCORE * 1e4;
    score -= opp_proximity_score * LearnedWeights::OPP_PROXIMITY_SCORE * 1e4;
    
    // Average distances (inverse - lower is better)
    // score -= my_avg_distance * LearnedWeights::MY_AVG_DISTANCE * 1e5;
    // score += opp_avg_distance * LearnedWeights::OPP_AVG_DISTANCE * 1e5;
    
    // Min distances
    score -= my_min_distance * LearnedWeights::MY_MIN_DISTANCE * 1e5;
    score += opp_min_distance * LearnedWeights::OPP_MIN_DISTANCE * 1e5;
    
    // Row-based distances
    score += my_stones_within_1_rows * LearnedWeights::MY_STONES_WITHIN_1_ROWS * 1e5;
    // score += my_stones_within_8_rows * LearnedWeights::MY_STONES_WITHIN_8_ROWS * 1e5;
    // score += my_stones_within_10_rows * LearnedWeights::MY_STONES_WITHIN_10_ROWS * 1e5;
    
    // River features
    score += my_river_count * LearnedWeights::MY_RIVER_COUNT * 1e5;
    score += my_rivers_near_goal * LearnedWeights::MY_RIVERS_NEAR_GOAL * 1e5;
    score += rivers_near_us * LearnedWeights::RIVERS_NEAR_US * 1e5;
    score += my_rivers_horizontal * LearnedWeights::MY_RIVERS_HORIZONTAL * 1e5;
    score += my_rivers_vertical * LearnedWeights::MY_RIVERS_VERTICAL * 1e5;
    
    score -= opp_river_count * LearnedWeights::OPP_RIVER_COUNT * 1e5;
    score -= opp_rivers_near_goal * LearnedWeights::OPP_RIVERS_NEAR_GOAL * 1e5;
    
    // Control features
    // score += my_board_control * LearnedWeights::MY_BOARD_CONTROL * 1e4;
    // score -= opp_board_control * LearnedWeights::OPP_BOARD_CONTROL * 1e4;
    // score += (my_board_control - opp_board_control) * LearnedWeights::CONTROL_DIFF * 1e4;
    // score += center_control * LearnedWeights::CENTER_CONTROL * 1e4;
    
    // Mobility features
    // score += my_mobility * LearnedWeights::MY_MOBILITY * 1e3;
    // score -= opp_mobility * LearnedWeights::OPP_MOBILITY * 1e3;
    // score += (my_mobility - opp_mobility) * LearnedWeights::MOBILITY_DIFF * 1e3;
    
    // Formation features
    // score += my_horizontal_connections * LearnedWeights::MY_HORIZONTAL_CONNECTIONS * 1e5;
    // score += my_vertical_connections * LearnedWeights::MY_VERTICAL_CONNECTIONS * 1e5;
    
    // Clustering features
    // score -= my_clustering * LearnedWeights::MY_CLUSTERING * 1e4;
    // score += opp_clustering * LearnedWeights::OPP_CLUSTERING * 1e4;
    
    // Stone count features
    // score += my_stones.size() * LearnedWeights::MY_STONE_COUNT * 1e5;
    // score -= opp_stones.size() * LearnedWeights::OPP_STONE_COUNT * 1e5;
    // score += (static_cast<int>(my_stones.size()) - static_cast<int>(opp_stones.size())) * LearnedWeights::STONE_COUNT_DIFF * 1e5;
    
    // Blocking features
    score += opp_stones_blocked * LearnedWeights::OPP_STONES_BLOCKED * 1e5;
    score += my_blocking_pieces * LearnedWeights::MY_BLOCKING_PIECES * 1e5;
    
    // Tempo features
    score += my_advancement * LearnedWeights::MY_ADVANCEMENT * 1e3;
    score -= opp_advancement * LearnedWeights::OPP_ADVANCEMENT * 1e3;
    score += (my_advancement - opp_advancement) * LearnedWeights::ADVANCEMENT_DIFF * 1e3;
    
    // Path diversity
    score += my_clear_paths_to_goal * LearnedWeights::MY_CLEAR_PATHS_TO_GOAL * 1e6;
    
    return score;
}
    std::vector<std::vector<Cell>> apply_move(
        const std::vector<std::vector<Cell>>& board,
        const std::unordered_map<std::string, std::string>& move,
        const std::string& current_player,
        int rows, int cols,
        const std::vector<int>& score_cols
    ) {
        auto new_board = board;
        std::string action = move.at("action");
        int from_x = std::stoi(move.at("from_x"));
        int from_y = std::stoi(move.at("from_y"));
        
        if (action == "move") {
            int to_x = std::stoi(move.at("to_x"));
            int to_y = std::stoi(move.at("to_y"));
            new_board[to_y][to_x] = new_board[from_y][from_x];
            new_board[from_y][from_x] = Cell();
        } else if (action == "push") {
            int to_x = std::stoi(move.at("to_x"));
            int to_y = std::stoi(move.at("to_y"));
            int pushed_x = std::stoi(move.at("pushed_x"));
            int pushed_y = std::stoi(move.at("pushed_y"));
            
            new_board[pushed_y][pushed_x] = new_board[to_y][to_x];
            new_board[to_y][to_x] = new_board[from_y][from_x];
            new_board[from_y][from_x] = Cell();
            
            if (new_board[to_y][to_x].side == "river") {
                new_board[to_y][to_x].side = "stone";
                new_board[to_y][to_x].orientation = "";
                GLOBAL_BFS_CACHE.clear();
            }
        } else if (action == "flip") {
            if (new_board[from_y][from_x].side == "stone") {
                new_board[from_y][from_x].side = "river";
                new_board[from_y][from_x].orientation = move.at("orientation");
            } else {
                new_board[from_y][from_x].side = "stone";
                new_board[from_y][from_x].orientation = "";
            }
            GLOBAL_BFS_CACHE.clear();
        } else if (action == "rotate") {
            if (new_board[from_y][from_x].orientation == "horizontal") {
                new_board[from_y][from_x].orientation = "vertical";
            } else {
                new_board[from_y][from_x].orientation = "horizontal";
            }
            GLOBAL_BFS_CACHE.clear();
        }
        
        return new_board;
    }
    
    double minimax(
        const std::vector<std::vector<Cell>>& board,
        int depth,
        double alpha,
        double beta,
        bool is_maximizing,
        int rows, int cols,
        const std::vector<int>& score_cols
    ) {
        std::string winner = check_win(board, rows, cols, score_cols);
        if (depth == 0 || !winner.empty()) {
            return evaluate_board(board, rows, cols, score_cols);
        }
        
        std::string current_player = is_maximizing ? player : opponent;
        auto moves = generate_all_valid_moves(board, current_player, rows, cols, score_cols);
        
        if (moves.empty()) {
            return evaluate_board(board, rows, cols, score_cols);
        }
        
        if (is_maximizing) {
            double max_eval = -std::numeric_limits<double>::infinity();
            for (const auto& move : moves) {
                auto new_board = apply_move(board, move, current_player, rows, cols, score_cols);
                double eval_score = minimax(new_board, depth - 1, alpha, beta, false, rows, cols, score_cols);
                max_eval = std::max(max_eval, eval_score);
                alpha = std::max(alpha, eval_score);
                if (beta <= alpha) break;
            }
            return max_eval;
        } else {
            double min_eval = std::numeric_limits<double>::infinity();
            for (const auto& move : moves) {
                auto new_board = apply_move(board, move, current_player, rows, cols, score_cols);
                double eval_score = minimax(new_board, depth - 1, alpha, beta, true, rows, cols, score_cols);
                min_eval = std::min(min_eval, eval_score);
                beta = std::min(beta, eval_score);
                if (beta <= alpha) break;
            }
            return min_eval;
        }
    }
    
    Move choose(
        const std::vector<std::vector<std::unordered_map<std::string, std::string>>>& py_board,
        int rows, int cols,
        const std::vector<int>& score_cols,
        double current_player_time,
        double opponent_time,
        bool avoid_repeat
    ) {
        GLOBAL_BFS_CACHE.clear();
        // Convert Python board to C++ board
        std::vector<std::vector<Cell>> board(rows, std::vector<Cell>(cols));
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                const auto& cell_dict = py_board[y][x];
                if (!cell_dict.empty()) {
                    Cell cell;
                    if (cell_dict.count("owner")) cell.owner = cell_dict.at("owner");
                    if (cell_dict.count("side")) cell.side = cell_dict.at("side");
                    if (cell_dict.count("orientation")) cell.orientation = cell_dict.at("orientation");
                    board[y][x] = cell;
                }
            }
        }
        
        // Opening book
        std::vector<std::unordered_map<std::string, std::string>> opening_book;
        if (player == "square") {
            opening_book = {
                // {{"action", "flip"}, {"from_x", "3"}, {"from_y", "3"}, {"orientation", "horizontal"}},
                // {{"action", "move"}, {"from_x", "3"}, {"from_y", "4"}, {"to_x", "0"}, {"to_y", "3"}},
                // {{"action", "flip"}, {"from_x", "0"}, {"from_y", "3"}, {"orientation", "vertical"}},
                // {{"action", "move"}, {"from_x", "4"}, {"from_y", "3"}, {"to_x", "0"}, {"to_y", "7"}}
            };
        } else {
            opening_book = {
                // {{"action", "flip"}, {"from_x", "3"}, {"from_y", "9"}, {"orientation", "horizontal"}},
                // {{"action", "move"}, {"from_x", "3"}, {"from_y", "8"}, {"to_x", "0"}, {"to_y", "9"}},
                // {{"action", "flip"}, {"from_x", "0"}, {"from_y", "9"}, {"orientation", "vertical"}},
                // {{"action", "move"}, {"from_x", "4"}, {"from_y", "9"}, {"to_x", "0"}, {"to_y", "5"}}
            };
        }
        
        // Use opening book for first few moves
        if (moves < static_cast<int>(opening_book.size())) {
            auto candidate = opening_book[moves];
            auto test_board = apply_move(board, candidate, player, rows, cols, score_cols);
            
            // Simple validation - if move changes board, it's valid
            bool valid = false;
            for (int y = 0; y < rows && !valid; y++) {
                for (int x = 0; x < cols && !valid; x++) {
                    if (!(board[y][x].owner == test_board[y][x].owner &&
                          board[y][x].side == test_board[y][x].side &&
                          board[y][x].orientation == test_board[y][x].orientation)) {
                        valid = true;
                    }
                }
            }
            
            if (valid) {
                moves++;
                
                // Track in last_moves
                last_moves.push_back(candidate);
                if (last_moves.size() > 3) {
                    last_moves.erase(last_moves.begin());
                }
                
                Move result;
                result.action = candidate.at("action");
                result.from_pos = {std::stoi(candidate.at("from_x")), std::stoi(candidate.at("from_y"))};
                if (candidate.count("to_x")) {
                    result.to_pos = {std::stoi(candidate.at("to_x")), std::stoi(candidate.at("to_y"))};
                }
                if (candidate.count("pushed_x")) {
                    result.pushed_to = {std::stoi(candidate.at("pushed_x")), std::stoi(candidate.at("pushed_y"))};
                }
                if (candidate.count("orientation")) {
                    result.orientation = candidate.at("orientation");
                }
                return result;
            }
        }
        
        // Generate and evaluate moves
        auto valid_moves = generate_all_valid_moves(board, player, rows, cols, score_cols);
        if (valid_moves.empty()) {
            return Move();
        }
        
        auto river_opportunities = find_river_creation_opportunities(board, rows, cols, score_cols);
        auto defensive_rivers = find_defensive_river_placements(board, rows, cols, score_cols);
        
        double best_score = -std::numeric_limits<double>::infinity();
        std::vector<std::unordered_map<std::string, std::string>> best_moves;
        double alpha = -std::numeric_limits<double>::infinity();
        double beta = std::numeric_limits<double>::infinity();
        
        auto my_goals = get_my_goal_cells(rows, cols, score_cols);
        
        for (const auto& move : valid_moves) {
            auto new_board = apply_move(board, move, player, rows, cols, score_cols);
            double score = minimax(new_board, MAX_DEPTH - 1, alpha, beta, false, rows, cols, score_cols);
            
            // Count current scoring stones for urgency multiplier
            int my_scoring_count = 0;
            for (int x : score_cols) {
                int my_score_row = (player == "circle") ? top_score_row() : bottom_score_row(rows);
                const Cell& cell = new_board[my_score_row][x];
                if (!cell.isEmpty() && cell.owner == player && cell.side == "stone") {
                    my_scoring_count++;
                }
            }
            // Urgency multiplier: 3 stones = push HARD for 4th!
            double urgency =2.0+ my_scoring_count;
            
            
            std::string action = move.at("action");
            int from_x = std::stoi(move.at("from_x"));
            int from_y = std::stoi(move.at("from_y"));
            
            // RIVER PUSH - very valuable for advancing multiple spaces
            if (action == "push" && move.count("pushed_x")) {
                int to_x = std::stoi(move.at("to_x"));
                int to_y = std::stoi(move.at("to_y"));
                int pushed_x = std::stoi(move.at("pushed_x"));
                int pushed_y = std::stoi(move.at("pushed_y"));

                auto dist_before = bfs_distance_to_goals_cached(board, from_x, from_y, my_goals, player, rows, cols, score_cols);
                auto dist_after = bfs_distance_to_goals_cached(new_board, to_x, to_y, my_goals, player, rows, cols, score_cols);
                double improvement = dist_before.distance - dist_after.distance;
                
                int push_dist = std::abs(to_x - pushed_x) + std::abs(to_y - pushed_y);
                if (push_dist > 1) {  // River push
                    score += push_dist * 1000.0;
                    
                    // Extra bonus if pushed piece gets close to goal
                    if (is_my_score_cell(pushed_x, pushed_y, player, rows, cols, score_cols)) {
                        score += 5e7;  // Pushed directly into goal!
                    } else {
                        const Cell& piece = board[from_y][from_x];
                        if (!piece.isEmpty() && piece.side == "stone") {
                            auto dist_after = bfs_distance_to_goals_cached(new_board, pushed_x, pushed_y, my_goals, player, rows, cols, score_cols);
                            if (dist_after.distance < 3) {
                                score += 80000.0;
                            }
                        }
                    }
                } else {
                    score += dist_before.distance ==0? 100: std::pow(20.0 - std::min(dist_after.distance, 20.0), 2) * 500.0;
                }
            }
            // RIVER MOVEMENT - bonus for using rivers to advance
            else if (action == "move" && move.count("to_x")) {
                int to_x = std::stoi(move.at("to_x"));
                int to_y = std::stoi(move.at("to_y"));
                int move_dist = std::abs(from_x - to_x) + std::abs(from_y - to_y);
                
                const Cell& piece = board[from_y][from_x];
                if (!piece.isEmpty() && piece.side == "stone") {
                    // Calculate BFS distance improvement
                    auto dist_before = bfs_distance_to_goals_cached(board, from_x, from_y, my_goals, player, rows, cols, score_cols);
                    auto dist_after = bfs_distance_to_goals_cached(new_board, to_x, to_y, my_goals, player, rows, cols, score_cols);
                    double improvement = dist_before.distance - dist_after.distance;
                    
                    if (move_dist > 1) {  // Used river to move
                        // score += move_dist * 200.0;  // Reward river usage
                        score += std::pow(improvement, 3) * 100000.0;  // Reward progress toward goal
                        
                        
                    } else {  // Regular 1-step move
                        score += dist_before.distance ==0? 100: std::pow(20.0 - std::min(dist_after.distance, 20.0), 3) * 500.0;
                        // if (dist_after.distance <= 2) {
                        //     score += 50000.0;
                        // }
                    }
                    // Extra bonus if move gets us into scoring position
                        if(dist_after.distance == 0 && dist_before.distance > 0) {
                            score += 5e8 * urgency;
                        }
                        else if(dist_after.distance == 1 && dist_before.distance > 1) {
                            score += 5e7 * urgency;
                        }
                        else if (dist_after.distance == 2 && dist_before.distance > 2) {
                            score += 1e7;
                        }
                        else if (dist_after.distance == 3 && dist_before.distance > 3) {
                            score += 1e6;
                        }
                        else if (dist_after.distance == 4 && dist_before.distance > 4) {
                            score += 1e5;
                        }
                        else if (dist_after.distance == 5 && dist_before.distance > 5) {
                            score += 1e4;
                        }
                        // else if (dist_after.distance == 6 && dist_before.distance > 6) {
                        //     score += 1e3;
                        // }
                        // else if (dist_after.distance == 7 && dist_before.distance > 7) {
                        //     score += 1e2;
                        // }
                        // else if (dist_after.distance == 8 && dist_before.distance > 8) {
                        //     score += 1e1;
                        // }
                }
            }
            // FLIP TO RIVER - strategic value
            else if (action == "flip" && move.count("orientation")) {
                std::string orientation = move.at("orientation");
                
                // Check if this flip is in our strategic opportunities
                for (size_t i = 0; i < std::min(size_t(3), river_opportunities.size()); i++) {
                    const auto& opp = river_opportunities[i];
                    if (opp.from_x == from_x && opp.from_y == from_y && opp.orientation == orientation) {
                        score += opp.value;
                        break;
                    }
                }
                
                // Check if this flip is defensive
                for (size_t i = 0; i < std::min(size_t(4), defensive_rivers.size()); i++) {
                    const auto& def = defensive_rivers[i];
                    if (def.from_x == from_x && def.from_y == from_y && def.orientation == orientation) {
                        score += def.value;
                        break;
                    }
                }
                
                // General bonus for creating rivers in forward positions
                const Cell& piece = board[from_y][from_x];
                if (!piece.isEmpty()) {
                    int my_score_row = my_goals[0].y;
                    if (std::abs(from_y - my_score_row) <= 4) {  // Near goal area
                        score += 3000.0;
                    }
                }
            }
            // ROTATE RIVER - adjust flow direction
            else if (action == "rotate") {
                score += 1000.0;
            }
            
            // Track best moves
            if (score > best_score) {
                best_score = score;
                best_moves = {move};
            } else if (std::abs(score - best_score) < 100.0) {  // Similar scores
                best_moves.push_back(move);
            }
            
            alpha = std::max(alpha, score);
        }
        
        // Choose from best moves
        std::unordered_map<std::string, std::string> chosen_move;
        if (!best_moves.empty()) {
            // Prefer river-utilizing moves if scores are similar
            std::vector<std::unordered_map<std::string, std::string>> river_moves;
            for (const auto& m : best_moves) {
                if ((m.at("action") == "push" || m.at("action") == "move") && 
                    m.count("to_x") && m.count("from_x")) {
                    int fx = std::stoi(m.at("from_x"));
                    int fy = std::stoi(m.at("from_y"));
                    int tx = std::stoi(m.at("to_x"));
                    int ty = std::stoi(m.at("to_y"));
                    if (std::abs(fx - tx) + std::abs(fy - ty) > 1) {
                        river_moves.push_back(m);
                    }
                }
            }
            
            if (!river_moves.empty()) {
                std::uniform_int_distribution<> dist(0, river_moves.size() - 1);
                chosen_move = river_moves[dist(rng)];
            } else {
                std::uniform_int_distribution<> dist(0, best_moves.size() - 1);
                chosen_move = best_moves[dist(rng)];
            }
        } else {
            std::uniform_int_distribution<> dist(0, valid_moves.size() - 1);
            chosen_move = valid_moves[dist(rng)];
        }
        
        // ========== REPETITION AVOIDANCE ==========
        // Add chosen move to last_moves
        last_moves.push_back(chosen_move);
        if (last_moves.size() > 6) {
            last_moves.erase(last_moves.begin());
        }
        
        // Count how many times this move appears in last_moves
        int move_count = 0;
        for (const auto& past_move : last_moves) {
            // Compare moves (same action and positions)
            bool same = (past_move.at("action") == chosen_move.at("action") &&
                        past_move.at("from_x") == chosen_move.at("from_x") &&
                        past_move.at("from_y") == chosen_move.at("from_y"));
            
            if (same && chosen_move.count("to_x")) {
                same = same && (past_move.count("to_x") && 
                               past_move.at("to_x") == chosen_move.at("to_x") &&
                               past_move.at("to_y") == chosen_move.at("to_y"));
            }
            
            if (same && chosen_move.count("orientation")) {
                same = same && (past_move.count("orientation") &&
                               past_move.at("orientation") == chosen_move.at("orientation"));
            }
            
            if (same) move_count++;
        }
        
        // If move repeated too many times, choose different move
        if (move_count > repetition_limit) {
            // Find alternative moves that haven't been repeated
            std::vector<std::unordered_map<std::string, std::string>> alt_moves;
            
            for (const auto& m : valid_moves) {
                // Count repetitions for this move
                int m_count = 0;
                for (const auto& past_move : last_moves) {
                    bool same = (past_move.at("action") == m.at("action") &&
                                past_move.at("from_x") == m.at("from_x") &&
                                past_move.at("from_y") == m.at("from_y"));
                    
                    if (same && m.count("to_x")) {
                        same = same && (past_move.count("to_x") && 
                                       past_move.at("to_x") == m.at("to_x") &&
                                       past_move.at("to_y") == m.at("to_y"));
                    }
                    
                    if (same && m.count("orientation")) {
                        same = same && (past_move.count("orientation") &&
                                       past_move.at("orientation") == m.at("orientation"));
                    }
                    
                    if (same) m_count++;
                }
                
                // Add to alternatives if not repeated too much
                if (m_count <= repetition_limit) {
                    alt_moves.push_back(m);
                }
            }
            
            // Choose from alternatives if available
            if (!alt_moves.empty()) {
                std::uniform_int_distribution<> dist(0, alt_moves.size() - 1);
                chosen_move = alt_moves[dist(rng)];
                
                // Update last_moves with new choice
                last_moves.pop_back();  // Remove the repeated move
                last_moves.push_back(chosen_move);
            }
            // If all moves are repeated, stick with chosen_move (can't avoid repetition)
        }
        
        // Convert to Move struct
        Move result;
        result.action = chosen_move.at("action");
        result.from_pos = {std::stoi(chosen_move.at("from_x")), std::stoi(chosen_move.at("from_y"))};
        if (chosen_move.count("to_x")) {
            result.to_pos = {std::stoi(chosen_move.at("to_x")), std::stoi(chosen_move.at("to_y"))};
        }
        if (chosen_move.count("pushed_x")) {
            result.pushed_to = {std::stoi(chosen_move.at("pushed_x")), std::stoi(chosen_move.at("pushed_y"))};
        }
        if (chosen_move.count("orientation")) {
            result.orientation = chosen_move.at("orientation");
        }
        
        return result;
    }
};

// ==================== PYBIND11 BINDINGS ====================


PYBIND11_MODULE(student_agent_module, m) {
    m.doc() = "Student Agent C++ Module for Rivers and Stones";
    
    py::class_<Move>(m, "Move")
        .def(py::init<>())
        .def_readwrite("action", &Move::action)
        .def_readwrite("from_pos", &Move::from_pos)
        .def_readwrite("to_pos", &Move::to_pos)
        .def_readwrite("pushed_to", &Move::pushed_to)
        .def_readwrite("orientation", &Move::orientation);
    
    py::class_<StudentAgent>(m, "StudentAgent")
        .def(py::init<const std::string&>())
        .def("choose", &StudentAgent::choose,
             py::arg("board"),
             py::arg("rows"),
             py::arg("cols"),
             py::arg("score_cols"),
             py::arg("current_player_time"),
             py::arg("opponent_time"),
             py::arg("avoid_repeat") = false);
}