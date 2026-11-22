#pragma once
// jaal::audit — a trust claim, checked field by field (C++26 reflection).
//
// sendable_opt_in / frozen_opt_in / sync_opt_in are claims a human makes
// about a type jaal can't see into: private fields, views into its own
// bytes, driver handles. A claim is only as good as the day it was written:
// add a shared_ptr or a raw `int` beside it a year later and nothing notices.
//
// With reflection (GCC 16: -std=c++26 -freflection) jaal CAN see every
// field, private ones included, all the way down. An audit turns the claim
// into a statement about LEAVES that is checked on every reflection build:
//
//     static_assert(jaal::audit::claim<Face, jaal::audit::rule::shareable,
//                   jaal::audit::fields_t<"cache_">,            // a named field (why, beside it)
//                   jaal::audit::types_t<"std::span">>);        // a family of leaf types
//
//   * The type is walked RECURSIVELY: every field of every nested class
//     (private ones too), through std::vector / optional / unique_ptr /
//     array / unordered_map / pair, to the leaves.
//   * A leaf passes if jaal proves it (Sendable; Shareable = Sendable and
//     Frozen; Sync) -- or it is self-synchronising shared state (a cell)
//     where the rule allows -- or it is vouched: its field NAMED, or its type
//     in a vouched FAMILY (a substring of the type's name). The type being
//     audited never vouches for itself: its own opt-in is ignored.
//   * For shareable, a `mutable` field must be Sync (a cell): frozen means
//     no const method may write it, except through synchronisation.
//   * A vouch that matches nothing anywhere in the tree fails, so the list
//     can't rot.
//
// Without reflection the audit is true unchecked: the claim stands as
// written, and the reflection build (mayag_claims_audited) is where it is
// checked.

#include "../meta/fixed_string.hpp"

#include <string_view>

namespace jaal::audit {

enum class rule { sendable, shareable, sync };

/// Field names vouched for by an audit.
template <meta::fixed_string... Names> struct fields_t {};
/// Type families (substrings of a leaf type's name) vouched for by an audit.
template <meta::fixed_string... Names> struct types_t {};

}  // namespace jaal::audit

#if defined(__cpp_impl_reflection)

#include <array>
#include <map>
#include <memory>
#include <meta>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "frozen.hpp"
#include "sendable.hpp"
#include "sync.hpp"

namespace jaal::audit {

namespace detail {

template <class List> struct names;
template <meta::fixed_string... N> struct names<fields_t<N...>> {
    static constexpr std::array<std::string_view, sizeof...(N)> v{N.view()...};
};
template <meta::fixed_string... N> struct names<types_t<N...>> {
    static constexpr std::array<std::string_view, sizeof...(N)> v{N.view()...};
};

template <class Fields>
consteval bool field_named(std::string_view n) {
    for (auto v : names<Fields>::v)
        if (v == n) return true;
    return false;
}
template <class Types>
consteval bool type_vouched(std::meta::info t) {
    const std::string_view s = std::meta::display_string_of(t);
    for (auto v : names<Types>::v)
        if (s.find(v) != std::string_view::npos) return true;
    return false;
}

/// Does jaal prove T on its own, under rule R? A raw pointer never is (what
/// it points at is exactly what can't be seen); a class may also pass as
/// self-synchronising shared state (a cell, an atomic, a co_owned).
template <rule R, class T>
consteval bool proven() {
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_pointer_v<U> || std::is_member_pointer_v<U> || std::is_reference_v<T>) return false;
    else if constexpr (R == rule::sendable) return Sendable<U>;
    else if constexpr (R == rule::shareable) return (Sendable<U> && Frozen<U>) || (std::is_class_v<U> && Sync<U>);
    else return Sync<U>;
}

/// Is r declared inside namespace std (at any depth)?
consteval bool in_std(std::meta::info r) {
    std::meta::info p = std::meta::parent_of(r);
    for (int depth = 0; depth < 32 && std::meta::is_namespace(p); ++depth) {
        if (!std::meta::has_identifier(p)) {   // the global namespace (or an anonymous one): stop
            if (!std::meta::has_parent(p)) return false;
        } else if (std::meta::identifier_of(p) == "std") {
            return true;
        }
        if (!std::meta::has_parent(p)) return false;
        p = std::meta::parent_of(p);
    }
    return false;
}
consteval std::string_view name_of(std::meta::info m) {
    return std::meta::has_identifier(m) ? std::meta::identifier_of(m) : std::string_view{};
}

// The std wrappers the walk sees through: the types inside them.
template <class T> struct inner { static constexpr bool wrapper = false; };
template <class X, class A> struct inner<std::vector<X, A>> { static constexpr bool wrapper = true; using types = std::tuple<X>; };
template <class X> struct inner<std::optional<X>> { static constexpr bool wrapper = true; using types = std::tuple<X>; };
template <class X, class D> struct inner<std::unique_ptr<X, D>> { static constexpr bool wrapper = true; using types = std::tuple<X>; };
template <class X, std::size_t N> struct inner<std::array<X, N>> { static constexpr bool wrapper = true; using types = std::tuple<X>; };
template <class X, std::size_t N> struct inner<X[N]> { static constexpr bool wrapper = true; using types = std::tuple<X>; };
template <class A, class B> struct inner<std::pair<A, B>> { static constexpr bool wrapper = true; using types = std::tuple<A, B>; };
template <class K, class V, class H, class E, class A> struct inner<std::unordered_map<K, V, H, E, A>> {
    static constexpr bool wrapper = true; using types = std::tuple<K, V>;
};
template <class K, class V, class C, class A> struct inner<std::map<K, V, C, A>> {
    static constexpr bool wrapper = true; using types = std::tuple<K, V>;
};

template <class T>
consteval bool user_class() {
    using U = std::remove_cv_t<T>;
    if constexpr (!std::is_class_v<U> || std::is_union_v<U> || std::is_polymorphic_v<U>) return false;
    else return !in_std(std::meta::has_template_arguments(std::meta::dealias(^^U)) ? std::meta::template_of(std::meta::dealias(^^U)) : std::meta::dealias(^^U));
}

template <class T>
consteval auto members() {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(std::meta::dealias(^^T), std::meta::access_context::unchecked()));
}

