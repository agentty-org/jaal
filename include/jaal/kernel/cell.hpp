#pragma once
// jaal::cell<T> — a LEAF lock you can take anywhere, including behind an
// interface that has no capability parameter (a text measurer, a glyph
// renderer): a cache, a counter, a free list.
//
//   jaal::cell<std::map<Key, Outline>> cache;
//   auto o = cache.with([](auto& m, Key k) -> std::optional<Outline> {
//       if (auto it = m.find(k); it != m.end()) return it->second;
//       return std::nullopt;
//   }, key);
//
// guarded<T, R> orders locks by proving, at compile time, what the caller
// already holds -- which needs a capability argument threaded down to the
// lock. A cell needs none, because it can never be part of an ordering at
// all: it is ALWAYS the innermost lock, and that is enforced, not hoped for.
//
//   * The body is CAPTURELESS and its arguments are Sendable: it reaches only
//     the cell's value and what it was handed -- never a capability, a
//     guarded or another cell (none of them is Sendable).
//   * Nothing escapes: the result is Sendable, so no pointer, reference or
//     view into T survives the lock.
//   * Nesting is a deterministic abort, not a rare deadlock. While a body
//     runs, this thread is marked as holding a leaf. Taking a cell or a
//     guarded, or any blocking jaal wait, from inside it -- say through a
//     global the body named -- aborts on the FIRST attempt, every time,
//     whatever the scheduling. A deadlock needs two locks held at once; a
//     program in which that ever happens fails its first test that does it.
//   * A cell may be taken while holding a guarded: leaves rank last.
//
// So a program built from guarded (ranked) and cell (leaf) locks has a total
// lock order, and cannot deadlock on them.

#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>

#include "../core/capability.hpp"
#include "../core/sendable.hpp"
#include "../core/sync.hpp"

namespace jaal {

namespace detail::cell_ {
template <class F>
concept captureless = std::is_empty_v<std::remove_cvref_t<F>> && std::default_initializable<std::remove_cvref_t<F>>;
template <class R>
concept escapable = std::is_void_v<R> || Sendable<std::remove_cv_t<R>>;

/// Mark the thread as inside a leaf for the body's duration; refuse if it
/// already is (a cell or guarded reached from inside another cell's body).
class enter {
  public:
    enter() noexcept : prev_(detail::cap::held_top()) {
        if (prev_ == leaf::value) [[unlikely]]
            detail::cap::lie("a jaal::cell taken inside a jaal::cell's body (leaves never nest)");
        detail::cap::held_top() = leaf::value;
    }
    ~enter() { detail::cap::held_top() = prev_; }
    enter(const enter&) = delete;
    enter& operator=(const enter&) = delete;

  private:
    std::uint16_t prev_;
};
}  // namespace detail::cell_

template <class T>
class cell {
  public:
    cell() = default;
    template <class... A>
        requires std::constructible_from<T, A...>
    explicit cell(std::in_place_t, A&&... a) : value_(std::forward<A>(a)...) {}
    cell(const cell&) = delete;
    cell& operator=(const cell&) = delete;

    /// Exclusive: body(T&, args...). Captureless body, Sendable args and result.
    template <class F, class... Args>
    auto with(F f, Args... args) -> std::invoke_result_t<F&, T&, Args&&...> {
        check<F, T&, Args...>();
        std::unique_lock lk(m_);
        const detail::cell_::enter in;
        return f(value_, std::move(args)...);
    }
    /// Shared: body(const T&, args...); readers run together.
    template <class F, class... Args>
    auto read(F f, Args... args) const -> std::invoke_result_t<F&, const T&, Args&&...> {
        check<F, const T&, Args...>();
        std::shared_lock lk(m_);
        const detail::cell_::enter in;
        return f(value_, std::move(args)...);
    }

  private:
    template <class F, class Ref, class... Args>
    static consteval void check() {
        static_assert(detail::cell_::captureless<F>,
                      "jaal::cell: the body must be CAPTURELESS; pass what it needs as arguments after it "
                      "(a capture could reach another lock)");
        static_assert((Sendable<Args> && ...),
                      "jaal::cell: an argument is not Sendable (a pointer, reference, lock or capability "
                      "could reach another lock); pass owned values");
        if constexpr (detail::cell_::captureless<F> && (Sendable<Args> && ...)) {
            static_assert(std::is_invocable_v<F&, Ref, Args&&...>, "jaal::cell: the body can't be called as body(T&, args...)");
            if constexpr (std::is_invocable_v<F&, Ref, Args&&...>)
                static_assert(detail::cell_::escapable<std::invoke_result_t<F&, Ref, Args&&...>>,
                              "jaal::cell: the body returns something not Sendable -- a pointer, reference "
                              "or view into the value would outlive the lock");
        }
    }

    mutable std::shared_mutex m_;
    T                         value_{};
};

// A cell is a lock: it stays where it was made (never sent), and every access
// to its value is under it (so threads may share it).
template <class T> inline constexpr bool sendable_opt_out<cell<T>> = true;
template <class T> inline constexpr bool sync_opt_in<cell<T>> = true;

}  // namespace jaal
