#ifndef CHESSENGINE_TEST_COMMON_H
#define CHESSENGINE_TEST_COMMON_H
#include <gtest/gtest.h>
#include <random>

#define ASSERT_NOT_CONTAINS(X, Y) ASSERT_EQ(std::find(begin(X), end(X), Y), end(X))
#define ASSERT_CONTAINS(X, Y) ASSERT_NE(std::find(begin(X), end(X), Y), end(X))
#define ASSERT_CONTAINS_ALL(X, Y...) for(auto& e : Y) { ASSERT_NE(std::find(begin(X), end(X), e), end(X)); }

static std::vector<int> randomperm(int n) {
    std::vector<int> perm(n, 0);
    for (int i = 0; i < n; i++) perm[i] = i;
    std::shuffle(perm.begin(), perm.end(), std::random_device());
    return perm;
}

#endif //CHESSENGINE_TEST_COMMON_H
