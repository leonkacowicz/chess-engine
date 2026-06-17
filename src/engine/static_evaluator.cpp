//
// Created by leon on 2019-11-02.
//

#include <chess/engine/static_evaluator.h>

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

    if (phase > PHASE_MAX) phase = PHASE_MAX;   // early promotions can exceed it
    const int mg_score = mg[WHITE] - mg[BLACK];
    const int eg_score = eg[WHITE] - eg[BLACK];
    return (mg_score * phase + eg_score * (PHASE_MAX - phase)) / PHASE_MAX;
}
