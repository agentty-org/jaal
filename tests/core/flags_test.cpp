// jaal flags: a run's starting data reaches init as an argument -- no global.
#include <jaal/jaal.hpp>

#include <cstdio>
#include <string>
#include <variant>

namespace {
struct App {
    struct Flags { std::string greeting = "default"; int start = 0; };
    struct Model { std::string greeting; int n = 0; };
    struct Bump {};
    using Msg = std::variant<Bump>;
    using Cmd = jaal::Cmd<Msg>;
    static Cmd init(Model& m, const Flags& f) { m.greeting = f.greeting; m.n = f.start; return {}; }
    static Cmd update(Model& m, Bump) { ++m.n; return {}; }
};
static_assert(jaal::Program<App>);
}  // namespace

int main() {
    int failures = 0;
    auto [m, c] = jaal::prog::init<App>(App::Flags{"hi", 41});
    (void)c;
    if (m.greeting != "hi" || m.n != 41) { std::fprintf(stderr, "FAIL: flags reach init\n"); ++failures; }
    auto [d, c2] = jaal::prog::init<App>();
    (void)c2;
    if (d.greeting != "default") { std::fprintf(stderr, "FAIL: no flags: value-initialised\n"); ++failures; }
    jaal::headless<App> h;
    if (h.model().greeting != "default") { std::fprintf(stderr, "FAIL: a headless host starts from default flags\n"); ++failures; }
    if (failures == 0) std::printf("flags_test: ok\n");
    return failures;
}
