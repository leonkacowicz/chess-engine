// Texel tuner for the static-evaluation scalar weights (#mzdhchb).
//
// 1. Build a labeled dataset by self-play (engine vs itself at a shallow depth,
//    random openings for variety); each non-check position is labeled with the
//    game's result from White's perspective. Cached to a file.
// 2. Fit the sigmoid scale K that best maps eval (centipawns, White POV) to the
//    win probability.
// 3. Coordinate-descend eval_w to minimize mean squared error of
//    sigmoid(K * eval) vs the result, over decreasing step sizes.
// 4. Print the tuned weights to paste into static_evaluator.cpp.
//
// Usage: eval_tune [data_file] [n_games] [depth]

#include <random>
#include <cmath>
#include <vector>
#include <fstream>
#include <iostream>
#include <string>
#include <chess/core.h>
#include <chess/board.h>
#include <chess/game.h>
#include <chess/move_gen.h>
#include <chess/fen.h>
#include <chess/engine/engine.h>
#include <chess/engine/static_evaluator.h>

using namespace chess::core;

// result = White's game outcome (1 / 0.5 / 0); eval_cp = the move-selection
// search score at this position, White POV (a low-variance per-position label
// that, unlike the outcome, isn't poisoned by a later blunder).
struct sample { board b; double result; int eval_cp; };

static std::vector<sample> selfplay_dataset(int n_games, int depth, uint64_t seed) {
    std::vector<sample> data;
    std::mt19937_64 rng(seed);
    static_evaluator ev;
    engine e(ev, depth);
    // The engine prints "info ..." to stdout per move (log_score); silence it
    // during data generation so it doesn't flood the output.
    std::ofstream devnull("/dev/null");
    std::streambuf* old_cout = std::cout.rdbuf(devnull.rdbuf());
    for (int gi = 0; gi < n_games; gi++) {
        game g;
        // random opening for variety
        bool aborted = false;
        int ropen = 4 + (int) (rng() % 8);
        for (int k = 0; k < ropen; k++) {
            auto mv = move_gen(g.states.back().b).generate();
            if (mv.empty()) { aborted = true; break; }
            g.do_move(mv[rng() % mv.size()]);
        }
        if (aborted) continue;

        std::vector<std::pair<board, int>> positions;   // (position, White-POV search score)
        double result = 0.5;
        int ply = 0;
        const int MAXPLY = 240;
        while (true) {
            const board cur = g.states.back().b;
            auto mv = move_gen(cur).generate();
            if (mv.empty()) {                          // mate or stalemate
                result = cur.under_check() ? (cur.side_to_play == WHITE ? 0.0 : 1.0) : 0.5;
                break;
            }
            if (g.is_draw_by_50move() || g.is_draw_by_3foldrep() || g.is_draw_by_insufficient_material()) break;
            if (ply >= MAXPLY) break;                  // unresolved -> draw label
            auto sr = e.search_iterate(g);             // (best move, score from side-to-move POV)
            move m = sr.first;
            if (m == null_move) break;
            if (!cur.under_check()) {                  // quiet-ish only
                const int wcp = (cur.side_to_play == WHITE) ? sr.second : -sr.second;
                positions.push_back({cur, wcp});
            }
            g.do_move(m);
            ply++;
        }
        for (auto& pr : positions) data.push_back({pr.first, result, pr.second});
        if ((gi + 1) % 100 == 0) std::cerr << "  " << (gi + 1) << " games, " << data.size() << " positions\n";
    }
    std::cout.rdbuf(old_cout);   // restore before devnull is destroyed
    return data;
}

static double sigmoid(int eval_cp, double K) {
    return 1.0 / (1.0 + std::pow(10.0, -K * eval_cp / 400.0));
}

// Option 1 (terminal grounding): a search score above this magnitude is a PROVEN
// mate (engine MATE=32000), i.e. an exact terminal value, not an estimate.
const int MATE_LABEL = 31900;
double mate_weight = 1.0;        // >1 weights those certain labels more (set from argv)

// Per-position {target, weight}. Mate-scored positions are ground truth: exact
// 1/0 target, optionally up-weighted. Others: blend of the game outcome
// (ground-truth anchor, noisy) and the search score through the sigmoid (low
// variance, biased). lambda=1 -> pure outcome (Texel); lambda=0 -> pure eval.
static std::pair<double, double> target_weight(const sample& s, double K, double lambda) {
    if (s.eval_cp >  MATE_LABEL) return {1.0, mate_weight};   // White has a forced mate
    if (s.eval_cp < -MATE_LABEL) return {0.0, mate_weight};   // White is getting mated
    return {lambda * s.result + (1.0 - lambda) * sigmoid(s.eval_cp, K), 1.0};
}

static double mse(const std::vector<sample>& data, double K, double lambda) {
    static_evaluator ev;
    double e = 0, wsum = 0;
    for (const auto& s : data) {
        const auto tw = target_weight(s, K, lambda);
        const double diff = tw.first - sigmoid(ev.eval(s.b), K);
        e += tw.second * diff * diff;
        wsum += tw.second;
    }
    return e / wsum;
}

