//
// Created by leon on 7/28/19.
//

#include <deque>
#include <algorithm>
#include <thread>
#include <mutex>
#include <future>

#include <chess/move_gen.h>
#include <chess/game.h>
#include <chess/zobrist.h>

#include <chess/engine/engine.h>
#include <chess/engine/evaluator.h>
#include <chess/engine/transposition_table.h>

using namespace chess::core;

std::pair<move, int> engine::search_iterate(game& g) {

    time_over = false;
    initial_search_time = std::chrono::steady_clock::now();
    auto legal_moves = move_gen(g.states.back().b).generate();
    if (legal_moves.empty()) {
        if (g.states.back().b.under_check())
            return std::make_pair(null_move, -MATE);
        else
            return std::make_pair(null_move, 0);
    }

    for (int c = 0; c < 2; c++)
        for (int i = 0; i < 64; i++)
            for (int j = 0; j < 64; j++)
                history[c][i][j] /= 8;
    nodes = 0;
    qnodes = 0;
    cache_hit_count = 0;
    // assuming last search was last played move, we can shift the killer move list one ply to the left and hope it will be still valid
    for (int i = 0; i < killers.size() - 1 && !killers.empty(); i++) {
        killers[i] = killers[i + 1];
    }

    bestmove = legal_moves[0];
    current_depth = 1;
    int val = search_root(g, current_depth, -INF, +INF);

    for (current_depth = 2; current_depth <= max_depth; current_depth++) {
        if (time_over) break;
        if (val > MATE - current_depth) break;
        val = search_widen(g, current_depth, val);
    }
    return std::make_pair(bestmove, val);
}

int engine::search_widen(game& g, int depth, int val) {
    const int alpha = val - 50;
    const int beta = val + 50;
    const int tmp = search_root(g, depth, alpha, beta);
    if (alpha < tmp && tmp < beta) return tmp;
    return search_root(g, depth, -INF, INF);
}

int engine::search_root(game& g, int depth, int alpha, int beta) {
    if (no_more_time()) return 0;
    auto b = g.states.back().b;
    auto hash = g.states.back().hash;
    tt_node node{};
    move current_bestmove = bestmove;
    if (tt.load(hash, depth, alpha, beta, &node) && node.type == EXACT) {
        if (node.bestmove != null_move)
            current_bestmove = node.bestmove;
    }
    auto legal_moves = get_move_scores(b, 0, current_bestmove);
    int val;
    int best = -1;
    int bestval = -INF;
    for (int i = 0; i < legal_moves.size(); i++) {
        sort_moves(legal_moves, i);
        move m = legal_moves[i].first;
        g.do_move(m);
        auto _ = auto_undo_last_move(g);
        if (best == -1) {
            val = -search<true>(g, depth - 1, 1, -beta, -alpha);
        } else {
            int tmp = -search<false>(g, depth - 1, 1, -alpha - 1, -alpha);
            if (no_more_time()) return 0;
            if (tmp > alpha) {
                val = -search<true>(g, depth - 1, 1, -beta, -alpha);
                if (no_more_time()) return 0;
            }
            else {
                continue;
            }
        }
        if (val > bestval) {
            bestval = val;
            best = i;
        }
        if (val > alpha) {
            best = i;
            current_bestmove = m;
            if (val > beta) {
                tt.save(hash, depth, val, BETA, m);
                return val;
            }
            alpha = val;
            tt.save(hash, depth, alpha, ALPHA, m);
            bestmove = current_bestmove;
            log_score(b, alpha);
            if (val >= MATE - depth) break;
        }
    }
    assert(best > -1);
    bestmove = current_bestmove;
    tt.save(hash, depth, alpha, EXACT, bestmove);
    return alpha;
}

