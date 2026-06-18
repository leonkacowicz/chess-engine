//
// Created by leon on 2019-11-02.
//

#include <chess/engine/static_evaluator.h>
#include <chess/magic_bitboard.h>

using namespace chess::core;

// Tapered piece-square evaluation (Michniewski "Simplified Evaluation Function"
// tables). Material + per-piece positional tables, with the king interpolated
// between a middlegame "stay safe" table and an endgame "centralize" table by a
// game-phase weight derived from remaining non-pawn material.
//
// Tables are written rank-8-first (index 0 = a8 ... 63 = h1), so a WHITE piece
// on LERF square sq indexes table[sq ^ 56] and a BLACK piece indexes table[sq].
// Tunable scalar weights (see static_evaluator.h); defaults = the hand-set
// values each term shipped with. External linkage so the tuner can modify them.
eval_params eval_w = {
    .material = {100, 320, 330, 500, 900},
    .mob      = {0, 3, 3, 2, 1},                  // P N B R Q
    .doubled = 12, .isolated = 14,
    .passed_mg = {0, 0, 5, 10, 20, 35, 60, 0},
    .passed_eg = {0, 0, 10, 20, 40, 70, 120, 0},
    .bishop_pair = 30, .rook_open = 20, .rook_semi = 10,
};

namespace {

const int PHASE_W[5]  = {0, 1, 1, 2, 4};             // game-phase weight per piece (fixed)
const int PHASE_MAX   = 24;

const int PST_PAWN[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};
const int PST_KNIGHT[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};
const int PST_BISHOP[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};
const int PST_ROOK[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0
};
const int PST_QUEEN[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,
   -10,  5,  5,  5,  5,  5,  0,-10,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};
const int* PST[5] = {PST_PAWN, PST_KNIGHT, PST_BISHOP, PST_ROOK, PST_QUEEN};

const int KING_MG[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20
};
const int KING_EG[64] = {
   -50,-40,-30,-20,-20,-30,-40,-50,
   -30,-20,-10,  0,  0,-10,-20,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-30,  0,  0,  0,  0,-30,-30,
   -50,-30,-30,-30,-30,-30,-30,-50
};

// Pawn structure. Doubled and isolated pawns are flat penalties; passed pawns
// are a bonus that grows with advancement and is weighted toward the endgame
// (where a passed pawn's promotion threat dominates). Weights in eval_w.
void pawn_structure(const board& b, color c, int& mg_out, int& eg_out) {
    const bitboard own = b.piece_of_type[PAWN] & b.piece_of_color[c];
    const bitboard enemy = b.piece_of_type[PAWN] & b.piece_of_color[opposite(c)];

    int flat = 0;   // doubled + isolated, applied equally to mg and eg
    for (int f = 0; f < 8; f++) {
        const int cnt = num_squares(own & file[f]);
        if (cnt >= 2) flat -= (cnt - 1) * eval_w.doubled;
        if (cnt > 0) {
            const bitboard adj = (f > 0 ? file[f - 1] : 0) | (f < 7 ? file[f + 1] : 0);
            if (!(own & adj)) flat -= cnt * eval_w.isolated;   // every pawn on this file is isolated
        }
    }

    int mg = flat, eg = flat;
    bitboard p = own;
    while (p) {
        const square sq = pop_lsb(&p);
        const int f = get_file(sq), r = get_rank(sq);
        const bitboard files = file[f] | (f > 0 ? file[f - 1] : 0) | (f < 7 ? file[f + 1] : 0);
        bitboard ahead;
        int rel;
        if (c == WHITE) { ahead = ~bitboard(0) << (8 * (r + 1)); rel = r; }
        else            { ahead = (bitboard(1) << (8 * r)) - 1;   rel = 7 - r; }
        if (!(enemy & files & ahead)) {   // no enemy pawn can stop it -> passed
            mg += eval_w.passed_mg[rel];
            eg += eval_w.passed_eg[rel];
        }
    }
    mg_out = mg;
    eg_out = eg;
}

// Mobility: per-piece count of reachable squares (sliders see through to current
// occupancy; own pieces excluded), weighted by piece type. Rewards active pieces
// and penalizes cramped ones. Uses magic-bitboard attacks, so it has a real
// per-eval cost. Weights in eval_w.mob[piece].
int mobility(const board& b, color c) {
    const bitboard own = b.piece_of_color[c];
    const bitboard occ = b.piece_of_color[WHITE] | b.piece_of_color[BLACK];
    const bitboard not_own = ~own;
    int m = 0;
    bitboard bb;

    bb = b.piece_of_type[KNIGHT] & own;
    while (bb) { square sq = pop_lsb(&bb); m += eval_w.mob[KNIGHT] * num_squares(knight_attacks(sq) & not_own); }
    bb = b.piece_of_type[BISHOP] & own;
    while (bb) { square sq = pop_lsb(&bb); m += eval_w.mob[BISHOP] * num_squares(attacks_from_bishop(sq, occ) & not_own); }
    bb = b.piece_of_type[ROOK] & own;
    while (bb) { square sq = pop_lsb(&bb); m += eval_w.mob[ROOK] * num_squares(attacks_from_rook(sq, occ) & not_own); }
    bb = b.piece_of_type[QUEEN] & own;
    while (bb) {
        square sq = pop_lsb(&bb);
        m += eval_w.mob[QUEEN] * num_squares((attacks_from_rook(sq, occ) | attacks_from_bishop(sq, occ)) & not_own);
    }
    return m;
}

// Bishop pair (controls both colour complexes) and rooks on open / semi-open
// files (no pawns / no friendly pawns). Cheap, time-control-robust positional
// bonuses; flat across game phase. Weights in eval_w.
int positional_bonuses(const board& b, color c) {
    const bitboard own = b.piece_of_color[c];
    int s = 0;
    if (num_squares(b.piece_of_type[BISHOP] & own) >= 2) s += eval_w.bishop_pair;
    const bitboard own_pawns = b.piece_of_type[PAWN] & own;
    const bitboard all_pawns = b.piece_of_type[PAWN];
    bitboard rooks = b.piece_of_type[ROOK] & own;
    while (rooks) {
        const int f = get_file(pop_lsb(&rooks));
        if (!(all_pawns & file[f])) s += eval_w.rook_open;          // open file
        else if (!(own_pawns & file[f])) s += eval_w.rook_semi;     // semi-open (only enemy pawns)
    }
    return s;
}

}  // namespace

