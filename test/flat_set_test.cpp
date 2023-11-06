#include <sg14/flat_set.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <forward_list>
#include <functional>
#if __has_include(<memory_resource>)
#include <memory_resource>
#endif
#if __has_include(<ranges>)
#include <ranges>
#endif
#include <string>
#include <vector>

#if __cplusplus >= 201702L
#include <sg14/inplace_vector.h>
#endif

template<class T> struct flat_sett : testing::Test {};

using flat_sett_types = testing::Types<
    sg14::flat_set<int>                                           // basic
    , sg14::flat_set<int, std::greater<int>>                      // custom comparator
    , sg14::flat_set<int, std::greater<>>                         // transparent comparator
    , sg14::flat_set<int, std::less<int>, std::deque<int>>        // custom container
#if __cplusplus >= 201702L
    , sg14::flat_set<int, std::less<int>, sg14::inplace_vector<int, 1000>> // interop with sg14::inplace_vector
#endif
#if __cpp_lib_memory_resource >= 201603L
    , sg14::flat_set<int, std::less<int>, std::pmr::vector<int>>  // pmr container
#endif
>;
TYPED_TEST_SUITE(flat_sett, flat_sett_types);

namespace {

struct AmbiguousEraseWidget {
    friend bool operator<(const AmbiguousEraseWidget& a, const AmbiguousEraseWidget& b) {
        return a.s_ < b.s_;
    }

    using iterator = sg14::flat_set<AmbiguousEraseWidget>::iterator;
    using const_iterator = sg14::flat_set<AmbiguousEraseWidget>::const_iterator;

    explicit AmbiguousEraseWidget(const char *s) : s_(s) {}
    AmbiguousEraseWidget(iterator) : s_("notfound") {}
    AmbiguousEraseWidget(const_iterator) : s_("notfound") {}

private:
    std::string s_;
};

} // namespace

TEST(flat_set, AmbiguousErase)
{
    sg14::flat_set<AmbiguousEraseWidget> fs;
    fs.emplace("a");
    fs.emplace("b");
    fs.emplace("c");
    EXPECT_TRUE(fs.size() == 3);
    fs.erase(AmbiguousEraseWidget("a"));  // calls erase(const Key&)
    EXPECT_TRUE(fs.size() == 2);
    fs.erase(fs.cbegin());                // calls erase(const_iterator)
    EXPECT_TRUE(fs.size() == 1);
#if __cplusplus >= 201703L
    fs.erase(fs.begin());                 // calls erase(iterator)
    EXPECT_TRUE(fs.size() == 0);
#endif
}

TEST(flat_set, ExtractDoesntSwap)
{
#if __cpp_lib_memory_resource >= 201603
    // This test fails if extract() is implemented in terms of swap().
    {
        std::pmr::monotonic_buffer_resource mr;
        std::pmr::polymorphic_allocator<int> a(&mr);
        sg14::flat_set<int, std::less<int>, std::pmr::vector<int>> fs({1, 2}, a);
        std::pmr::vector<int> v = std::move(fs).extract();
        EXPECT_TRUE(v.get_allocator() == a);
        EXPECT_TRUE(fs.empty());
    }
#endif

    // Sanity-check with std::allocator, even though this can't fail.
    {
        std::allocator<int> a;
        sg14::flat_set<int, std::less<int>, std::vector<int>> fs({1, 2}, a);
        std::vector<int> v = std::move(fs).extract();
        EXPECT_TRUE(v.get_allocator() == a);
        EXPECT_TRUE(fs.empty());
    }
}

namespace {

struct ThrowingSwapException {};

struct ComparatorWithThrowingSwap {
    std::function<bool(int, int)> cmp_;
    static bool please_throw;
    ComparatorWithThrowingSwap(std::function<bool(int, int)> f) : cmp_(f) {}
    friend void swap(ComparatorWithThrowingSwap& a, ComparatorWithThrowingSwap& b) {
        if (please_throw)
            throw ThrowingSwapException();
        a.cmp_.swap(b.cmp_);
    }
    bool operator()(int a, int b) const {
        return cmp_(a, b);
    }
};
bool ComparatorWithThrowingSwap::please_throw = false;

} // namespace