template<bool is_pv>
int engine::search(game& g, int depth, int ply, int alpha, int beta) {
    if (no_more_time()) return 0;
    board b = g.states.back().b;
    uint64_t hash = g.states.back().hash;
    int mate_value = MATE - ply;

    if (alpha < -mate_value) alpha = -mate_value;
    if (beta > mate_value) beta = mate_value;
    if (alpha >= beta) return alpha;

    if (g.is_draw_by_3foldrep() || g.is_draw_by_50move()) return 0;

    tt_node node;
    move tt_move = null_move;
    int val;
    if (tt.load(hash, depth, alpha, beta, &node)) {
        cache_hit_count++;
        tt_move = node.bestmove;
        val = node.value;
        if (!is_pv || (alpha < val && val < beta)) {
            if (std::abs(val) > MATE - 100) {
                if (val > 0) val = val - ply;
                else val = val + ply;
            }
            return val;
        }
    } else if (tt.load(hash, -1, -INF, INF, &node)) {
        tt_move = node.bestmove;
    }

    if (g.is_draw_by_insufficient_material()) {
        tt.save(hash, INF, 0, EXACT, null_move);
        return 0;
    }

    bool in_check = b.under_check(b.side_to_play);
    if (in_check) depth++;
    if (depth <= 0 && !in_check)
        return qsearch(g, ply, alpha, beta);

    nodes++;

    auto legal_moves = get_move_scores(b, ply, tt_move);
    if (legal_moves.empty()) {
        val = 0;
        if (in_check) {
            val = -MATE + ply;
            tt.save(hash, INF, -MATE, EXACT, null_move);
        } else {
            tt.save(hash, INF, 0, EXACT, null_move);
        }
        return val;
    }

    if (depth < 3
        && !is_pv
        && !in_check
        && abs(beta - 1) > -MATE + 100)
    {
        int static_eval = eval.eval(b) * (b.side_to_play == BLACK ? -1 : 1);

        int eval_margin = 120 * depth;
        if (static_eval - eval_margin >= beta)
            return static_eval - eval_margin;
    }


    // Null-move pruning is unsound in zugzwang: if the side to move has only
    // pawns left, "passing" can be strictly better than any legal move, so the
    // null search returns a bound that doesn't hold. Require non-pawn material.
    const bitboard us_non_pawn = b.piece_of_color[b.side_to_play]
        & (b.piece_of_type[KNIGHT] | b.piece_of_type[BISHOP] | b.piece_of_type[ROOK] | b.piece_of_type[QUEEN]);
    if (!is_pv && !in_check && depth > 2 && can_do_null_move && us_non_pawn && eval.eval(b) * (b.side_to_play == BLACK ? -1 : 1) >= beta) {
        g.do_null_move();
        can_do_null_move = false;
        int nmval;
        if (depth > 6)
            nmval = -search<is_pv>(g, depth - 4, ply + 1, -beta, -beta + 1);
        else
            nmval = -search<is_pv>(g, depth - 3, ply + 1, -beta, -beta + 1);
        can_do_null_move = true;
        g.undo_last_move();
        if (no_more_time()) return 0;
        if (nmval >= beta) return nmval;
    }


    bool raised_alpha = false;
    int best = -1;
    int bestval = -INF;
    tt_node_type new_tt_node_type = ALPHA;
    for (int i = 0; i < legal_moves.size(); i++) {
        sort_moves(legal_moves, i);
        move m = legal_moves[i].first;
        g.do_move(m);
        auto _ = auto_undo_last_move(g);
        if (!raised_alpha) {
            val = -search<is_pv>(g, depth - 1, ply + 1, -beta, -alpha);
            if (no_more_time()) return 0;
        } else {
            int tmp = -search<false>(g, depth - 1, ply + 1, -alpha - 1, -alpha);
            if (no_more_time()) return 0;
            if (tmp > alpha) {
                val = -search<true>(g, depth - 1, ply + 1, -beta, -alpha);
                if (no_more_time()) return 0;
            }
            else continue;
        }
        if (val > bestval) {
            bestval = val;
            best = i;
        }
        if (val > alpha) {
            best = i;
            if (val >= beta) {
                square dest = move_dest(m);
                if (b.piece_at(get_bb(dest)) == NO_PIECE && dest != b.en_passant && move_type(m) < PROMOTION_QUEEN) {
                    history[b.side_to_play][move_origin(m)][move_dest(m)] += depth * depth;
                    set_killer_move(m, ply);
                }
                new_tt_node_type = BETA;
                alpha = val;
                break;
            }
            raised_alpha = true;
            new_tt_node_type = EXACT;
            alpha = val;
            if (val >= MATE - depth) break;
        }
    }
    if (alpha > MATE - 100) {
        if (MATE - (alpha + ply) <= depth)
            tt.save(hash, INF, alpha + ply, new_tt_node_type, legal_moves[best].first);
        else
            tt.save(hash, depth, alpha + ply, new_tt_node_type, legal_moves[best].first);
    } else if (alpha < -MATE + 100) {
        tt.save(hash, depth, alpha - ply, new_tt_node_type, legal_moves[best].first);
    } else {
        tt.save(hash, depth, alpha, new_tt_node_type, legal_moves[best].first);
    }
    return alpha;
}