// Fit K against the ground-truth outcome (lambda=1) so the cp->win-prob scale is
// anchored to reality, then reuse that K for the blended target.
static double fit_K(const std::vector<sample>& data) {
    double bestK = 1.0, bestE = mse(data, 1.0, 1.0);
    for (double K = 0.40; K <= 2.0; K += 0.02) {
        double e = mse(data, K, 1.0);
        if (e < bestE) { bestE = e; bestK = K; }
    }
    return bestK;
}

int main(int argc, char** argv) {
    chess::core::init();
    const std::string file = argc > 1 ? argv[1] : "eval_tune_data.txt";
    const double lambda    = argc > 2 ? std::stod(argv[2]) : 0.5;   // outcome weight
    mate_weight            = argc > 3 ? std::stod(argv[3]) : 1.0;   // option 1: weight on mate labels
    const int n_games      = argc > 4 ? std::stoi(argv[4]) : 1500;
    const int depth        = argc > 5 ? std::stoi(argv[5]) : 4;

    std::vector<sample> data;
    std::ifstream in(file);
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const auto p1 = line.find(';');                  // end of FEN
            const auto p2 = line.find(';', p1 + 1);          // end of result
            data.push_back({fen::board_from_fen(line.substr(0, p1)),
                            std::stod(line.substr(p1 + 1, p2 - p1 - 1)),
                            std::stoi(line.substr(p2 + 1))});
        }
        std::cerr << "loaded " << data.size() << " positions from " << file << "\n";
    } else {
        std::cerr << "generating self-play dataset (" << n_games << " games, depth " << depth << ")...\n";
        data = selfplay_dataset(n_games, depth, 0x9e3779b97f4a7c15ULL);
        std::ofstream out(file);
        for (auto& s : data) out << fen::to_string(s.b, 1) << ";" << s.result << ";" << s.eval_cp << "\n";
        std::cerr << "wrote " << data.size() << " positions to " << file << "\n";
    }
    if (data.empty()) { std::cerr << "no data\n"; return 1; }

    const double K = fit_K(data);
    std::cout << "positions=" << data.size() << "  K=" << K << "  lambda=" << lambda
              << "  mate_weight=" << mate_weight << "  initial E=" << mse(data, K, lambda) << "\n";

    std::vector<int*> params = {
        &eval_w.material[1], &eval_w.material[2], &eval_w.material[3], &eval_w.material[4],
        &eval_w.mob[1], &eval_w.mob[2], &eval_w.mob[3], &eval_w.mob[4],
        &eval_w.doubled, &eval_w.isolated,
        &eval_w.passed_mg[2], &eval_w.passed_mg[3], &eval_w.passed_mg[4], &eval_w.passed_mg[5], &eval_w.passed_mg[6],
        &eval_w.passed_eg[2], &eval_w.passed_eg[3], &eval_w.passed_eg[4], &eval_w.passed_eg[5], &eval_w.passed_eg[6],
        &eval_w.bishop_pair, &eval_w.rook_open, &eval_w.rook_semi,
    };
    double best = mse(data, K, lambda);
    for (int step : {16, 8, 4, 2, 1}) {
        bool improved = true;
        while (improved) {
            improved = false;
            for (int* p : params) {
                *p += step;
                double e = mse(data, K, lambda);
                if (e < best) { best = e; improved = true; continue; }
                *p -= 2 * step;
                e = mse(data, K, lambda);
                if (e < best) { best = e; improved = true; continue; }
                *p += step;   // revert
            }
        }
        std::cout << "  after step " << step << ": E=" << best << "\n";
    }

    std::cout << "final E=" << best << "\n--- tuned eval_w ---\n";
    std::cout << "material = {" << eval_w.material[0] << ", " << eval_w.material[1] << ", " << eval_w.material[2]
              << ", " << eval_w.material[3] << ", " << eval_w.material[4] << "}\n";
    std::cout << "mob = {" << eval_w.mob[0] << ", " << eval_w.mob[1] << ", " << eval_w.mob[2] << ", "
              << eval_w.mob[3] << ", " << eval_w.mob[4] << "}\n";
    std::cout << "doubled=" << eval_w.doubled << " isolated=" << eval_w.isolated << "\n";
    std::cout << "passed_mg = {0, 0, " << eval_w.passed_mg[2] << ", " << eval_w.passed_mg[3] << ", "
              << eval_w.passed_mg[4] << ", " << eval_w.passed_mg[5] << ", " << eval_w.passed_mg[6] << ", 0}\n";
    std::cout << "passed_eg = {0, 0, " << eval_w.passed_eg[2] << ", " << eval_w.passed_eg[3] << ", "
              << eval_w.passed_eg[4] << ", " << eval_w.passed_eg[5] << ", " << eval_w.passed_eg[6] << ", 0}\n";
    std::cout << "bishop_pair=" << eval_w.bishop_pair << " rook_open=" << eval_w.rook_open
              << " rook_semi=" << eval_w.rook_semi << "\n";
    return 0;
}