TEST(flat_set, ThrowingSwapDoesntBreakInvariants)
{
    using std::swap;
    sg14::flat_set<int, ComparatorWithThrowingSwap> fsx({1,2,3}, ComparatorWithThrowingSwap(std::less<int>()));
    sg14::flat_set<int, ComparatorWithThrowingSwap> fsy({4,5,6}, ComparatorWithThrowingSwap(std::greater<int>()));

    if (true) {
        ComparatorWithThrowingSwap::please_throw = false;
        swap(fsx, fsy);  // should swap both the comparators and the containers
        fsx.insert(7);
        fsy.insert(8);
        std::vector<int> expected_fsx = {7, 6, 5, 4};
        std::vector<int> expected_fsy = {1, 2, 3, 8};
        EXPECT_TRUE(expected_fsx.size() == fsx.size());
        EXPECT_TRUE(std::equal(expected_fsx.begin(), expected_fsx.end(), fsx.begin()));
        EXPECT_TRUE(expected_fsy.size() == fsy.size());
        EXPECT_TRUE(std::equal(expected_fsy.begin(), expected_fsy.end(), fsy.begin()));
    }

    // However, if ComparatorWithThrowingSwap::please_throw were
    // set to `true`, then flat_set's behavior would be undefined.
}

TEST(flat_set, VectorBool)
{
#if __cplusplus >= 201402L  // C++11 doesn't support vector<bool>::emplace
    using FS = sg14::flat_set<bool>;
    FS fs;
    auto it_inserted = fs.emplace(true);
    EXPECT_TRUE(it_inserted.second);
    auto it = it_inserted.first;
    EXPECT_TRUE(it == fs.begin());
    EXPECT_TRUE(fs.size() == 1);
    it = fs.emplace_hint(it, false);
    EXPECT_TRUE(it == fs.begin());
    EXPECT_TRUE(fs.size() == 2);
    auto count = fs.erase(false);
    EXPECT_TRUE(count == 1);
    EXPECT_TRUE(fs.size() == 1);
    it = fs.erase(fs.begin());
    EXPECT_TRUE(fs.empty());
    EXPECT_TRUE(it == fs.begin());
    EXPECT_TRUE(it == fs.end());
#endif
}

namespace {

struct VectorBoolEvilComparator {
    bool operator()(bool a, bool b) const {
        return a < b;
    }
    template<class T, class U>
    bool operator()(T a, U b) const {
        static_assert(sizeof(T) < 0, "should never instantiate this call operator");
        return a < b;
    }
};

} // namespace

TEST(flat_set, VectorBoolEvilComparator)
{
    using FS = sg14::flat_set<bool, VectorBoolEvilComparator>;
    FS fs;
    (void)fs.lower_bound(true);
    (void)fs.upper_bound(true);
    (void)fs.equal_range(true);
    const FS& cfs = fs;
    (void)cfs.lower_bound(true);
    (void)cfs.upper_bound(true);
    (void)cfs.equal_range(true);
}

namespace {

struct InstrumentedWidget {
    static int move_ctors, copy_ctors;
    InstrumentedWidget(const char *s) : s_(s) {}
    InstrumentedWidget(InstrumentedWidget&& o) : s_(std::move(o.s_)) { move_ctors += 1; }
    InstrumentedWidget(const InstrumentedWidget& o) : s_(o.s_) { copy_ctors += 1; }
    InstrumentedWidget& operator=(InstrumentedWidget&&) = default;
    InstrumentedWidget& operator=(const InstrumentedWidget&) = default;

    friend bool operator<(const InstrumentedWidget& a, const InstrumentedWidget& b) {
        return a.s_ < b.s_;
    }

private:
    std::string s_;
};
int InstrumentedWidget::move_ctors = 0;
int InstrumentedWidget::copy_ctors = 0;

} // namespace

TEST(flat_set, MoveOperationsPilferOwnership)
{
    using FS = sg14::flat_set<InstrumentedWidget>;
    InstrumentedWidget::move_ctors = 0;
    InstrumentedWidget::copy_ctors = 0;
    FS fs;
    fs.insert(InstrumentedWidget("abc"));
    EXPECT_TRUE(InstrumentedWidget::move_ctors == 1);
    EXPECT_TRUE(InstrumentedWidget::copy_ctors == 0);

    fs.emplace(InstrumentedWidget("def")); fs.erase("def");  // poor man's reserve()
    InstrumentedWidget::copy_ctors = 0;
    InstrumentedWidget::move_ctors = 0;

    fs.emplace("def");  // is still not directly emplaced; a temporary is created to find()
    EXPECT_TRUE(InstrumentedWidget::move_ctors == 1);
    EXPECT_TRUE(InstrumentedWidget::copy_ctors == 0);
    InstrumentedWidget::move_ctors = 0;

    FS fs2 = std::move(fs);  // should just transfer buffer ownership
    EXPECT_TRUE(InstrumentedWidget::move_ctors == 0);
    EXPECT_TRUE(InstrumentedWidget::copy_ctors == 0);

    fs = std::move(fs2);  // should just transfer buffer ownership
    EXPECT_TRUE(InstrumentedWidget::move_ctors == 0);
    EXPECT_TRUE(InstrumentedWidget::copy_ctors == 0);

    FS fs3(fs, std::allocator<InstrumentedWidget>());
    EXPECT_TRUE(InstrumentedWidget::move_ctors == 0);
    EXPECT_TRUE(InstrumentedWidget::copy_ctors == 2);
    InstrumentedWidget::copy_ctors = 0;

    FS fs4(std::move(fs), std::allocator<InstrumentedWidget>());  // should just transfer buffer ownership
    EXPECT_TRUE(InstrumentedWidget::move_ctors == 0);
    EXPECT_TRUE(InstrumentedWidget::copy_ctors == 0);
}

