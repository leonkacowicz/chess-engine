//
// Created by leon on 2019-11-02.
//

#ifndef CHESSENGINE_STATIC_EVALUATOR_H
#define CHESSENGINE_STATIC_EVALUATOR_H


#include <chess/engine/evaluator.h>

class static_evaluator : public evaluator {
public:
    int eval(const chess::core::board& b) override;
};


#endif //CHESSENGINE_STATIC_EVALUATOR_H
