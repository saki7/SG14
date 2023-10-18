#include <sg14/ring_span.h>

#include <gtest/gtest.h>

#include <numeric>
#include <string>
#include <vector>

TEST(ring_span, Basic)
{
    int a[5];
    int b[5];
    auto q = sg14::ring_span<int>(a, a+5);

    q.push_back(7);
    q.push_back(3);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.front(), 7);

    q.pop_front();
    EXPECT_EQ(q.size(), 1u);

    q.push_back(18);
    auto q3 = std::move(q);
    EXPECT_EQ(q3.front(), 3u);
    EXPECT_EQ(q3.back(), 18u);

    auto q5 = std::move(q3);
    EXPECT_EQ(q5.front(), 3);
    EXPECT_EQ(q5.back(), 18);
    EXPECT_EQ(q5.size(), 2u);

    q5.pop_front();
    q5.pop_front();
    EXPECT_TRUE(q5.empty());

    auto q6 = sg14::ring_span<int>(b, b+5);
    q6.push_back(6);
    q6.push_back(7);
    q6.push_back(8);
    q6.push_back(9);
    q6.emplace_back(10);
    q6.swap(q5);
    EXPECT_TRUE(q6.empty());
    EXPECT_EQ(q5.size(), 5u);
    EXPECT_EQ(q5.front(), 6);
    EXPECT_EQ(q5.back(), 10);
}

TEST(ring_span, Filter)
{
    double a[3];
    sg14::ring_span<double> buffer(a, a+3);
    buffer.push_back(1.0);
    buffer.push_back(2.0);
    buffer.push_back(3.0);
    buffer.push_back(5.0);
    EXPECT_EQ(buffer.front(), 2.0);
    constexpr double filter_coefficients[3] = {0.25, 0.5, 0.25};
    buffer.push_back(7);
    EXPECT_EQ(std::inner_product(buffer.begin(), buffer.end(), filter_coefficients, 0.0), 5.0);
}

TEST(ring_span, IteratorRegression)
{
    double a[3];
    auto r = sg14::ring_span<double>(a, a+3);
    r.push_back(1.0);
    decltype(r)::iterator it = r.end();
    decltype(r)::const_iterator cit = r.end();  // test conversion from non-const to const
    EXPECT_EQ(it, cit);  // test comparison of const and non-const
    EXPECT_EQ(it + 0, it);
    EXPECT_EQ(it - 1, r.begin());
    EXPECT_EQ(cit + 0, cit);
    EXPECT_EQ(cit - 1, r.cbegin());
    EXPECT_EQ(it - cit, 0);
    EXPECT_EQ(cit - r.begin(), 1);

    double b[4];
    auto r2 = sg14::ring_span<double>(b, b+4);
    swap(r, r2);  // test existence of ADL swap

    // Set up the ring for the TEST_OP tests below.
    r = sg14::ring_span<double>(a, a+3, a, 2);
    EXPECT_EQ(r.size(), 2u);

#define TEST_OP(op, a, b, c) \
    EXPECT_TRUE(a(r.begin() op r.end())); \
    EXPECT_TRUE(b(r.end() op r.begin())); \
    EXPECT_TRUE(c(r.begin() op r.begin()))
#define _
    TEST_OP(==, !, !, _);
    TEST_OP(!=, _, _, !);
    TEST_OP(<, _, !, !);
    TEST_OP(<=, _, !, _);
    TEST_OP(>, !, _, !);
    TEST_OP(>=, !, _, _);
#undef _
#undef TEST_OP
}

TEST(ring_span, CopyPopper)
{
    std::vector<std::string> v = { "quick", "brown", "fox" };
    sg14::ring_span<std::string, sg14::copy_popper<std::string>> r(v.begin(), v.end(), {"popped"});
    r.emplace_back("slow");
    EXPECT_EQ(v, (std::vector<std::string>{"slow", "brown", "fox"}));
    r.emplace_back("red");
    EXPECT_EQ(v, (std::vector<std::string>{"slow", "red", "fox"}));
    std::string result = r.pop_front();
    EXPECT_EQ(v, (std::vector<std::string>{"popped", "red", "fox"}));
    EXPECT_EQ(result, "slow");
}

TEST(ring_span, ReverseIterator)
{
    int a[3];
    auto r = sg14::ring_span<int>(a, a+3);
    const auto c = sg14::ring_span<int>(a, a+3);

    r.push_back(1);
    r.push_back(2);
    r.push_back(3);
    r.push_back(4);
    auto v = std::vector<double>(3);

    std::copy(r.begin(), r.end(), v.begin());
    EXPECT_EQ(v, (std::vector<double>{2,3,4}));

    std::copy(r.cbegin(), r.cend(), v.begin());
    EXPECT_EQ(v, (std::vector<double>{2,3,4}));

    std::copy(r.rbegin(), r.rend(), v.begin());
    EXPECT_EQ(v, (std::vector<double>{4,3,2}));

    std::copy(r.crbegin(), r.crend(), v.begin());
    EXPECT_EQ(v, (std::vector<double>{4,3,2}));

    static_assert(std::is_same<decltype(r.begin()), decltype(r)::iterator>::value, "");
    static_assert(std::is_same<decltype(c.begin()), decltype(r)::const_iterator>::value, "");
    static_assert(std::is_same<decltype(r.cbegin()), decltype(r)::const_iterator>::value, "");
    static_assert(std::is_same<decltype(c.cbegin()), decltype(r)::const_iterator>::value, "");

    static_assert(std::is_same<decltype(r.end()), decltype(r)::iterator>::value, "");
    static_assert(std::is_same<decltype(c.end()), decltype(r)::const_iterator>::value, "");
    static_assert(std::is_same<decltype(r.cend()), decltype(r)::const_iterator>::value, "");
    static_assert(std::is_same<decltype(c.cend()), decltype(r)::const_iterator>::value, "");

    static_assert(std::is_same<decltype(r.rbegin()), decltype(r)::reverse_iterator>::value, "");
    static_assert(std::is_same<decltype(c.rbegin()), decltype(r)::const_reverse_iterator>::value, "");
    static_assert(std::is_same<decltype(r.crbegin()), decltype(r)::const_reverse_iterator>::value, "");
    static_assert(std::is_same<decltype(c.crbegin()), decltype(r)::const_reverse_iterator>::value, "");

    static_assert(std::is_same<decltype(r.rend()), decltype(r)::reverse_iterator>::value, "");
    static_assert(std::is_same<decltype(c.rend()), decltype(r)::const_reverse_iterator>::value, "");
    static_assert(std::is_same<decltype(r.crend()), decltype(r)::const_reverse_iterator>::value, "");
    static_assert(std::is_same<decltype(c.crend()), decltype(r)::const_reverse_iterator>::value, "");
}
