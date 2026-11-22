// jaal::loop_only — a message the loop may make but no Sink may carry.
// Direct sends don't compile (compile_fail/sink.cpp case 4); this pins the
// runtime half: a loop-only message inside a bigger Msg aborts at send, and
// every other message is untouched.
#include <jaal/jaal.hpp>

#include <cstdio>
#include <cstdlib>
#include <variant>

#include <sys/wait.h>
#include <unistd.h>

namespace {
int failures = 0;
void ok(bool c, const char* what) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
struct Plain { int v = 0; };
struct EditLike { int v = 0; };   // stands in for a model-editing callable
using Group = std::variant<Plain, EditLike>;
struct Msg : std::variant<Group, Plain> { using variant::variant; };
}  // namespace
template <> inline constexpr bool jaal::loop_only<EditLike> = true;


int main() {
    jaal::kernel::inbox<Msg> in;
    const auto out = in.sink();
    ok(out.send(Msg{Plain{1}}), "a plain message is sent");
    ok(out.send(Msg{Group{Plain{2}}}), "a plain message inside a group is sent");
    const pid_t pid = ::fork();
    if (pid == 0) {
        std::freopen("/dev/null", "w", stderr);
        (void)out.send(Msg{Group{EditLike{3}}});
        std::_Exit(0);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    ok(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT, "a loop-only message nested in a Msg aborts at send");
    if (failures == 0) std::printf("loop_only_test: ok\n");
    return failures == 0 ? 0 : 1;
}
