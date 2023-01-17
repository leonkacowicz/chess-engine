//
// Created by leon on 7/28/19.
//

#ifndef CHESSENGINE_EVALUATOR_H
#define CHESSENGINE_EVALUATOR_H

#include <chess/board.h>

enum value : int {
    INF = 32001, MATE = 32000, DRAW = 0, CERTAIN_VICTORY = 10000
};

class evaluator {
public:
    virtual int eval(const chess::core::board& b);
};


#endif //CHESSENGINE_EVALUATOR_H
