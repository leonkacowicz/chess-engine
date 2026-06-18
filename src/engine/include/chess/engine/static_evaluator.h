//
// Created by leon on 2019-11-02.
//

#ifndef CHESSENGINE_STATIC_EVALUATOR_H
#define CHESSENGINE_STATIC_EVALUATOR_H


#include <chess/engine/evaluator.h>

// Runtime-tunable scalar eval weights (for Texel tuning). The piece-square and
// king tables stay fixed for now; these scalars are the first tuning pass.
// Index material/mob by piece enum (PAWN..QUEEN); passed_* by the pawn's
// relative rank (1..6 used).
struct eval_params {
    int material[5];                 // P N B R Q
    int mob[5];                      // mobility weight per piece (N B R Q used)
    int doubled, isolated;
    int passed_mg[8], passed_eg[8];
    int bishop_pair, rook_open, rook_semi;
};
extern eval_params eval_w;           // defaults defined in static_evaluator.cpp

class static_evaluator : public evaluator {
public:
    int eval(const chess::core::board& b) override;
};


#endif //CHESSENGINE_STATIC_EVALUATOR_H
