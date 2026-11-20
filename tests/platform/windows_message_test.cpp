// tests/platform/windows_message_test.cpp — the Windows reactor on the
// thread's window-message queue (wait_reactor::message_queue()).
//
//   1. an empty queue is quiet: a 300 ms wait blocks once and burns no CPU
//   2. a message POSTED from another thread wakes the wait promptly
//   3. the message is still in the queue afterwards (the reactor consumed
//      nothing), and it stays readable until the thread drains it
//   4. a message that arrived BEFORE the wait, and was already seen by a
//      PeekMessage(PM_NOREMOVE) -- so it is no longer "new" -- still wakes
//      it. This is the MWMO_INPUTAVAILABLE case: without the flag a GUI
//      thread would sleep on a keystroke it had already glimpsed.
//   5. handles and the queue together: an event still wakes it, and is
//      reported on its own token
//   6. dropping the registration returns the wait to WaitForMultipleObjects:
//      a posted message no longer wakes it

#include <jaal/platform.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace std::chrono_literals;
namespace pf = jaal::platform;
using clk = std::chrono::steady_clock;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                         \
        }                                                                     \
    } while (0)

namespace {

double cpu_ms() {
    FILETIME c, e, k, u;
    ::GetProcessTimes(::GetCurrentProcess(), &c, &e, &k, &u);
    auto v = [](FILETIME f) {
        return static_cast<double>(static_cast<unsigned long long>(f.dwHighDateTime) << 32 | f.dwLowDateTime) / 1e4;
    };
    return v(k) + v(u);
}

void drain() {
    MSG m;
    while (::PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {}
}

constexpr UINT kMsg = WM_APP + 7;

int quiet_then_wakes_on_post() {
    drain();
    auto r = pf::wait_reactor::create().value();
    auto reg = r.watch(pf::wait_reactor::message_queue(), pf::interest::read, 1).value();

    const auto w0 = r.os_waits();
    const double c0 = cpu_ms();
    const auto t0 = clk::now();
    auto a = r.wait(300ms).value();
    CHECK(a.timeout && a.count == 0);
    CHECK(clk::now() - t0 >= 250ms);
    std::printf("  idle 300 ms wait: %llu os waits, %.1f ms cpu\n",
                static_cast<unsigned long long>(r.os_waits() - w0), cpu_ms() - c0);
    CHECK(r.os_waits() - w0 <= 2);
    CHECK(cpu_ms() - c0 < 30.0);

    const DWORD tid = ::GetCurrentThreadId();
    std::jthread t([tid] {
        std::this_thread::sleep_for(50ms);
        ::PostThreadMessageW(tid, kMsg, 42, 0);
    });
    const auto t1 = clk::now();
    auto b = r.wait(5s).value();
    const auto took = clk::now() - t1;
    std::printf("  posted message woke the wait after %.1f ms\n",
                std::chrono::duration<double, std::milli>(took).count());
    CHECK(!b.timeout && b.count == 1 && b.ready[0].token == 1 && b.ready[0].readable);
    CHECK(took < 1s);

    // The reactor consumed nothing: the message is still there, and the
    // queue stays readable until the thread itself drains it.
    MSG m;
    CHECK(::PeekMessageW(&m, nullptr, kMsg, kMsg, PM_NOREMOVE) && m.wParam == 42);
    CHECK(r.wait(0ms).value().count == 1);
    drain();
    CHECK(r.wait(50ms).value().timeout);
    return 0;
}

int already_seen_message_still_wakes() {
    drain();
    auto r = pf::wait_reactor::create().value();
    auto reg = r.watch(pf::wait_reactor::message_queue(), pf::interest::read, 2).value();
    ::PostThreadMessageW(::GetCurrentThreadId(), kMsg, 7, 0);
    MSG m;
    CHECK(::PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE));   // glimpsed, not removed: no longer "new"
    const auto t0 = clk::now();
    auto a = r.wait(2s).value();
    CHECK(!a.timeout && a.count == 1 && a.ready[0].token == 2);
    CHECK(clk::now() - t0 < 100ms);
    drain();
    return 0;
}

int handles_and_queue_together() {
    drain();
    auto r = pf::wait_reactor::create().value();
    const HANDLE ev = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto q = r.watch(pf::wait_reactor::message_queue(), pf::interest::read, 3).value();
    auto e = r.watch(ev, pf::interest::read, 4).value();
    CHECK(r.wait(30ms).value().timeout);
    ::SetEvent(ev);
    auto a = r.wait(2s).value();
    CHECK(!a.timeout && a.count == 1 && a.ready[0].token == 4);   // the event, not the empty queue
    ::ResetEvent(ev);
    ::CloseHandle(ev);
    return 0;
}

int unwatch_stops_message_wakeups() {
    drain();
    auto r = pf::wait_reactor::create().value();
    {
        auto reg = r.watch(pf::wait_reactor::message_queue(), pf::interest::read, 5).value();
    }   // dropped
    ::PostThreadMessageW(::GetCurrentThreadId(), kMsg, 1, 0);
    auto a = r.wait(150ms).value();
    CHECK(a.timeout && a.count == 0);
    drain();
    return 0;
}

}  // namespace

int main() {
    // A thread has no message queue until it calls a USER function; make one.
    MSG m;
    ::PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE);

    if (int rc = quiet_then_wakes_on_post()) return rc;
    std::printf("ok   empty queue is quiet; a posted message wakes it promptly\n");
    if (int rc = already_seen_message_still_wakes()) return rc;
    std::printf("ok   a message already glimpsed by PeekMessage still wakes it\n");
    if (int rc = handles_and_queue_together()) return rc;
    std::printf("ok   handles and the queue are watched together\n");
    if (int rc = unwatch_stops_message_wakeups()) return rc;
    std::printf("ok   dropping the registration stops message wakeups\n");
    return 0;
}
