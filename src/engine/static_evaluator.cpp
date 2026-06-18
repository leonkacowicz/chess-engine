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
namespace {

const int MATERIAL[5] = {100, 320, 330, 500, 900};   // P N B R Q (king has none)
const int PHASE_W[5]  = {0, 1, 1, 2, 4};             // game-phase weight per piece
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
// (where a passed pawn's promotion threat dominates). Indexed by the pawn's
// relative rank (how far it has advanced, 0..7; only 1..6 occur).
const int DOUBLED = 12;
const int ISOLATED = 14;
const int PASSED_MG[8] = {0, 0, 5, 10, 20, 35, 60, 0};
const int PASSED_EG[8] = {0, 0, 10, 20, 40, 70, 120, 0};

void pawn_structure(const board& b, color c, int& mg_out, int& eg_out) {
    const bitboard own = b.piece_of_type[PAWN] & b.piece_of_color[c];
    const bitboard enemy = b.piece_of_type[PAWN] & b.piece_of_color[opposite(c)];

    int flat = 0;   // doubled + isolated, applied equally to mg and eg
    for (int f = 0; f < 8; f++) {
        const int cnt = num_squares(own & file[f]);
        if (cnt >= 2) flat -= (cnt - 1) * DOUBLED;
        if (cnt > 0) {
            const bitboard adj = (f > 0 ? file[f - 1] : 0) | (f < 7 ? file[f + 1] : 0);
            if (!(own & adj)) flat -= cnt * ISOLATED;   // every pawn on this file is isolated
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
            mg += PASSED_MG[rel];
            eg += PASSED_EG[rel];
        }
    }
    mg_out = mg;
    eg_out = eg;
}

// Mobility: per-piece count of reachable squares (sliders see through to current
// occupancy; own pieces excluded), weighted by piece type. Rewards active pieces
// and penalizes cramped ones. Uses magic-bitboard attacks, so it has a real
// per-eval cost.
const int MOB_N = 3, MOB_B = 3, MOB_R = 2, MOB_Q = 1;

int mobility(const board& b, color c) {
    const bitboard own = b.piece_of_color[c];
    const bitboard occ = b.piece_of_color[WHITE] | b.piece_of_color[BLACK];
    const bitboard not_own = ~own;
    int m = 0;
    bitboard bb;

    bb = b.piece_of_type[KNIGHT] & own;
    while (bb) { square sq = pop_lsb(&bb); m += MOB_N * num_squares(knight_attacks(sq) & not_own); }
    bb = b.piece_of_type[BISHOP] & own;
    while (bb) { square sq = pop_lsb(&bb); m += MOB_B * num_squares(attacks_from_bishop(sq, occ) & not_own); }
    bb = b.piece_of_type[ROOK] & own;
    while (bb) { square sq = pop_lsb(&bb); m += MOB_R * num_squares(attacks_from_rook(sq, occ) & not_own); }
    bb = b.piece_of_type[QUEEN] & own;
    while (bb) {
        square sq = pop_lsb(&bb);
        m += MOB_Q * num_squares((attacks_from_rook(sq, occ) | attacks_from_bishop(sq, occ)) & not_own);
    }
    return m;
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
                const int s = MATERIAL[p] + PST[p][idx];
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
    }

    if (phase > PHASE_MAX) phase = PHASE_MAX;   // early promotions can exceed it
    const int mg_score = mg[WHITE] - mg[BLACK];
    const int eg_score = eg[WHITE] - eg[BLACK];
    return (mg_score * phase + eg_score * (PHASE_MAX - phase)) / PHASE_MAX;
}
