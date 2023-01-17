//
// Created by leon on 2019-08-11.
//

#ifndef CHESSENGINE_TRANSPOSITION_TABLE_H
#define CHESSENGINE_TRANSPOSITION_TABLE_H

#include <cstdint>
#include <vector>
#include <chess/move.h>

enum tt_node_type {
    EXACT, ALPHA, BETA
};

struct tt_node {
    uint64_t hash;
    int depth;
    int value;
    tt_node_type type;
    chess::core::move bestmove;
};

class transposition_table {
    size_t size;
    std::vector<tt_node> nodes;

public:
    explicit transposition_table(size_t size) : size(size) {
        nodes.resize(size);
    }

    void save(uint64_t hash, int depth, int value, tt_node_type type, chess::core::move bestmove) {
        assert(bestmove != 0);
        assert(!(value < 31950 && value > 31000 && type == EXACT));
        assert(!(-value < 31950 && -value > 31000 && type == EXACT));
        auto& n = nodes[hash % size];
        if (n.hash == hash) {
            if (n.depth > depth) return;
            if (n.depth == depth && n.type == EXACT && type != EXACT) return;
        }
        n.hash = hash;
        n.depth = depth;
        n.value = value;
        n.type = type;
        n.bestmove = bestmove;
    }

    bool load(uint64_t hash, int depth, tt_node* n) {
        tt_node& m = nodes[hash % size];
        if (m.hash == hash) {
            *n = m;
            if (m.depth >= depth) {
                return true;
            }
        }
        return false;
    }

    bool load(uint64_t hash, int depth, int alpha, int beta, tt_node* n) {
        tt_node& m = nodes[hash % size];
        if (m.hash == hash) {
            *n = m;
            if (m.depth >= depth) {
                if (m.type == EXACT) return true;
                if (m.type == ALPHA && m.value <= alpha) {
                    //n->value = alpha;
                    return true;
                }
                if (m.type == BETA && m.value >= beta) {
                    //n->value = beta;
                    return true;
                }
            }
        }
        return false;
    }
};


#endif //CHESSENGINE_TRANSPOSITION_TABLE_H