TYPED_TEST(flat_sett, Construction)
{
    using FS = TypeParam;

    static_assert(std::is_same<int, typename FS::key_type>::value, "");
    static_assert(std::is_same<int, typename FS::value_type>::value, "");
    using Compare = typename FS::key_compare;
    std::vector<int> vec = {1, 3, 5};
    if (true) {
        FS fs;  // default constructor
        fs = {1, 3, 5};  // assignment operator
        EXPECT_TRUE(std::is_sorted(fs.begin(), fs.end(), fs.key_comp()));
    }
    if (true) {
        FS fs = {1, 3, 1, 5, 3};
        EXPECT_TRUE(fs.size() == 3);  // assert that uniqueing takes place
        std::vector<int> vec2 = {1, 3, 1, 5, 3};
        FS fs2(vec2.begin(), vec2.end());
        EXPECT_TRUE(fs2.size() == 3); // assert that uniqueing takes place
    }
    for (auto&& fs : {
#if __cpp_lib_ranges >= 201911L && __cpp_lib_ranges_to_container >= 202202L
        FS(std::from_range, vec),
        FS(std::from_range, vec, Compare()),
#endif
        FS({1, 3, 5}),
        FS({1, 3, 5}, Compare()),
        FS(vec.begin(), vec.end()),
        FS(vec.begin(), vec.end(), Compare()),
        FS(vec.rbegin(), vec.rend()),
        FS(vec.rbegin(), vec.rend(), Compare()),
    }) {
        auto cmp = fs.key_comp();
        EXPECT_TRUE(std::is_sorted(fs.begin(), fs.end(), cmp));
        EXPECT_TRUE(fs.find(0) == fs.end());
        EXPECT_TRUE(fs.find(1) != fs.end());
        EXPECT_TRUE(fs.find(2) == fs.end());
        EXPECT_TRUE(fs.find(3) != fs.end());
        EXPECT_TRUE(fs.find(4) == fs.end());
        EXPECT_TRUE(fs.find(5) != fs.end());
        EXPECT_TRUE(fs.find(6) == fs.end());
    }
    if (std::is_sorted(vec.begin(), vec.end(), Compare())) {
        for (auto&& fs : {
            FS(sg14::sorted_unique, vec.begin(), vec.end()),
            FS(sg14::sorted_unique, vec.begin(), vec.end(), Compare()),
            FS(sg14::sorted_unique, {1, 3, 5}),
            FS(sg14::sorted_unique, {1, 3, 5}, Compare()),
        }) {
            auto cmp = fs.key_comp();
            EXPECT_TRUE(std::is_sorted(fs.begin(), fs.end(), cmp));
        }
    }
}

TYPED_TEST(flat_sett, SpecialMembers)
{
    using FS = TypeParam;

    static_assert(std::is_default_constructible<FS>::value, "");
    static_assert(std::is_nothrow_move_constructible<FS>::value == std::is_nothrow_move_constructible<typename FS::container_type>::value, "");
    static_assert(std::is_copy_constructible<FS>::value, "");
    static_assert(std::is_copy_assignable<FS>::value, "");
    static_assert(std::is_move_assignable<FS>::value, "");
    static_assert(std::is_nothrow_destructible<FS>::value, "");
}