template <rule R, class Fields, class Types, std::size_t Depth, class Owner, class T>
consteval bool leaf_ok();

/// Every field of class T, recursively.
template <rule R, class Fields, class Types, std::size_t Depth, class T>
consteval bool class_ok() {
    static_assert(Depth < 24, "jaal::audit: the type tree is too deep (a recursive type?)");
    template for (constexpr auto m : members<T>()) {
        using F = [:std::meta::type_of(m):];
        constexpr std::string_view name = name_of(m);
        if constexpr (!field_named<Fields>(name)) {
            if constexpr (R == rule::shareable && std::meta::is_mutable_member(m) &&
                          !Sync<std::remove_cv_t<F>> && !type_vouched<Types>(std::meta::dealias(^^F))) {
                static_assert(false, std::string("jaal::audit: ") + std::string(std::meta::display_string_of(std::meta::dealias(^^T))) +
                                         "::" + std::string(name) +
                                         " is `mutable` but not Sync: a const method could write it unsynchronised");
            }
            if constexpr (!std::meta::is_mutable_member(m) || !Sync<std::remove_cv_t<F>>)
                static_assert(leaf_ok<R, Fields, Types, Depth + 1, T, F>());
        }
    }
    return true;
}

template <rule R, class Fields, class Types, std::size_t Depth, class Owner, class Tuple>
consteval bool each_ok() {
    return []<class... X>(std::type_identity<std::tuple<X...>>) {
        return (leaf_ok<R, Fields, Types, Depth, Owner, X>() && ...);
    }(std::type_identity<Tuple>{});
}

template <class T> struct unique_target { static constexpr bool is = false; };
template <class X, class D> struct unique_target<std::unique_ptr<X, D>> { static constexpr bool is = true; using type = X; };
template <class T> struct shared_target { static constexpr bool is = false; };
template <class X> struct shared_target<std::shared_ptr<X>> { static constexpr bool is = true; using type = X; };

template <rule R, class Fields, class Types, std::size_t Depth, class Owner, class T>
consteval bool leaf_ok() {
    using U = std::remove_cv_t<T>;
    if constexpr (type_vouched<Types>(std::meta::dealias(^^U))) return true;
    else if constexpr (proven<R, U>()) return true;
    // shared_ptr<const X>: other owners exist, but none can write X (it is
    // const all the way down -- walked as shareable). A shared_ptr to a
    // non-const X is shared mutable state: never.
    else if constexpr (shared_target<U>::is) {
        using X = typename shared_target<U>::type;
        if constexpr (std::is_const_v<X> && !std::is_void_v<std::remove_cv_t<X>>)
            return leaf_ok<rule::shareable, Fields, Types, Depth + 1, Owner, X>();
        else {
            static_assert(false, std::string("jaal::audit: in ") + std::string(std::meta::display_string_of(std::meta::dealias(^^Owner))) +
                                     ", a " + std::string(std::meta::display_string_of(std::meta::dealias(^^U))) +
                                     " shares mutable (or untyped) state");
            return false;
        }
    }
    // Shared through const access, a unique_ptr<X> still hands out a non-const
    // X&: a const method could write through it. So X must be const (then it
    // is walked as frozen) or synchronise itself.
    else if constexpr (R != rule::sendable && unique_target<U>::is) {
        using X = typename unique_target<U>::type;
        if constexpr (std::is_const_v<X>) return leaf_ok<R, Fields, Types, Depth + 1, Owner, X>();
        else if constexpr (Sync<std::remove_cv_t<X>>) return true;
        else {
            static_assert(false, std::string("jaal::audit: in ") + std::string(std::meta::display_string_of(std::meta::dealias(^^Owner))) +
                                     ", a " + std::string(std::meta::display_string_of(std::meta::dealias(^^U))) +
                                     " is shared but its pointee is neither const nor Sync: a const method could write through it");
            return false;
        }
    }
    else if constexpr (inner<U>::wrapper) return each_ok<R, Fields, Types, Depth, Owner, typename inner<U>::types>();
    else if constexpr (user_class<U>()) return class_ok<R, Fields, Types, Depth, U>();
    else {
        static_assert(false, std::string("jaal::audit: in ") + std::string(std::meta::display_string_of(std::meta::dealias(^^Owner))) +
                                 ", a field of type " + std::string(std::meta::display_string_of(std::meta::dealias(^^U))) +
                                 " is neither proven by jaal nor vouched for in the audit");
        return false;
    }
}

