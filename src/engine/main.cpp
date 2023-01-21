#include <iostream>
#include <string>
#include <memory>
#include <algorithm>
#include <fstream>
#include <chess/core.h>
#include <chess/move_gen.h>
#include <chess/game.h>
#include <chess/uci/uci.h>
#include <chess/engine/engine.h>
#include <chess/engine/static_evaluator.h>

using std::stringstream;
using std::string;
using namespace chess::core;

std::vector<string> split(const string& input, const string& delimiter = " ") {
    auto start = 0U;
    auto end = input.find(delimiter);
    auto delim_len = delimiter.length();
    std::vector<string> ret;
    while (end != std::string::npos) {
        ret.push_back(input.substr(start, end - start));
        start = end + delim_len;
        end = input.find(delimiter, start);
    }

    ret.push_back(input.substr(start, end));
    return ret;
}

void print_illegal_start_sequence_message(const std::vector<string>& words, int pos) {
    std::cerr << "ERROR: Illegal move sequence from startpos:";
    for (int i = 2; i <= pos; i++) {
        std::cerr << " " << words[i];
    }
    std::cerr << std::endl;
}

game handle_position_cmd(const std::vector<string>& words) {
    assert(words[0] == "position");
    if (words[1] == "startpos") {
        board b;
        b.set_initial_position();
        game g(b);
        auto iter = std::find(words.begin(), words.end(), "moves");
        if (iter == words.end()) return g;
        while (++iter != words.end()) {
            auto moves = move_gen(g.states.back().b).generate();
            auto move_found = std::find_if(moves.begin(), moves.end(), [&] (const move& m) {
                return to_long_move(m) == *iter;
            });
            if (move_found != moves.end()) {
                g.do_move(*move_found);
            } else {
                print_illegal_start_sequence_message(words, iter - words.begin() + 1);
                return g;
            }
        }
        return g;
    }
    throw std::runtime_error("position type not implemented");
}

int main()
{
    chess::core::init();
    static_evaluator eval;
    std::unique_ptr<engine> eng = std::make_unique<engine>(eval);
    board b;
    b.set_initial_position();
    game g(b);
    string input;
    while (std::getline(std::cin, input)) {
        auto words = split(input);

        if (words[0] == "uci") {
            std::cout << "id name chess-engine-name-tbd" << std::endl;
            std::cout << "id author Leon Kacowicz" << std::endl;
            std::cout << "uciok" << std::endl;
            std::cout.flush();
            continue;
        } else if (words[0] == "quit") {
            break;
        } else if (words[0] == "isready") {
            std::cout << "readyok" << std::endl;
            std::cout.flush();
            continue;
        } else if (words[0] == "position") {
            if (words[1] == "startpos") {
                g = handle_position_cmd(words);
            }
            continue;
        } else if (words[0] == "go") {
            chess::uci::cmd_go cmd(words);
            //uci_go_cmd cmd(words);
            if (move_gen(g.states.back().b).generate().empty()) {
                std::cerr << "no legal move found to be searched" << std::endl;
                std::cout << "bestmove (none)" << std::endl;
            } else {
                std::cout << "info calculating move for " << cmd.move_time.count() << "ms\n";
                if (cmd.max_depth > 0) eng->max_depth = cmd.max_depth;
                else eng->max_depth = 30;
                eng->timed_search(g, cmd.move_time);
            }
        } else if (words[0] == "print") {
            b.print();
        }
        else if (words[0] == "stop") {
            eng->time_over = true;
            std::cout << "bestmove " << to_long_move(eng->bestmove) << std::endl;
        }
    }
    return 0;
}