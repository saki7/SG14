#include <sg14/algorithm_ext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <ctime>
#include <iostream>
#include <memory>
#include <vector>

namespace {
    struct lifetest {
        static uint64_t construct;
        static uint64_t destruct;
        static uint64_t move;
        explicit lifetest() {
            ++construct;
        }
        lifetest(lifetest&&) noexcept {
            ++move;
        }
        ~lifetest() {
            ++destruct;
        }
        static void reset() {
            construct = 0;
            destruct = 0;
            move = 0;
        }
        static void test(uint64_t inconstruct, uint64_t indestruct, uint64_t inmove) {
            assert(construct == inconstruct);
            assert(destruct == indestruct);
            assert(move == inmove);
        }
    };
    uint64_t lifetest::construct = 0;
    uint64_t lifetest::destruct = 0;
    uint64_t lifetest::move = 0;
} // namespace

TEST(uninitialized_value_construct, Basic)
{
    for (int n = 0; n < 256; ++n) {
        auto m = (lifetest*)malloc(sizeof(lifetest) * n);
        lifetest::reset();

        sg14::uninitialized_value_construct(m, m + n);
        ASSERT_EQ(lifetest::construct, n);
        ASSERT_EQ(lifetest::destruct, 0);
        ASSERT_EQ(lifetest::move, 0);

        sg14::destroy(m, m + n);
        ASSERT_EQ(lifetest::construct, n);
        ASSERT_EQ(lifetest::destruct, n);
        ASSERT_EQ(lifetest::move, 0);

        free(m);
    }

    auto m = (int*)malloc(sizeof(int) * 5);
    sg14::uninitialized_value_construct(m, m + 5);
    assert(std::all_of(m, m + 5, [](int x) { return x == 0; }));
    free(m);
}

TEST(uninitialized_default_construct, Basic)
{
    for (int n = 0; n < 256; ++n) {
        auto mem1 = (lifetest*)malloc(sizeof(lifetest) * n);
        lifetest::reset();

        sg14::uninitialized_default_construct(mem1, mem1 + n);
        ASSERT_EQ(lifetest::construct, n);
        ASSERT_EQ(lifetest::destruct, 0);
        ASSERT_EQ(lifetest::move, 0);

        auto mem2 = (lifetest*)malloc(sizeof(lifetest) * n);
        sg14::uninitialized_move(mem1, mem1 + n, mem2);
        ASSERT_EQ(lifetest::construct, n);
        ASSERT_EQ(lifetest::destruct, 0);
        ASSERT_EQ(lifetest::move, n);

        sg14::destroy(mem2, mem2 + n);
        ASSERT_EQ(lifetest::construct, n);
        ASSERT_EQ(lifetest::destruct, n);
        ASSERT_EQ(lifetest::move, n);

        free(mem1);
        free(mem2);
    }
}
