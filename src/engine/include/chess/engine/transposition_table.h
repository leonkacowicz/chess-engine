//
// Created by leon on 2019-08-11.
//

#ifndef CHESSENGINE_TRANSPOSITION_TABLE_H
#define CHESSENGINE_TRANSPOSITION_TABLE_H

#include <cstdint>
#include <vector>
#include <chess/move.h>

enum tt_node_type : uint8_t {
    EXACT, ALPHA, BETA
};

// Repacked to 24 bytes so the generation tag fits with no growth: depth is
// bounded by INF (32001) so int16_t suffices, and the enum is uint8_t-backed.
struct tt_node {
    uint64_t hash;
    int value;
    chess::core::move bestmove;
    int16_t depth;
    uint16_t generation;
    tt_node_type type;
};

class transposition_table {
    size_t size;
    std::vector<tt_node> nodes;
    uint16_t generation = 0;   // bumped once per search (per move)

public:
    explicit transposition_table(size_t size) : size(size) {
        nodes.resize(size);
    }

    // Advance to a new search generation. Entries from older generations become
    // freely replaceable, so stale deep entries from earlier moves can no longer
    // ossify slots against the current search (while still being usable on load).
    void new_search() {
        ++generation;
    }

    void save(uint64_t hash, int depth, int value, tt_node_type type, chess::core::move bestmove) {
        assert(bestmove != 0);
        assert(!(value < 31950 && value > 31000 && type == EXACT));
        assert(!(-value < 31950 && -value > 31000 && type == EXACT));
        auto& n = nodes[hash % size];
        if (n.hash == hash) {
            // Same position: keep the deeper / EXACT result (still valid), but
            // refresh its generation so it counts as current.
            if (n.depth > depth) { n.generation = generation; return; }
            if (n.depth == depth && n.type == EXACT && type != EXACT) { n.generation = generation; return; }
        } else if (n.generation == generation) {
            // Different position from the CURRENT search: depth-preferred, so a
            // shallow store can't evict a deep entry we may still need.
            if (n.depth > depth) return;
        }
        // else: different position from an OLDER search (stale) -> replace freely.
        n.hash = hash;
        n.depth = (int16_t) depth;
        n.value = value;
        n.type = type;
        n.bestmove = bestmove;
        n.generation = generation;
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