TEST(flat_set, Iterators)
{
    sg14::flat_set<int> fs = {3, 1, 4, 2};
    EXPECT_EQ(fs.begin(), fs.cbegin());
    EXPECT_EQ(fs.end(), fs.cend());
    EXPECT_EQ(fs.rbegin(), fs.crbegin());
    EXPECT_EQ(fs.rend(), fs.crend());
    std::vector<int> expected = {1, 2, 3, 4};
    EXPECT_TRUE(std::equal(fs.begin(), fs.end(), expected.begin(), expected.end()));
    EXPECT_TRUE(std::equal(fs.cbegin(), fs.cend(), expected.begin(), expected.end()));
    EXPECT_TRUE(std::equal(fs.rbegin(), fs.rend(), expected.rbegin(), expected.rend()));
    EXPECT_TRUE(std::equal(fs.crbegin(), fs.crend(), expected.rbegin(), expected.rend()));
}

TEST(flat_set, ContainerAccessors)
{
    // The C++23 flat_set provides only `ctr = fs.extract()` and `fs.replace(ctr)`,
    // but the SG14 version provides direct access to the container in place.
    sg14::flat_set<int> fs = {3, 1, 4, 2};
    const auto& cfs = fs;
    static_assert(std::is_same<decltype(fs.keys()), const std::vector<int>&>::value, "");
    static_assert(std::is_same<decltype(cfs.keys()), const std::vector<int>&>::value, "");
    std::vector<int> expected_keys = {1, 2, 3, 4};
    EXPECT_EQ(fs.keys(), expected_keys);
    EXPECT_EQ(cfs.keys(), expected_keys);
}

TEST(flat_set, ExtractReplace)
{
    std::vector<int> expected_keys = {1, 2, 7, 8};
    std::vector<int> rvalue_keys = {2, 7, 1, 8};

    sg14::flat_set<int> fs;
    fs.replace(std::move(rvalue_keys));
    EXPECT_TRUE(rvalue_keys.empty());
    EXPECT_EQ(fs.keys(), expected_keys);
    fs.replace(expected_keys); // lvalue
    EXPECT_EQ(fs.keys(), expected_keys);

    rvalue_keys = {1, 2, 7, 8};
    fs.replace(sg14::sorted_unique, std::move(rvalue_keys));
    EXPECT_TRUE(rvalue_keys.empty());
    EXPECT_EQ(fs.keys(), expected_keys);
    fs.replace(sg14::sorted_unique, expected_keys); // lvalue
    EXPECT_EQ(fs.keys(), expected_keys);
}

TEST(flat_set, InsertMulti)
{
    {
        sg14::flat_set<int> fs;
        std::vector<int> v = {3,1,4,1,5};
        fs.insert(v.begin(), v.end());
        EXPECT_EQ(fs, (sg14::flat_set<int>{1,3,4,5}));
        fs = {4, 5};
        std::forward_list<int> lst = {3,1,4,1,5};
        fs.insert(lst.begin(), lst.end());
        EXPECT_EQ(fs, (sg14::flat_set<int>{1,3,4,5}));
    }
    {
        sg14::flat_set<int> fs;
        std::vector<int> v = {1,3,4,5};
        fs.insert(sg14::sorted_unique, v.begin(), v.end());
        EXPECT_EQ(fs, (sg14::flat_set<int>{1,3,4,5}));
        fs = {4, 5};
        std::forward_list<int> lst = {3,5,7};
        fs.insert(sg14::sorted_unique, lst.begin(), lst.end());
        EXPECT_EQ(fs, (sg14::flat_set<int>{3,4,5,7}));
    }
}

TEST(flat_set, InsertRange)
{
#if __cpp_lib_ranges >= 201911L && __cpp_lib_ranges_to_container >= 202202L
    {
        sg14::flat_set<int> fs;
        std::vector<int> v = {3,1,4,1,5};
        fs.insert_range(v);
        EXPECT_EQ(fs, (sg14::flat_set<int>{1,3,4,5}));
        fs = {4, 5};
        fs.insert_range(v | std::views::take_while([](int x) { return x < 9; }));
        EXPECT_EQ(fs, (sg14::flat_set<int>{1,3,4,5}));
    }
    {
        sg14::flat_set<int> fs;
        std::vector<int> v = {1,2,3,4};
        fs.insert_range(sg14::sorted_unique, v);
        EXPECT_EQ(fs, (sg14::flat_set<int>{1,2,3,4}));
        fs = {3, 4};
        fs.insert_range(sg14::sorted_unique, v | std::views::transform([](int x) { return x * 2; }));
        EXPECT_EQ(fs, (sg14::flat_set<int>{2,3,4,6,8}));
    }
#endif // __cpp_lib_ranges >= 201911L && __cpp_lib_ranges_to_container >= 202202L
}
