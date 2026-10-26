#pragma once
// jaal::kernel::published<T> — the current version of a shared object.
//
// A program often has ONE live instance of something many threads use: a
// connection pool, a compiled catalog, a provider handle. A writer builds a
// new one and swaps it in; readers grab the current one and keep it alive
// for as long as they use it, even across a swap.
//
// guarded<T> refuses that shape, rightly: a std::shared_ptr is a SHARED
// owner, so handing one out of a lock shares state past the lock. That is
// exactly what this pattern wants, and it is safe only when the pointee
// guards itself. published<T> is that pattern, named, and the condition is
// CHECKED: a published<const T> needs Frozen<T> (readers can't change it),
// a published<T> needs Sync<T> (T synchronises its own writes). A type jaal
// can't see inside opts in with jaal::sync_opt_in / frozen_opt_in, which is
// a claim a human made and a grep can find.
//
//   jaal::kernel::published<Pool> pool;          // Pool synchronises itself
//   pool.publish(std::make_shared<Pool>(...));   // writer
//   if (auto p = pool.current()) p->use();       // reader, any thread
//   auto old = pool.take();                      // teardown: swap out
//
// Readers pay one atomic load (std::atomic<std::shared_ptr>), never a lock
// (a tiny mutex on a standard library without that specialisation).
// A reader's handle keeps the old object alive after a swap, so a swap
// never frees anything under someone.
//
// What it does NOT do: make T thread-safe. T's own members are reached
// through the handle from many threads at once, so T must be immutable
// after publish, or synchronise its own mutable parts (its own guarded<U>,
// atomics). That is the claim a caller makes by choosing this type.

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <version>

#include "../core/frozen.hpp"
#include "../core/sendable.hpp"
#include "../core/sync.hpp"

// std::atomic<std::shared_ptr<T>> is C++20, and libstdc++ has it, but libc++
// still does not (Termux, Android): there the declaration falls through to
// the primary atomic template and fails to compile. Gate on the feature
// macro, and keep the same API over a mutex where it is missing. The slot is
// written rarely and read a few times a frame, so that costs a couple of
// uncontended lock pairs. JAAL_FORCE_PUBLISHED_MUTEX=1 builds the fallback on
// a library that has the specialisation, so it gets exercised somewhere.
#if defined(JAAL_FORCE_PUBLISHED_MUTEX) && JAAL_FORCE_PUBLISHED_MUTEX
#  define JAAL_ATOMIC_SHARED_PTR 0
#elif defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
#  define JAAL_ATOMIC_SHARED_PTR 1
#else
#  define JAAL_ATOMIC_SHARED_PTR 0
#endif

namespace jaal::kernel {

template <class T>
class published {
public:
    /// The condition, checked on the first publish rather than at the class
    /// so a slot can name a type that is still incomplete where it is
    /// declared (snapshot's cell, a Pool that holds its own published<>).
    static consteval void require_shareable() {
        using U = std::remove_cv_t<T>;
        if constexpr (std::is_const_v<T>) {
            // Readers get const access. Fine when nothing changes through it
            // (Frozen), or when only synchronised `mutable` fields do
            // (ConstSync: read-only data plus an atomic counter).
            if constexpr (!ConstSync<U>)
                static_assert(ConstSync<U>,
                    "jaal: published<const T> hands the same T to many threads; T must be "
                    "Frozen, its mutable fields Sync (see jaal::frozen_reason<T>()), or opt "
                    "in with frozen_opt_in");
        } else if constexpr (!Sync<U>) {
            static_assert(Sync<U>,
                "jaal: published<T> hands the same T& to many threads; T must be Sync "
                "(see jaal::sync_reason<T>()), or opt in with sync_opt_in");
        }
    }
    published() = default;
    published(const published&)            = delete;
    published& operator=(const published&) = delete;

    /// Make `next` the current object. Readers that already hold the old
    /// one keep it until they drop it.
    void publish(std::shared_ptr<T> next) noexcept {
        require_shareable();
#if JAAL_ATOMIC_SHARED_PTR
        slot_.store(std::move(next), std::memory_order_release);
#else
        std::shared_ptr<T> old;   // dropped after the lock, not under it
        {
            std::lock_guard lk(m_);
            old = std::exchange(slot_, std::move(next));
        }
#endif
    }

    /// publish(), returning the old object instead of dropping it here.
    /// Use it when destroying the old one blocks (it joins threads) and this
    /// runs under a guarded: hold the result and let it go after the lock.
    [[nodiscard]] std::shared_ptr<T> replace(std::shared_ptr<T> next) noexcept {
        require_shareable();
#if JAAL_ATOMIC_SHARED_PTR
        return slot_.exchange(std::move(next), std::memory_order_acq_rel);
#else
        std::lock_guard lk(m_);
        return std::exchange(slot_, std::move(next));
#endif
    }

    /// Publish `next` only if `expected` is still current. Returns false
    /// (publishing nothing) when another writer got there first, so a
    /// rebuild can re-check against what that writer published.
    [[nodiscard]] bool publish_if(const std::shared_ptr<T>& expected,
                                  std::shared_ptr<T> next) noexcept {
        require_shareable();
#if JAAL_ATOMIC_SHARED_PTR
        auto e = expected;
        return slot_.compare_exchange_strong(e, std::move(next),
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire);
#else
        std::shared_ptr<T> old;
        {
            std::lock_guard lk(m_);
            if (slot_ != expected) return false;
            old = std::exchange(slot_, std::move(next));
        }
        return true;
#endif
    }

    /// The current object, or null if none is published.
    [[nodiscard]] std::shared_ptr<T> current() const noexcept {
#if JAAL_ATOMIC_SHARED_PTR
        return slot_.load(std::memory_order_acquire);
#else
        std::lock_guard lk(m_);
        return slot_;
#endif
    }

    /// Swap the current object out (leaving none) and return it, so the
    /// caller decides where it is destroyed.
    [[nodiscard]] std::shared_ptr<T> take() noexcept {
#if JAAL_ATOMIC_SHARED_PTR
        return slot_.exchange(nullptr, std::memory_order_acq_rel);
#else
        std::lock_guard lk(m_);
        return std::exchange(slot_, nullptr);
#endif
    }

private:
#if JAAL_ATOMIC_SHARED_PTR
    std::atomic<std::shared_ptr<T>> slot_;
#else
    mutable std::mutex m_;   // copying a shared_ptr writes its refcount
    std::shared_ptr<T> slot_;
#endif
};

}  // namespace jaal::kernel

namespace jaal {
// A published<T> is a process-wide slot, not a value to move around.
template <class T> inline constexpr bool sendable_opt_out<kernel::published<T>> = true;
// The slot itself is an atomic swap; what T does is T's business, and the
// type's contract (see the top of this file) already says T guards itself.
template <class T> inline constexpr bool sync_opt_in<kernel::published<T>> = true;
}  // namespace jaal
