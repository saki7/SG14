
// You can omit this line, but then we'll spend most of our time in std::next.
#define PLF_HIVE_RANDOM_ACCESS_ITERATORS 1

#define PLF_HIVE_P2596 0
#include <sg14/plf_hive.h>
#undef PLF_HIVE_P2596

#undef PLF_HIVE_H
#define plf p2596
#define PLF_HIVE_P2596 1
#include <sg14/plf_hive.h>
#undef PLF_HIVE_P2596
#undef plf

#undef PLF_HIVE_H
#define plf matt
#include "../../plf_hive/plf_hive.h"
#undef plf

#include <benchmark/benchmark.h>

struct xoshiro256ss {
    using u64 = unsigned long long;
    u64 s[4] {};

    static constexpr u64 splitmix64(u64& x) {
        u64 z = (x += 0x9e3779b97f4a7c15uLL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9uLL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebuLL;
        return z ^ (z >> 31);
    }

    constexpr explicit xoshiro256ss() : xoshiro256ss(0) {}

    constexpr explicit xoshiro256ss(u64 seed) {
        s[0] = splitmix64(seed);
        s[1] = splitmix64(seed);
        s[2] = splitmix64(seed);
        s[3] = splitmix64(seed);
    }

    using result_type = u64;
    static constexpr u64 min() { return 0; }
    static constexpr u64 max() { return u64(-1); }

    static constexpr u64 rotl(u64 x, int k) {
        return (x << k) | (x >> (64 - k));
    }

    constexpr u64 operator()() {
        u64 result = rotl(s[1] * 5, 7) * 9;
        u64 t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }
};

#define N 1000

#if N < 256
static void BM_PlfStackIssue1_MattSmart(benchmark::State& state) {
    int fake_input[N] = {};
    xoshiro256ss g;
    matt::hive<int> h;
    h.reshape({N, N});

    for (auto _ : state) {
        for (int t = 0; t < 100; ++t) {
            if (g() % 2 || h.empty()) {
                h.insert(fake_input, fake_input + N);
            } else {
                h.erase(h.begin(), std::next(h.begin(), N));
            }
        }
    }
    benchmark::DoNotOptimize(h);
}
BENCHMARK(BM_PlfStackIssue1_MattSmart);

static void BM_PlfStackIssue1_MattNaive(benchmark::State& state) {
    int fake_input[N] = {};
    xoshiro256ss g;
    matt::hive<int> h;

    for (auto _ : state) {
        for (int t = 0; t < 100; ++t) {
            if (g() % 2 || h.empty()) {
                h.insert(fake_input, fake_input + N);
            } else {
                h.erase(h.begin(), std::next(h.begin(), N));
            }
        }
    }
    benchmark::DoNotOptimize(h);
}
BENCHMARK(BM_PlfStackIssue1_MattNaive);
#endif

static void BM_PlfStackIssue1_OldSmart(benchmark::State& state) {
    int fake_input[N] = {};
    xoshiro256ss g;
    plf::hive<int> h;
    h.reshape({N, N});

    for (auto _ : state) {
        for (int t = 0; t < 100; ++t) {
            if (g() % 2 || h.empty()) {
                h.insert(fake_input, fake_input + N);
            } else {
                h.erase(h.begin(), std::next(h.begin(), N));
            }
        }
    }
    benchmark::DoNotOptimize(h);
}
BENCHMARK(BM_PlfStackIssue1_OldSmart);

static void BM_PlfStackIssue1_OldNaive(benchmark::State& state) {
    int fake_input[N] = {};
    xoshiro256ss g;
    plf::hive<int> h;

    for (auto _ : state) {
        for (int t = 0; t < 100; ++t) {
            if (g() % 2 || h.empty()) {
                h.insert(fake_input, fake_input + N);
            } else {
                h.erase(h.begin(), std::next(h.begin(), N));
            }
        }
    }
    benchmark::DoNotOptimize(h);
}
BENCHMARK(BM_PlfStackIssue1_OldNaive);

static void BM_PlfStackIssue1_NewSmart(benchmark::State& state) {
    int fake_input[N] = {};
    xoshiro256ss g;
    p2596::hive<int> h;
    for (auto _ : state) {
        for (int t = 0; t < 100; ++t) {
            if (g() % 2 || h.empty()) {
                if (h.capacity() == h.size()) {
                    p2596::hive<int> temp;
                    temp.reserve(N);
                    h.splice(temp);
                }
                h.insert(fake_input, fake_input + N);
            } else {
                h.erase(h.begin(), std::next(h.begin(), N));
            }
        }
    }
    benchmark::DoNotOptimize(h);
}
BENCHMARK(BM_PlfStackIssue1_NewSmart);

static void BM_PlfStackIssue1_NewNaive(benchmark::State& state) {
    int fake_input[N] = {};
    xoshiro256ss g;
    p2596::hive<int> h;

    for (auto _ : state) {
        for (int t = 0; t < 100; ++t) {
            if (g() % 2 || h.empty()) {
                h.insert(fake_input, fake_input + N);
            } else {
                h.erase(h.begin(), std::next(h.begin(), N));
            }
        }
    }
    benchmark::DoNotOptimize(h);
}
BENCHMARK(BM_PlfStackIssue1_NewNaive);

BENCHMARK_MAIN();
