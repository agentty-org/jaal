// display_clock — the Windows half of jaal/platform/display_clock.hpp.
// The only file that sees <windows.h> / DWM for it.

#include <jaal/platform/display_clock.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <atomic>

namespace jaal::platform {

namespace {

// dcomp.dll / dwmapi.dll, resolved once: Windows 11 has the compositor
// clock, Windows 10 has DwmFlush (the same cadence, coarser), and a build
// that predates both still links.
struct dwm_api {
    using wait_fn  = DWORD(WINAPI*)(UINT, const HANDLE*, DWORD);
    using boost_fn = HRESULT(WINAPI*)(BOOL);
    using flush_fn = HRESULT(WINAPI*)();
    wait_fn  wait  = nullptr;
    boost_fn boost = nullptr;
    flush_fn flush = nullptr;
};
const dwm_api& api() {
    static const dwm_api a = [] {
        dwm_api r;
        if (HMODULE m = ::LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
            r.wait  = reinterpret_cast<dwm_api::wait_fn>(reinterpret_cast<void*>(::GetProcAddress(m, "DCompositionWaitForCompositorClock")));
            r.boost = reinterpret_cast<dwm_api::boost_fn>(reinterpret_cast<void*>(::GetProcAddress(m, "DCompositionBoostCompositorClock")));
        }
        if (HMODULE m = ::LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            r.flush = reinterpret_cast<dwm_api::flush_fn>(reinterpret_cast<void*>(::GetProcAddress(m, "DwmFlush")));
        return r;
    }();
    return a;
}

LONGLONG qpc_now() noexcept {
    LARGE_INTEGER q;
    ::QueryPerformanceCounter(&q);
    return q.QuadPart;
}
LONGLONG qpc_freq() noexcept {
    static const LONGLONG f = [] { LARGE_INTEGER x; ::QueryPerformanceFrequency(&x); return x.QuadPart; }();
    return f;
}

}  // namespace

struct display_clock::impl {
    HANDLE tick  = nullptr;   // manual reset: a frame began (tick() resets it)
    HANDLE armed = nullptr;   // manual reset: frames are wanted
    HANDLE quit  = nullptr;   // manual reset
    HANDLE thread = nullptr;
    std::atomic<LONGLONG>      tick_qpc{0};
    std::atomic<LONGLONG>      period_q{0};   // smoothed frame interval, QPC ticks
    std::atomic<std::uint64_t> seq{0};
    bool boosted = false;     // owner thread only
    bool is_armed = false;    // owner thread only

    // The one wait on the compositor in the process. Blocks only while armed;
    // when disarmed it sleeps on `armed` and the compositor never wakes it.
    static DWORD WINAPI run(void* self) {
        auto& p = *static_cast<impl*>(self);
        const HANDLE gate[2] = {p.quit, p.armed};   // quit first: it wins a tie
        LONGLONG last = 0;
        for (;;) {
            if (::WaitForMultipleObjects(2, gate, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) return 0;
            bool ok = false;
            if (api().wait != nullptr) {
                // STATUS_SUCCESS on a frame; returns at once (non-zero) when
                // the display is off -- then fall through to a paced retry.
                ok = api().wait(0, nullptr, 100) == 0;
            } else if (api().flush != nullptr) {
                ok = SUCCEEDED(api().flush());
            }
            if (!ok) {
                ::WaitForSingleObject(p.quit, 16);   // occluded / no clock: no spin
                last = 0;
                continue;
            }
            const LONGLONG now = qpc_now();
            if (last != 0) {
                const LONGLONG d = now - last, f = qpc_freq();
                const LONGLONG cur = p.period_q.load(std::memory_order_relaxed);
                // a real interval (2-50 ms), not a gap where frames stopped
                if (d > f / 500 && d < f / 20 && (cur == 0 || d < cur + cur / 2))
                    p.period_q.store(cur == 0 ? d : cur + (d - cur) / 8, std::memory_order_relaxed);
            }
            last = now;
            p.tick_qpc.store(now, std::memory_order_release);
            p.seq.fetch_add(1, std::memory_order_release);
            ::SetEvent(p.tick);
        }
    }

    ~impl() {
        if (thread != nullptr) {
            ::SetEvent(quit);
            ::WaitForSingleObject(thread, INFINITE);
            ::CloseHandle(thread);
        }
        if (boosted && api().boost != nullptr) (void)api().boost(FALSE);
        for (HANDLE h : {tick, armed, quit})
            if (h != nullptr) ::CloseHandle(h);
    }
};

display_clock::display_clock() : p_(std::make_unique<impl>()) {}
display_clock::display_clock(display_clock&&) noexcept = default;
display_clock& display_clock::operator=(display_clock&&) noexcept = default;
display_clock::~display_clock() = default;

result<display_clock> display_clock::open() {
    if (api().wait == nullptr && api().flush == nullptr)
        return std::unexpected(error::make(std::errc::not_supported, "display_clock: no DWM clock"));
    display_clock c;
    auto& p = *c.p_;
    p.tick  = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p.armed = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p.quit  = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (p.tick == nullptr || p.armed == nullptr || p.quit == nullptr)
        return std::unexpected(error{std::errc::io_error, static_cast<std::int32_t>(::GetLastError()), "display_clock: CreateEvent"});
    p.thread = ::CreateThread(nullptr, 64 * 1024, &impl::run, &p, 0, nullptr);
    if (p.thread == nullptr)
        return std::unexpected(error{std::errc::io_error, static_cast<std::int32_t>(::GetLastError()), "display_clock: CreateThread"});
    // It wakes the UI for frames: it must never be the one that is late.
    ::SetThreadPriority(p.thread, THREAD_PRIORITY_TIME_CRITICAL);
    return c;
}

borrowed_handle display_clock::handle() const noexcept { return borrowed_handle{p_ ? p_->tick : nullptr}; }

void display_clock::arm(bool on) noexcept {
    if (!p_ || p_->is_armed == on) return;
    p_->is_armed = on;
    if (on) ::SetEvent(p_->armed);
    else ::ResetEvent(p_->armed);
    if (!on && p_->boosted) boost(false);
}

void display_clock::boost(bool on) noexcept {
    if (!p_ || p_->boosted == on || api().boost == nullptr) return;
    p_->boosted = on;
    (void)api().boost(on ? TRUE : FALSE);
}

display_clock::frame display_clock::tick() noexcept {
    frame f;
    if (!p_) return f;
    ::ResetEvent(p_->tick);
    const LONGLONG t = p_->tick_qpc.load(std::memory_order_acquire);
    f.seq = p_->seq.load(std::memory_order_acquire);
    if (t == 0) return f;
    const LONGLONG fq = qpc_freq();
    const auto to_ns = [fq](LONGLONG q) {
        return std::chrono::nanoseconds(static_cast<std::int64_t>(static_cast<long double>(q) * 1.0e9L / static_cast<long double>(fq)));
    };
    // QPC and steady_clock need not share an epoch: convert by offset.
    f.at = std::chrono::steady_clock::now() - to_ns(qpc_now() - t);
    f.period = to_ns(p_->period_q.load(std::memory_order_relaxed));
    return f;
}

}  // namespace jaal::platform
