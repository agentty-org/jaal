// Compile-fail cases for jaal::cell: every way a cell body could reach a
// second lock, or leak a reference past its own, fails to build.
#include <jaal/kernel/cell.hpp>
#include <jaal/kernel/guarded.hpp>

#include <map>
#include <string>
#include <string_view>

jaal::cell<std::map<int, std::string>> cache;
jaal::cell<int>                         other;

#if JAAL_CASE == 1
// a capturing body: it could reach `other` (or anything) and nest a lock
void f() { cache.with([&](auto&) { other.with([](int&) {}); }); }
#elif JAAL_CASE == 2
// a cell passed into a body: a second lock smuggled in as an argument
void f() { cache.with([](auto&, jaal::cell<int>& o) { o.with([](int&) {}); }, std::ref(other)); }
#elif JAAL_CASE == 3
// a pointer into the value escaping the lock
void f() { const std::string* p = cache.with([](auto& m) { return &m[1]; }); (void)p; }
#elif JAAL_CASE == 4
// a view into the value escaping the lock
void f() { std::string_view v = cache.read([](const auto& m) { return std::string_view{m.at(1)}; }); (void)v; }
#elif JAAL_CASE == 5
// a borrowed argument: the body could keep it
void f() { int x = 0; other.with([](int& v, int* px) { v = *px; }, &x); }
#else
#  error "unknown JAAL_CASE"
#endif