std::vector<std::pair<move, int>> engine::get_move_scores(const board& b, int ply, const move tt_move) {
    move move_buffer[220];
    move* moves_end = move_gen(b).generate_into(move_buffer);
    std::vector<std::pair<move, int>> ret;
    ret.reserve(moves_end - move_buffer);
    for (move* mp = move_buffer; mp != moves_end; ++mp) {
        const move m = *mp;
        const square org = move_origin(m);
        const square dst = move_dest(m);
        int score = history[b.side_to_play][org][dst];
        board bnew = b;
        bnew.make_move(m);
        if (m == tt_move) score += 1'000'000'000;
        if (bnew.under_check(bnew.side_to_play)) score += 400'000'000;
        const piece mover = b.piece_at(get_bb(org));
        const piece captured = b.piece_at(get_bb(dst));
        if (captured != NO_PIECE || (mover == PAWN && dst == b.en_passant)) {
            score += 100000100;
            if (captured == QUEEN) score += 800;
            else if (captured == ROOK) score += 400;
            else if (captured == BISHOP) score += 235;
            else if (captured == KNIGHT) score += 225;
        }
        if (mover == PAWN && (get_rank(dst) % 8) == 0) score += 90'000'000;
        if (killers.size() > ply) {
            if (killers[ply].first == m) score += 80'000'000;
            if (killers[ply].second == m) score += 79'999'999;
        }
        ret.emplace_back(m, score);
    }
    return ret;
}

void engine::sort_moves(std::vector<std::pair<move, int>>& moves, int first) {
    int high = first;
    int high_value = -INF;
    for (int i = first; i < moves.size(); i++) {
        if (moves[i].second > high_value) {
            high = i;
            high_value = moves[i].second;
        }
    }
    if (high > first) {
        auto tmp = moves[first];
        moves[first] = moves[high];
        moves[high] = tmp;
    }
}

engine::engine(evaluator& e, int max_depth) : eval(e), max_depth(max_depth) {
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < 64; i++)
            for (int j = 0; j < 64; j++)
                history[c][i][j] = 0;
}

void engine::set_killer_move(move m, int ply) {
    if (killers.size() <= ply) {
        killers.resize(ply + 1, std::make_pair(null_move, null_move));
    }

    if (killers[ply].first != m) {
        killers[ply].second = killers[ply].first;
        killers[ply].first = m;
    }
}