int static_evaluator::eval(const board& b) {
    const bitboard occupied = b.piece_of_color[WHITE] | b.piece_of_color[BLACK];
    int mg[2] = {0, 0};
    int eg[2] = {0, 0};
    int phase = 0;

    for (bitboard i = 1; i != 0; i <<= 1uL) {
        if (!(occupied & i)) continue;
        const color c = (b.piece_of_color[WHITE] & i) ? WHITE : BLACK;
        const int idx = (c == WHITE) ? (int(get_square(i)) ^ 56) : int(get_square(i));
        for (int p = PAWN; p <= QUEEN; p++) {
            if (b.piece_of_type[p] & i) {
                const int s = eval_w.material[p] + PST[p][idx];
                mg[c] += s;
                eg[c] += s;
                phase += PHASE_W[p];
                break;
            }
        }
    }

    // Kings are tracked outside piece_of_type; evaluate them tapered.
    for (color c : {WHITE, BLACK}) {
        if (b.king_pos[c] == SQ_NONE) continue;
        const int idx = (c == WHITE) ? (int(b.king_pos[c]) ^ 56) : int(b.king_pos[c]);
        mg[c] += KING_MG[idx];
        eg[c] += KING_EG[idx];
    }

    for (color c : {WHITE, BLACK}) {
        int psm, pse;
        pawn_structure(b, c, psm, pse);
        mg[c] += psm;
        eg[c] += pse;
        const int mob = mobility(b, c);
        mg[c] += mob;
        eg[c] += mob;
        const int pb = positional_bonuses(b, c);
        mg[c] += pb;
        eg[c] += pb;
    }

    if (phase > PHASE_MAX) phase = PHASE_MAX;   // early promotions can exceed it
    const int mg_score = mg[WHITE] - mg[BLACK];
    const int eg_score = eg[WHITE] - eg[BLACK];
    return (mg_score * phase + eg_score * (PHASE_MAX - phase)) / PHASE_MAX;
}
