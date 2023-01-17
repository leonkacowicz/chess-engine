//
// Created by leon on 2019-07-29.
//

#include <gtest/gtest.h>
#include <chess/square.h>
#include <chess/engine/engine.h>
#include <chess/game.h>
#include <chess/fen.h>
#include <chess/engine/static_evaluator.h>
#include <chrono>

using namespace chess::core;

TEST(engine_test, engine_call) {
    using namespace std::chrono_literals;
    board b;
    b.set_initial_position();
    game g(b);
    g.do_move(get_move(SQ_E2, SQ_E4));
    static_evaluator eval;
    engine e(eval);
    for (int i = 1; i < 200; i++) {
        std::cout << i << ".\n\n";
        auto m = e.timed_search(g, 50ms);
        if (m == null_move) break;
        g.do_move(m);
        g.states.back().b.print();
    }
}

TEST(engine_test, engine_should_find_mate_in_one) {
    board b = fen::board_from_fen("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - -");
    b.print();
    static_evaluator eval;
    engine e(eval);

    auto g = game(b);
    auto m = e.search_iterate(g);

    ASSERT_EQ(m.first, get_move(SQ_D1, SQ_D8));
}

TEST(engine_test, engine_should_find_mate_in_one_b) {
    board b = chess::core::fen::board_from_fen("r1qr1b2/1R3pkp/3p2pN/ppnPp1Q1/bn2P3/4P2P/PBBP2P1/5RK1 w - e6");
    static_evaluator eval;
    engine e(eval);

    auto g = game(b);
    auto m = e.search_iterate(g);

    ASSERT_EQ(m.first, get_move(SQ_D5, SQ_E6));
}

TEST(engine_test, engine_should_find_mate_in_3ply) {
    board b = chess::core::fen::board_from_fen("r1qr1b2/1R3pkp/3p2pN/ppnPp1Q1/bn2P3/4P2P/PBBP2P1/5RK1 w - -");
    static_evaluator eval;
    engine e(eval);

    auto g = game(b);
    auto m = e.search_iterate(g);

    ASSERT_EQ(m.first, get_move(SQ_F1, SQ_F7));
}

TEST(engine_test, engine_should_find_mate_in_5ply) {
    board b = chess::core::fen::board_from_fen("r1qr1b2/1R3pkp/3p2pN/ppnPp1Q1/bn2P3/4P2P/PBBP1PP1/5RK1 w - - 0 1");
    static_evaluator eval;
    engine e(eval);

    auto g = game(b);
    auto m = e.search_iterate(g);

    ASSERT_EQ(m.first, get_move(SQ_B7, SQ_F7));
}

TEST(engine_test, engine_should_find_mate_in_8ply) {
    board b = chess::core::fen::board_from_fen("8/5p2/2p5/2p2kpK/2R1p1N1/3NP3/2P5/5B2 w - - 0 1");
    static_evaluator eval;
    engine e(eval);

    auto g = game(b);
    auto m = e.search_iterate(g);

    ASSERT_EQ(m.first, get_move(SQ_D3, SQ_C5));
}

TEST(engine_test, engine_should_find_mate_in_9ply) {
    board b = fen::board_from_fen("1Q6/N2k4/1p1pp3/1rp1p3/1b1p4/2p5/8/5K2 w - - 0 1");

    static_evaluator eval;
    engine e(eval);

    auto g = game(b);
    auto m = e.search_iterate(g);

    ASSERT_EQ(m.first, get_move(SQ_A7, SQ_C6));

}

TEST(engine_test, engine_should_find_mate_in_14ply) {
    board b = fen::board_from_fen("1k2N3/p7/P2Np3/2Pp1p2/2p4p/7P/1q3PP1/3R2KR w - - 0 1");

    static_evaluator eval;
    engine e(eval);

    auto g = game(b);
    using std::chrono_literals::operator""ms;
    move m = e.timed_search(g, 2000ms);

    ASSERT_EQ(m, get_move(SQ_C5, SQ_C6));
}

class zero_eval : public evaluator {
    int eval(const board& b) override {
        return 0;
    }
};

