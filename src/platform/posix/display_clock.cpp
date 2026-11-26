// display_clock — where the OS has no compositor clock to ask (POSIX: the
// window system delivers frame callbacks / a display link instead).

#include <jaal/platform/display_clock.hpp>

namespace jaal::platform {

struct display_clock::impl {};

display_clock::display_clock() : p_(std::make_unique<impl>()) {}
display_clock::display_clock(display_clock&&) noexcept = default;
display_clock& display_clock::operator=(display_clock&&) noexcept = default;
display_clock::~display_clock() = default;

result<display_clock> display_clock::open() {
    return std::unexpected(error::make(std::errc::not_supported, "display_clock: not on this OS"));
}
borrowed_handle display_clock::handle() const noexcept { return borrowed_handle{}; }
void display_clock::arm(bool) noexcept {}
void display_clock::boost(bool) noexcept {}
display_clock::frame display_clock::tick() noexcept { return {}; }

}  // namespace jaal::platform
