//
// Created by leon on 7/28/19.
//

#ifndef CHESSENGINE_ENGINE_H
#define CHESSENGINE_ENGINE_H


#include <chess/move.h>
#include <chess/board.h>
#include <unordered_map>
#include <chrono>
#include <chess/game.h>
#include <chess/zobrist.h>
#include <chess/engine/evaluator.h>
#include <chess/engine/transposition_table.h>

class engine {
    typedef chess::core::move move;
    typedef chess::core::board board;
    typedef chess::core::game game;

    std::chrono::milliseconds max_time;

    std::vector<std::pair<move, move>> killers;
    int nodes = 0;
    int qnodes = 0;
    int current_depth = -1;
    int cache_hit_count = 0;
    int history[2][64][64];
    transposition_table tt{10'000'000};
    bool can_do_null_move = true;
    std::chrono::steady_clock::time_point initial_search_time;
    evaluator& eval;

    bool no_more_time();
public:
    bool time_over = false;
    move bestmove;
    int max_depth;

    engine(evaluator& e, int max_depth = 30);

    move timed_search(game& g, const std::chrono::milliseconds& time);

    std::pair<move, int> search_iterate(game& g);

    int search_widen(game& g, int depth, int val);

    int search_root(game& g, int depth, int alpha, int beta);

    template<bool is_pv>
    int search(game& g, int depth, int ply, int alpha, int beta);

    void set_killer_move(move m, int ply);

    void log_score(const board& b, int val);

    int qsearch(game& g, int ply, int alpha, int beta);

    std::vector<std::pair<move, int>>
    get_move_scores(const board& b, int ply, const std::vector<move>& moves, const move tt_move);

    void sort_moves(std::vector<std::pair<move, int>>& moves, int first);
};

#endif //CHESSENGINE_ENGINE_H