int engine::qsearch(game& g, int ply, int alpha, int beta) {
    if (no_more_time()) return 0;
    if (g.is_draw_by_3foldrep() || g.is_draw_by_50move()) return 0;
    const board& b = g.states.back().b;
    nodes++;
    qnodes++;
    uint64_t hash = g.states.back().hash;
    tt_node node;
    move tt_move = null_move;
    // Probe the TT for the hash move and for a real bound cutoff from entries
    // the main search wrote. We deliberately do NOT write per-node qsearch
    // results back: they would be depth-0 entries that evict the main search's
    // valuable deep entries under the naive replacement scheme. (Previously
    // qsearch stored the raw static eval as EXACT and returned it on a hit,
    // which could return a non-quiescent value that ignores captures.)
    if (tt.load(hash, 0, &node)) {
        tt_move = node.bestmove;
        int val = node.value;
        if (std::abs(val) > MATE - 100) val = val > 0 ? val - ply : val + ply;
        if (node.type == EXACT) return val;
        if (node.type == ALPHA && val <= alpha) return val;
        if (node.type == BETA && val >= beta) return val;
    }

    int stand_pat = eval.eval(b);
    if (b.side_to_play == BLACK) stand_pat = -stand_pat;
    if (stand_pat >= beta) return stand_pat;
    if (stand_pat > alpha) alpha = stand_pat;

    if (g.is_draw_by_insufficient_material()) {
        tt.save(hash, INF, 0, EXACT, null_move);
        return 0;
    }
    // Generate all legal moves (needed to detect checkmate/stalemate via an
    // empty list), but score ONLY the captures/promotions/en-passant that
    // qsearch will actually search. This avoids the per-move board copy +
    // make_move + under_check that get_move_scores pays for every quiet move
    // qsearch would otherwise skip.
    move move_buffer[220];
    move* moves_end = move_gen(b).generate_into(move_buffer);
    if (moves_end == move_buffer) {
        if (b.under_check(b.side_to_play)) {
            tt.save(hash, INF, -MATE, EXACT, null_move);
            return -MATE + ply;
        }
        tt.save(hash, INF, 0, EXACT, null_move);
        return 0;
    }

    std::vector<std::pair<move, int>> captures;
    captures.reserve(moves_end - move_buffer);
    for (move* mp = move_buffer; mp != moves_end; ++mp) {
        const move m = *mp;
        const square dst = move_dest(m);
        const piece captured = b.piece_at(get_bb(dst));
        const bool promo = move_type(m) >= PROMOTION_QUEEN;
        const bool ep = (dst == b.en_passant && b.piece_at(get_bb(move_origin(m))) == PAWN);
        if (captured == NO_PIECE && !promo && !ep) continue;   // skip quiets
        int score = (m == tt_move) ? 1'000'000'000 : 0;
        if (captured != NO_PIECE || ep) {
            score += 100000100;
            if (captured == QUEEN) score += 800;
            else if (captured == ROOK) score += 400;
            else if (captured == BISHOP) score += 235;
            else if (captured == KNIGHT) score += 225;
        }
        if (promo) score += 90'000'000;
        captures.emplace_back(m, score);
    }

    int best = stand_pat;
    for (int i = 0; i < captures.size(); i++) {
        sort_moves(captures, i);
        move m = captures[i].first;
        g.do_move(m);
        auto _ = auto_undo_last_move(g);
        int val = -qsearch(g, ply + 1, -beta, -alpha);
        if (no_more_time()) return 0;
        if (val > best) best = val;
        if (val > alpha) alpha = val;
        if (val >= beta) break;
    }
    return best;
}

void engine::log_score(const board& b, int val) {
    if (time_over) return;
    int mate = MATE - std::abs(val);
    std::stringstream ss;
    ss << "info depth " << current_depth;
    if (mate < 30) {
        if (val < 0)
            ss << " score mate -" << mate;
        else
            ss << " score mate " << mate;

    } else {
        ss << " score cp " << val;
    }

    long time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - initial_search_time).count();
    ss << " nodes " << nodes;
    ss << " qnodes " << qnodes;
    ss << " nps " << int(double(nodes) * 1'000'000'000 / double(time));
    ss << " time " << (time / 1'000'000);
    ss << " tthit " << cache_hit_count;
    ss << " pv " << to_long_move(bestmove);
    tt_node node{};
    board b2 = b;
    b2.make_move(bestmove);

    for (int i = 1; i < current_depth; i++) {
        auto hash = zobrist::hash(b2);
        if (tt.load(hash, 0, &node) && node.bestmove != null_move) {
            ss << " " << to_long_move(node.bestmove);
            b2.make_move(node.bestmove);
        } else {
            break;
        }
    }
    std::cout << ss.str() << std::endl;
}

move engine::timed_search(game& g, const std::chrono::milliseconds& time) {
    // Runs synchronously. The UCI driver runs this on its own thread so the
    // input loop stays responsive and `stop` can flip time_over to abort us.
    time_over = false;
    bestmove = null_move;
    max_time = time;
    search_iterate(g);
    std::cout << "bestmove " << to_long_move(bestmove) << std::endl;
    return bestmove;
}

bool engine::no_more_time() {
    if (time_over) return true;
    if (max_time.count() == 0) return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - initial_search_time);
    if (elapsed >= max_time) time_over = true;
    return time_over;
}

