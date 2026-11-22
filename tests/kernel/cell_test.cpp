// jaal::cell — a leaf lock: exclusion holds, readers share, a guarded may
// enclose a cell, and NESTING one inside another aborts on the first attempt
// (a would-be deadlock becomes a deterministic failure). The shapes that
// don't compile are pinned in tests/compile_fail/cell.cpp.
#include <jaal/kernel/cell.hpp>
#include <jaal/kernel/guarded.hpp>
#include <jaal/kernel/lock_order.hpp>
#include <jaal/kernel/waits_for.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace {
int failures = 0;
void ok(bool c, const char* what) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

jaal::cell<std::map<int, std::string>> g_cache;   // a cache behind an interface, as fonts have
jaal::cell<int>                         g_other;
using outer_rank = jaal::rank<10, "test.cell_outer">;
jaal::guarded<int, outer_rank>          g_outer;

/// Run f in a child process: did it abort (SIGABRT)?
template <class F>
bool aborts(F f) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        std::freopen("/dev/null", "w", stderr);   // the abort message is expected
        f();
        std::_Exit(0);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    return WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT;
}
}  // namespace

int main() {
    // exclusion: 8 threads x 20k increments, none lost
    {
        jaal::cell<long> n;
        std::vector<std::jthread> ts;
        for (int t = 0; t < 8; ++t)
            ts.emplace_back([&] { for (int i = 0; i < 20000; ++i) n.with([](long& v) { ++v; }); });
        ts.clear();
        ok(n.read([](const long& v) { return v; }) == 8 * 20000, "cell: every increment from 8 threads lands");
    }
    // a value in, a copy out: nothing escapes the lock
    g_cache.with([](auto& m, int k, std::string v) { m[k] = std::move(v); }, 7, std::string{"seven"});
    const auto got = g_cache.read([](const auto& m, int k) -> std::optional<std::string> {
        if (auto it = m.find(k); it != m.end()) return it->second;
        return std::nullopt;
    }, 7);
    ok(got == "seven", "cell: a lookup returns an owned copy");

    // a guarded may enclose a cell: leaves rank last
    jaal::thread_root([](const jaal::unlocked& cap) {
        g_outer.with(cap, [](int& o) { o = g_other.with([](int& x) { return ++x; }); });
    });
    ok(g_other.read([](const int& x) { return x; }) == 1, "cell: taken inside a guarded body (leaf ranks last)");

    // the deadlock shape -- a second lock reached from inside a cell's body
    // through a global -- aborts at once, not one run in a million
    ok(aborts([] { g_cache.with([](auto&) { g_other.with([](int& x) { ++x; }); }); }),
       "cell: a cell inside a cell's body aborts on the first attempt");
    ok(aborts([] { g_cache.with([](auto&) { (void)g_cache.read([](const auto& m) { return m.size(); }); }); }),
       "cell: re-entering the same cell aborts (it would self-deadlock)");
    ok(aborts([] {
           g_cache.with([](auto&) {
               jaal::thread_root([](const jaal::unlocked& cap) { g_outer.with(cap, [](int&) {}); });
           });
       }),
       "cell: a guarded taken inside a cell's body aborts");
    ok(!aborts([] { g_cache.with([](auto& m) { m.clear(); }); g_other.with([](int&) {}); }),
       "cell: one after the other is fine");

    // jaal::lazy: built once, by exactly one of many racing threads, read by all
    {
        static jaal::lazy<std::vector<int>> table;
        static std::atomic<int> builds{0};   // a lazy's build takes no lock (it runs as a leaf)
        std::vector<std::jthread> ts;
        std::array<long, 8> sums{};
        for (int t = 0; t < 8; ++t)
            ts.emplace_back([&sums, t] {
                const auto& v = table.get([] { builds.fetch_add(1); return std::vector<int>(1000, 3); });
                long s = 0;
                for (int x : v) s += x;
                sums[static_cast<std::size_t>(t)] = s;
            });
        ts.clear();
        bool all = true;
        for (long s : sums) all = all && s == 3000;
        ok(builds.load() == 1 && all, "lazy: 8 racing threads, one build, all see it");
        static_assert(jaal::Sync<jaal::lazy<std::vector<int>>>, "lazy is Sync");
        static_assert(!jaal::Sendable<jaal::lazy<std::vector<int>>>, "lazy stays where it was made");
    }
    if (failures == 0) std::printf("cell_test: ok\n");
    return failures == 0 ? 0 : 1;
}
