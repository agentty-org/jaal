// tests/platform/fd_reuse_test.cpp — a registration that outlives its fd.
//
// The rule (platform/concepts.hpp) is: drop the registration, then close.
// When a caller gets it backwards, the fd number is free for the next open,
// and on epoll the late unwatch used to EPOLL_CTL_DEL whatever now held that
// number: an unrelated watch went silent and its owner waited forever.
//
// On epoll the stale slot is now marked when the number is watched again,
// so its unwatch leaves the new watch alone. poll and kqueue refuse the new
// watch with file_exists instead, which is loud rather than silent.

#include <jaal/platform.hpp>
#include <jaal/platform/posix/poll_reactor.hpp>

#include <chrono>
#include <cstdio>

#include <unistd.h>

using namespace std::chrono_literals;
namespace pf = jaal::platform;

#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,   \
                         __LINE__, #c);                                   \
            return 1;                                                     \
        }                                                                 \
    } while (0)

namespace {

template <pf::Reactor R>
int late_unwatch_spares_the_new_watch(const char* name, bool refuses) {
    auto r = R::create().value();

    int a[2];
    CHECK(::pipe(a) == 0);
    auto stale = r.watch(a[0], pf::interest::read, 1).value();
    ::close(a[0]);                  // wrong order: closed while still watched
    ::close(a[1]);

    int b[2];
    CHECK(::pipe(b) == 0);
    CHECK(b[0] == a[0]);            // the lowest free number comes back
    auto fresh = r.watch(b[0], pf::interest::read, 2);
    if (refuses) {
        CHECK(!fresh && fresh.error().code == std::errc::file_exists);
        std::printf("%s: watching a reused fd number is refused\n", name);
    } else {
        CHECK(fresh.has_value());
        stale = typename R::registration{};   // the late unwatch
        CHECK(::write(b[1], "x", 1) == 1);
        auto w = r.wait(1000ms).value();
        CHECK(w.count == 1 && w.ready[0].token == 2 && w.ready[0].readable);
        std::printf("%s: a late unwatch leaves the reused fd's watch alone\n", name);
    }
    ::close(b[0]);
    ::close(b[1]);
    return 0;
}

}  // namespace

int main() {
    int rc = 0;
#if defined(__linux__)
    rc |= late_unwatch_spares_the_new_watch<pf::epoll_reactor>("epoll", false);
#endif
#if defined(__APPLE__) || defined(__FreeBSD__)
    rc |= late_unwatch_spares_the_new_watch<pf::kqueue_reactor>("kqueue", true);
#endif
    rc |= late_unwatch_spares_the_new_watch<pf::poll_reactor>("poll", true);
    return rc;
}
