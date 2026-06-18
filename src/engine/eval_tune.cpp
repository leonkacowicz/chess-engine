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

struct sample { board b; double result; };   // result = White's game outcome (1 / 0.5 / 0)

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

        std::vector<board> positions;
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
            if (!cur.under_check()) positions.push_back(cur);   // quiet-ish only
            move m = e.search_iterate(g).first;
            if (m == null_move) break;
            g.do_move(m);
            ply++;
        }
        for (auto& p : positions) data.push_back({p, result});
        if ((gi + 1) % 100 == 0) std::cerr << "  " << (gi + 1) << " games, " << data.size() << " positions\n";
    }
    std::cout.rdbuf(old_cout);   // restore before devnull is destroyed
    return data;
}

static double sigmoid(int eval_cp, double K) {
    return 1.0 / (1.0 + std::pow(10.0, -K * eval_cp / 400.0));
}

static double mse(const std::vector<sample>& data, double K) {
    static_evaluator ev;
    double e = 0;
    for (const auto& s : data) {
        const double diff = s.result - sigmoid(ev.eval(s.b), K);
        e += diff * diff;
    }
    return e / data.size();
}

static double fit_K(const std::vector<sample>& data) {
    double bestK = 1.0, bestE = mse(data, 1.0);
    for (double K = 0.40; K <= 2.0; K += 0.02) {
        double e = mse(data, K);
        if (e < bestE) { bestE = e; bestK = K; }
    }
    return bestK;
}

int main(int argc, char** argv) {
    chess::core::init();
    const std::string file = argc > 1 ? argv[1] : "eval_tune_data.txt";
    const int n_games = argc > 2 ? std::stoi(argv[2]) : 1500;
    const int depth   = argc > 3 ? std::stoi(argv[3]) : 4;

    std::vector<sample> data;
    std::ifstream in(file);
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            auto sc = line.rfind(';');
            data.push_back({fen::board_from_fen(line.substr(0, sc)), std::stod(line.substr(sc + 1))});
        }
        std::cerr << "loaded " << data.size() << " positions from " << file << "\n";
    } else {
        std::cerr << "generating self-play dataset (" << n_games << " games, depth " << depth << ")...\n";
        data = selfplay_dataset(n_games, depth, 0x9e3779b97f4a7c15ULL);
        std::ofstream out(file);
        for (auto& s : data) out << fen::to_string(s.b, 1) << ";" << s.result << "\n";
        std::cerr << "wrote " << data.size() << " positions to " << file << "\n";
    }
    if (data.empty()) { std::cerr << "no data\n"; return 1; }

    const double K = fit_K(data);
    std::cout << "positions=" << data.size() << "  K=" << K << "  initial E=" << mse(data, K) << "\n";

    std::vector<int*> params = {
        &eval_w.material[1], &eval_w.material[2], &eval_w.material[3], &eval_w.material[4],
        &eval_w.mob[1], &eval_w.mob[2], &eval_w.mob[3], &eval_w.mob[4],
        &eval_w.doubled, &eval_w.isolated,
        &eval_w.passed_mg[2], &eval_w.passed_mg[3], &eval_w.passed_mg[4], &eval_w.passed_mg[5], &eval_w.passed_mg[6],
        &eval_w.passed_eg[2], &eval_w.passed_eg[3], &eval_w.passed_eg[4], &eval_w.passed_eg[5], &eval_w.passed_eg[6],
        &eval_w.bishop_pair, &eval_w.rook_open, &eval_w.rook_semi,
    };
    double best = mse(data, K);
    for (int step : {16, 8, 4, 2, 1}) {
        bool improved = true;
        while (improved) {
            improved = false;
            for (int* p : params) {
                *p += step;
                double e = mse(data, K);
                if (e < best) { best = e; improved = true; continue; }
                *p -= 2 * step;
                e = mse(data, K);
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
