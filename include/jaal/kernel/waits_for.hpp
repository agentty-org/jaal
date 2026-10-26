#pragma once
// jaal::kernel::waits_for — the standard library's blocking waits, behind
// `unlocked`.
//
// guarded::wait_with, pool::shutdown, scope and delay_for already need
// `const unlocked&`, so a wait under a lock doesn't compile. The std waits
// don't: future::get, std::call_once (every other caller waits while the
// body runs) and atomic::wait. A thread_root() minted inside a lock body
// slipped past the TLS check whenever it then used one of these, because
// they aren't jaal waits. These wrappers make them jaal waits: the type asks
// for `unlocked`, and the runtime check aborts at the call if the capability
// was a lie, instead of deadlocking later.
//
// The banlist bans the raw spellings outside allowlisted files.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <utility>

#include "../core/capability.hpp"

namespace jaal {

/// future.get(), proven unlocked.
template <class T>
decltype(auto) wait(const unlocked&, std::future<T>& f) {
    detail::cap::require_unlocked("jaal::wait(future)");
    return f.get();
}
template <class T>
decltype(auto) wait(const unlocked&, std::future<T>&& f) {
    detail::cap::require_unlocked("jaal::wait(future)");
    return std::move(f).get();
}

/// future.wait_for(d), proven unlocked.
template <class T, class Rep, class Period>
std::future_status wait_for(const unlocked&, const std::future<T>& f,
                            std::chrono::duration<Rep, Period> d) {
    detail::cap::require_unlocked("jaal::wait_for(future)");
    return f.wait_for(d);
}

/// std::call_once, with the body held to a leaf lock's rule. Every caller
/// but the first waits for the body, so the body is a lock in all but name:
/// if it took a lock or waited on another thread, a waiter holding that lock
/// would deadlock. So inside the body any guarded lock or jaal wait aborts
/// at once, naming the rule. In exchange the call itself needs nothing: a
/// body that can take nothing can't be half of a cycle, whatever the caller
/// holds (the same reasoning as a terminal lock). Plain I/O is fine.
template <class F>
void once(std::once_flag& flag, F&& body) {
    std::call_once(flag, [&] {
        auto& top = detail::cap::held_top();
        const std::uint16_t prev = top;
        top = leaf::value;   // as if holding a leaf: nothing may be taken under it
        struct restore { std::uint16_t& t; std::uint16_t v; ~restore() { t = v; } } r{top, prev};
        body();
    });
}

/// atomic::wait(old), proven unlocked: a compare-exchange plus this is a
/// hand-made lock, so it is held to a lock's rules.
template <class T>
void atomic_wait(const unlocked&, const std::atomic<T>& a, T old,
                 std::memory_order order = std::memory_order_seq_cst) {
    detail::cap::require_unlocked("jaal::atomic_wait");
    a.wait(old, order);
}

}  // namespace jaal