// ── stale vouches ─────────────────────────────────────────────────────────
template <class Fields, class Types, std::size_t Depth, class T>
consteval void collect(std::vector<std::string_view>& seen_fields, std::vector<std::string_view>& seen_types);

template <class Fields, class Types, std::size_t Depth, class Tuple>
consteval void collect_each(std::vector<std::string_view>& f, std::vector<std::string_view>& t) {
    [&]<class... X>(std::type_identity<std::tuple<X...>>) {
        (collect<Fields, Types, Depth, X>(f, t), ...);
    }(std::type_identity<Tuple>{});
}

template <class Fields, class Types, std::size_t Depth, class T>
consteval void collect(std::vector<std::string_view>& f, std::vector<std::string_view>& t) {
    using U = std::remove_cv_t<T>;
    if constexpr (Depth < 24) {
        const std::string_view tn = std::meta::display_string_of(std::meta::dealias(^^U));
        for (auto v : names<Types>::v)
            if (tn.find(v) != std::string_view::npos) t.push_back(v);
        if constexpr (inner<U>::wrapper) collect_each<Fields, Types, Depth + 1, typename inner<U>::types>(f, t);
        else if constexpr (shared_target<U>::is) {
            using X = std::remove_cv_t<typename shared_target<U>::type>;
            if constexpr (!std::is_void_v<X>) collect<Fields, Types, Depth + 1, X>(f, t);
        }
        else if constexpr (user_class<U>()) {
            template for (constexpr auto m : members<U>()) {
                using F = [:std::meta::type_of(m):];
                for (auto v : names<Fields>::v)
                    if (v == name_of(m)) f.push_back(v);
                collect<Fields, Types, Depth + 1, F>(f, t);
            }
        }
    }
}

template <class T, class Fields, class Types>
consteval bool vouches_used() {
    std::vector<std::string_view> f, t;
    template for (constexpr auto m : members<T>()) {
        using F = [:std::meta::type_of(m):];
        for (auto v : names<Fields>::v)
            if (v == name_of(m)) f.push_back(v);
        collect<Fields, Types, 1, F>(f, t);
    }
    for (auto v : names<Fields>::v) {
        bool hit = false;
        for (auto s : f) hit = hit || s == v;
        if (!hit) return false;
    }
    for (auto v : names<Types>::v) {
        bool hit = false;
        for (auto s : t) hit = hit || s == v;
        if (!hit) return false;
    }
    return true;
}

}  // namespace detail

/// Run the audit (see the header): fails to compile, naming the owner and the
/// field, when a leaf is neither proven nor vouched for, or a vouch is stale.
template <class T, rule R, class Fields, class Types>
consteval bool check() {
    static_assert(detail::class_ok<R, Fields, Types, 0, T>());
    static_assert(detail::vouches_used<T, Fields, Types>(),
                  "jaal::audit: the audit vouches for a field or type family that is nowhere in the type");
    template for (constexpr auto b : std::define_static_array(
                      std::meta::bases_of(std::meta::dealias(^^T), std::meta::access_context::unchecked()))) {
        using B = [:std::meta::type_of(b):];
        static_assert(detail::leaf_ok<R, Fields, Types, 1, T, B>(),
                      "jaal::audit: a base class of an audited type must itself pass");
    }
    return true;
}

}  // namespace jaal::audit

#endif

namespace jaal::audit {
/// The audit for T under rule R, vouching for Fields (fields_t<...>) and
/// Types (types_t<...>). True when checked; also true -- unchecked -- without
/// reflection, so `static_assert(jaal::audit::claim<...>)` sits beside the
/// claim on every compiler and is enforced on the one that can see fields.
template <class T, rule R, class Fields = fields_t<>, class Types = types_t<>>
inline constexpr bool claim =
#if defined(__cpp_impl_reflection)
    check<T, R, Fields, Types>();
#else
    true;
#endif
}  // namespace jaal::audit