TEST(engine_test, engine_should_stick_to_mate1) {
    //1. e4 g6 2. Bc4 d5 3. Bxd5 Qxd5 4. d4 Qxe4+ 5. Kf1 a6
    //6. Qe2 Qxe2+ 7. Nxe2 e5 8. Bf4 Bb4 9. Bxe5 h5 10. Nb1c3 Ne7
    //11. Nb5 axb5 12. f4 Rh7 13. Nc3 Bxc3 14. Bd6 Nb8c6 15. Bxe7 Nxe7
    //16. Re1 Bxb2 17. Rxe7+ Kxe7 18. f5 Ra5 19. g4 Kf8 20. h3 Rg7
    //21. Rh2 Bc3 22. Re2 Bb4 23. Re8+ Kxe8 24. Kf2 hxg4 25. hxg4 Rxa2
    //26. Kg1 Ra1+ 27. Kf2 Ra2 28. Kf3 Kf8 29. c4 Ra3+ 30. Ke2 Ra2+
    //31. Kf1 Rh7 32. Kg1 Rc2 33. cxb5 Rh4 34. b6 Rxg4+ 35. Kf1 Re4
    //36. f6 Re1#
    //0-1

    board b = chess::core::fen::board_from_fen("2b2k2/1pp2p2/1P3Pp1/8/1b1Pr3/8/2r5/5K2 b - - 0 36");
    zero_eval eval;
    engine e(eval);

    auto g = game(b);
    auto m = e.search_iterate(g);
    ASSERT_EQ(m.first, get_move(SQ_E4, SQ_E1));
}

//
TEST(engine_test, engine_should_stick_to_mate2) {
    //1. e4 g6 2. Bc4 d5 3. Bxd5 Qxd5 4. d4 Qxe4+ 5. Kf1 a6
    //6. Qe2 Qxe2+ 7. Nxe2 e5 8. Bf4 Bb4 9. Bxe5 h5 10. Nb1c3 Ne7
    //11. Nb5 axb5 12. f4 Rh7 13. Nc3 Bxc3 14. Bd6 Nb8c6 15. Bxe7 Nxe7
    //16. Re1 Bxb2 17. Rxe7+ Kxe7 18. f5 Ra5 19. g4 Kf8 20. h3 Rg7
    //21. Rh2 Bc3 22. Re2 Bb4 23. Re8+ Kxe8 24. Kf2 hxg4 25. hxg4 Rxa2
    //26. Kg1 Ra1+ 27. Kf2 Ra2 28. Kf3 Kf8 29. c4 Ra3+ 30. Ke2 Ra2+
    //31. Kf1 Rh7 32. Kg1 Rc2 33. cxb5 Rh4 34. b6 Rxg4+ 35. Kf1 Re4
    //36. f6 Re1#
    //0-1

    board b = chess::core::fen::board_from_fen("2b2k2/1pp2p2/1P4p1/5P2/1b1P2r1/8/2r5/5K2 b - - 1 35");
    zero_eval eval;
    engine e(eval);

    auto g = game(b);
    auto m = e.search_iterate(g);
    ASSERT_TRUE(m.first == get_move(SQ_G4, SQ_E4) || m.first == get_move(SQ_G4, SQ_D4));
}

TEST(engine_test, engine_should_find_mate_in_7ply) {
//    8   ◦ ◦ ♕ ◦ ◦ ◦ ◦ ◦
//    7   ◦ ◦ ◦ ◦ ◦ ♟ ◦ ◦
//    6   ◦ ◦ ◦ ◦ ◦ ♙ ◦ ◦
//    5   ◦ ◦ ◦ ◦ ◦ ◦ ◦ ◦
//    4   ◦ ◦ ◦ ◦ ◦ ◦ ◦ ◦
//    3   ♙ ♚ ◦ ◦ ◦ ◦ ◦ ◦
//    2   ◦ ◦ ◦ ◦ ◦ ◦ ◦ ◦
//    1   ◦ ◦ ♔ ◦ ◦ ◦ ◦ ◦
//
//    a◦b◦c◦d◦e◦f◦g◦h


    board b = fen::board_from_fen("2Q5/5p2/5P2/8/8/Pk6/8/2K5 w - - 0 1");
    game g(b);
    zero_eval eval;
    engine e(eval);
    auto m = e.search_iterate(g);
    ASSERT_TRUE(m.first == get_move(SQ_C8, SQ_C5));
}

TEST(engine_test, static_eval_should_be_simmetric) {
    auto b = fen::board_from_fen("kq6/p7/8/7N/8/8/PP6/4K2R w K--- - 0 1");
    static_evaluator e;
    ASSERT_EQ(e.eval(b), -e.eval(b.flip_colors()));
}