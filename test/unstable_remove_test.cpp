#include <sg14/algorithm_ext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <list>
#include <random>
#include <vector>

TEST(unstable_remove, Remove)
{
    int expected[] = {1,2,3};
    {
        std::vector<int> v = {42, 42, 1, 2, 3};
        auto it = sg14::unstable_remove(v.begin(), v.end(), 42);
        static_assert(std::is_same<decltype(sg14::unstable_remove(v.begin(), v.end(), 42)), decltype(v.begin())>::value, "");
        EXPECT_EQ(it, v.begin() + 3);
        EXPECT_TRUE(std::is_permutation(v.begin(), it, expected));
    }
    {
        std::list<int> v = {1, 42, 42, 2, 42, 3, 42, 42, 42};
        auto it = sg14::unstable_remove(v.begin(), v.end(), 42);
        EXPECT_EQ(std::distance(v.begin(), it), 3);
        EXPECT_TRUE(std::is_permutation(v.begin(), it, expected));
    }
    {
        std::vector<int> v = {42, 42};
        auto it = sg14::unstable_remove(v.begin(), v.end(), 42);
        EXPECT_EQ(it, v.begin());
        it = sg14::unstable_remove(v.begin(), v.begin(), 42);
        EXPECT_EQ(it, v.begin());
    }
}

TEST(unstable_remove, RemoveIf)
{
    int expected[] = {1,2,3};
    auto is42 = [](int x) { return x == 42; };
    {
        std::vector<int> v = {42, 42, 1, 2, 3};
        auto it = sg14::unstable_remove_if(v.begin(), v.end(), is42);
        static_assert(std::is_same<decltype(sg14::unstable_remove_if(v.begin(), v.end(), is42)), decltype(v.begin())>::value, "");
        EXPECT_EQ(it, v.begin() + 3);
        EXPECT_TRUE(std::is_permutation(v.begin(), it, expected));
    }
    {
        std::list<int> v = {1, 42, 42, 2, 42, 3, 42, 42, 42};
        auto it = sg14::unstable_remove_if(v.begin(), v.end(), is42);
        EXPECT_EQ(std::distance(v.begin(), it), 3);
        EXPECT_TRUE(std::is_permutation(v.begin(), it, expected));
    }
    {
        std::vector<int> v = {42, 42};
        auto it = sg14::unstable_remove_if(v.begin(), v.end(), is42);
        EXPECT_EQ(it, v.begin());
        it = sg14::unstable_remove_if(v.begin(), v.begin(), is42);
        EXPECT_EQ(it, v.begin());
    }
}

TEST(unstable_remove, NoUnneededMoves)
{
    struct MoveOnly {
        int i_;
        explicit MoveOnly(int i) : i_(i) {}
        MoveOnly(MoveOnly&&) { EXPECT_FALSE(true); }
        void operator=(MoveOnly&&) { EXPECT_FALSE(true); }
        bool operator==(const char*) const { return true; }
        static bool AlwaysTrue(const MoveOnly&) { return true; }
    };
    std::vector<MoveOnly> v;
    v.reserve(100);
    v.emplace_back(1);
    v.emplace_back(3);
    v.emplace_back(5);
    v.emplace_back(7);
    auto it = sg14::unstable_remove(v.begin(), v.end(), "always true");
    EXPECT_EQ(it, v.begin());
    it = sg14::unstable_remove_if(v.begin(), v.end(), MoveOnly::AlwaysTrue);
    EXPECT_EQ(it, v.begin());
}

TEST(unstable_remove, DequeExamples)
{
    std::mt19937 g;
    std::deque<unsigned int> original;
    for (int i = 0; i < 1000; ++i) {
        original.push_back(g());
    }
    auto pred = [](int x) { return (x % 2) == 0; };

    auto expected = original;
    expected.erase(std::remove_if(expected.begin(), expected.end(), pred), expected.end());

    {
        auto dq = original;
        dq.erase(sg14::unstable_remove_if(dq.begin(), dq.end(), pred), dq.end());

        EXPECT_EQ(dq.size(), expected.size());
        EXPECT_TRUE(std::is_permutation(dq.begin(), dq.end(), expected.begin(), expected.end()));
    }
    {
        auto dq = original;
        dq.erase(dq.begin(), std::find_if_not(dq.begin(), dq.end(), pred));
        dq.erase(sg14::unstable_remove_if(dq.begin(), dq.end(), pred), dq.end());

        EXPECT_EQ(dq.size(), expected.size());
        EXPECT_TRUE(std::is_permutation(dq.begin(), dq.end(), expected.begin(), expected.end()));
    }
    {
        auto dq = original;
        dq.erase(dq.begin(), sg14::unstable_remove_if(dq.rbegin(), dq.rend(), pred).base());

        EXPECT_EQ(dq.size(), expected.size());
        EXPECT_TRUE(std::is_permutation(dq.begin(), dq.end(), expected.begin(), expected.end()));
    }
    {
        auto dq = original;
        dq.erase(std::find_if_not(dq.rbegin(), dq.rend(), pred).base(), dq.end());
        dq.erase(dq.begin(), sg14::unstable_remove_if(dq.rbegin(), dq.rend(), pred).base());

        EXPECT_EQ(dq.size(), expected.size());
        EXPECT_TRUE(std::is_permutation(dq.begin(), dq.end(), expected.begin(), expected.end()));
    }
}
