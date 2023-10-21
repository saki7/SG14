
#include <benchmark/benchmark.h>
#include <sg14/algorithm_ext.h>
#include <random>
#include <vector>

static std::vector<std::array<int, 16>> get_sample_data()
{
    std::mt19937 g;
    auto v = std::vector<std::array<int, 16>>(30'000);
    for (auto& arr : v) {
        std::generate_n(arr.data(), arr.size(), std::ref(g));
    }
    return v;
}

static auto isOdd = [](const std::array<int, 16>& t) { return (t[0] & 1) != 0; };
static auto isEven = [](const std::array<int, 16>& t) { return (t[0] & 1) == 0; };

static void UnstableRemoveIf(benchmark::State& state)
{
    auto orig = get_sample_data();
    auto v1 = orig;
    auto v2 = orig;

    for (auto _ : state) {
        auto it1 = sg14::unstable_remove_if(v1.begin(), v1.end(), isOdd);
        auto it2 = sg14::unstable_remove_if(v2.begin(), v2.end(), isEven);
        state.PauseTiming();
        benchmark::DoNotOptimize(it1);
        benchmark::DoNotOptimize(it2);
        benchmark::ClobberMemory();
        std::copy(orig.begin(), orig.end(), v1.begin());
        std::copy(orig.begin(), orig.end(), v2.begin());
        state.ResumeTiming();
    }
}
BENCHMARK(UnstableRemoveIf);

static void StdPartition(benchmark::State& state)
{
    auto orig = get_sample_data();
    auto v1 = orig;
    auto v2 = orig;

    for (auto _ : state) {
        auto it1 = std::partition(v1.begin(), v1.end(), isOdd);
        auto it2 = std::partition(v2.begin(), v2.end(), isEven);
        state.PauseTiming();
        benchmark::DoNotOptimize(it1);
        benchmark::DoNotOptimize(it2);
        benchmark::ClobberMemory();
        std::copy(orig.begin(), orig.end(), v1.begin());
        std::copy(orig.begin(), orig.end(), v2.begin());
        state.ResumeTiming();
    }
}
BENCHMARK(StdPartition);

static void StdRemoveIf(benchmark::State& state)
{
    auto orig = get_sample_data();
    auto v1 = orig;
    auto v2 = orig;

    for (auto _ : state) {
        auto it1 = std::remove_if(v1.begin(), v1.end(), isOdd);
        auto it2 = std::remove_if(v2.begin(), v2.end(), isEven);
        state.PauseTiming();
        benchmark::DoNotOptimize(it1);
        benchmark::DoNotOptimize(it2);
        benchmark::ClobberMemory();
        std::copy(orig.begin(), orig.end(), v1.begin());
        std::copy(orig.begin(), orig.end(), v2.begin());
        state.ResumeTiming();
    }
}
BENCHMARK(StdRemoveIf);

BENCHMARK_MAIN();
