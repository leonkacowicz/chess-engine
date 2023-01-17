//
// Created by leon on 2019-11-02.
//

#include <chess/engine/static_evaluator.h>

using namespace chess::core;

int static_evaluator::eval(const board & b) {

    int accum[2] = {0, 0};
    for (bitboard i(1); i != 0; i <<= 1uL) {
        color c = b.color_at(i);
        if (b.piece_of_type[PAWN] & i)
            accum[c] += 1'00
                    + 2 * (get_rank(get_square(i))) * (c == WHITE)
                    + 2 * (7 - get_rank(get_square(i))) * (c == BLACK)
                    + 2 * (((file_d | file_e) & i) != 0);
        if (b.piece_of_type[KNIGHT] & i)
            accum[c] += 2'95
                    + 5 * (((file_c | file_d | file_e | file_f) & (rank_3 | rank_4 | rank_5 | rank_6) & i) != 0)
                    + 5 * (((file_d | file_e) & (rank_4 | rank_5) & i) != 0);
        if (b.piece_of_type[BISHOP] & i)
            accum[c] += 3'15
                    + (((rank_1 & i) && c == WHITE) || ((rank_8 & i) && c == BLACK) ? 0 : 10);
        if (b.piece_of_type[ROOK] & i) accum[c] += 5'00;
        if (b.piece_of_type[QUEEN] & i) accum[c] += 9'00;
    }
    return accum[WHITE] - accum[BLACK];
}