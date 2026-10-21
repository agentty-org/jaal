// tests/kernel/strays_test.cpp — jaal::strays().
//
//   1. a pool shutdown that gives up on a job counts it, and the count
//      drops when the job finally returns
//   2. the same for an isolated task
//   3. a stop_group member left at the grace counts until it leaves
//   4. a shutdown that waited for everything counts nothing, and a second
//      shutdown of an abandoned pool doesn't count twice

#include <jaal/kernel/pool.hpp>
#include <jaal/kernel/stop_group.hpp>
#include <jaal/kernel/strays.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

namespace {
int failures = 0;
void ok(bool c, const char* w) {
    if (!c) { std::fprintf(stderr, "FAIL %s\n", w); ++failures; }
}

// Waits for the count to settle: a stray decrements as it returns, on its
// own thread.
bool strays_become(std::size_t n) {
    for (int i = 0; i < 500; ++i) {
        if (jaal::strays() == n) return true;
        std::this_thread::sleep_for(2ms);
    }
    return jaal::strays() == n;
}

// A job that ignores its stop token until released.
std::atomic<bool> release{false};
std::atomic<int>  started{0};
void stubborn(std::stop_token) {
    started.fetch_add(1);
    while (!release.load()) std::this_thread::sleep_for(1ms);
}
}  // namespace

static int run(const jaal::unlocked& cap) {
    // 1 + 4b. a pool job left at the grace.
    {
        release = false;
        started = 0;
        jaal::kernel::pool p(2);
        (void)p.submit(stubborn);
        while (started.load() < 1) std::this_thread::yield();
        const auto n = p.shutdown(cap, 20ms);
        ok(n == 1, "1. shutdown reports the stuck job");
        ok(jaal::strays() == 1, "1. it is a stray");
        (void)p.shutdown(cap, 0ms);
        ok(jaal::strays() == 1, "4. a second shutdown doesn't count twice");
        release = true;
        ok(strays_become(0), "1. it stops being a stray when it returns");
    }

    // 2. an isolated task.
    {
        release = false;
        started = 0;
        jaal::kernel::pool p(1);
        (void)p.submit_isolated(stubborn);
        while (started.load() < 1) std::this_thread::yield();
        ok(p.shutdown(cap, 20ms) == 1, "2. shutdown reports the isolated task");
        ok(jaal::strays() == 1, "2. it is a stray");
        release = true;
        ok(strays_become(0), "2. and stops being one when it returns");
    }

    // 3. a stop_group member.
    {
        release = false;
        std::atomic<bool> in{false};
        jaal::kernel::stop_group g;
        std::thread t([&] {
            auto m = g.join();
            in = true;
            while (!release.load()) std::this_thread::sleep_for(1ms);
        });
        while (!in) std::this_thread::yield();
        ok(g.stop_and_wait(cap, 20ms) == 1, "3. the member is still out");
        ok(jaal::strays() == 1, "3. it is a stray");
        release = true;
        t.join();
        ok(jaal::strays() == 0, "3. leaving the group counts it out");
    }

    // 4a. nothing abandoned, nothing counted.
    {
        jaal::kernel::pool p(2);
        auto f = p.submit([](std::stop_token) {});
        f.get();
        ok(p.shutdown(cap, 1s) == 0, "4. a clean shutdown abandons nothing");
        ok(jaal::strays() == 0, "4. and counts nothing");
    }

    if (failures == 0) std::puts("strays: every abandoned thread is counted until it returns");
    return failures ? 1 : 0;
}

int main() {
    return jaal::thread_root([](const jaal::unlocked& cap) { return run(cap); });
}
