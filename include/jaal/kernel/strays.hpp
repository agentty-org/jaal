#pragma once
// jaal::strays() — threads jaal stopped waiting for that are still running.
//
// A bounded shutdown (pool, stop_group) gives up on work that ignores its
// stop token, so a wedged syscall can't hold the process open. The thread is
// left to finish on its own. What jaal owns is co-owned by it, so that part
// is safe. But the work may still read the program's statics, and returning
// from main() destroys those under it.
//
// So a program asks before it returns:
//
//     if (jaal::strays() != 0) { flush(); std::_Exit(code); }
//
// The count drops as each one finally returns.

#include <atomic>
#include <cstddef>

namespace jaal {

namespace detail::strays {
inline std::atomic<std::size_t> count{0};
inline void add(std::size_t n) noexcept { count.fetch_add(n, std::memory_order_relaxed); }
inline void left() noexcept { count.fetch_sub(1, std::memory_order_relaxed); }
}  // namespace detail::strays

/// Threads abandoned at a shutdown grace that haven't returned yet.
[[nodiscard]] inline std::size_t strays() noexcept {
    return detail::strays::count.load(std::memory_order_relaxed);
}

}  // namespace jaal
