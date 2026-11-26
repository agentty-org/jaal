#pragma once
// jaal::platform::display_clock — the display's own frame clock, as a handle.
//
// A UI that animates must draw on the DISPLAY's cadence, not on a timer that
// guesses it. Guessing is what lags: a timer grid laid on a one-off vblank
// reading drifts (60.006 Hz is not 60), a stale phase puts every late draw
// just after the deadline instead of just before it, and a whole refresh of
// latency appears with no slow code anywhere. The OS knows when it composes;
// this asks it.
//
// The shape is jaal's: the clock is a WAITABLE HANDLE the reactor watches
// like a socket or a child's exit, so a host waits on input, output, timers
// and the next frame in ONE wait() -- no callback thread reaching into the
// program, no second loop. While armed, the handle becomes readable once per
// compositor frame; tick() then says when that frame began. Disarmed (the
// default), it never fires and costs nothing: an idle app takes no wakeups.
//
//   Windows 11   DCompositionWaitForCompositorClock -- DWM's own frame clock,
//                per-target, ~0.1 ms after scan-out's vblank (measured: 16.67
//                +- 0.07 ms at 60 Hz). Windows 10: DwmFlush, the same cadence.
//                boost() asks DWM for its higher refresh while motion is
//                user-driven (DCompositionBoostCompositorClock).
//   elsewhere    not supported (open() fails): Wayland and macOS deliver
//                frame callbacks / a display link through their windows.
//
// The blocking wait lives on a thread jaal owns, inside this capability
// (as the pipe reader does): nothing above it waits on the compositor.

#include <chrono>
#include <cstdint>
#include <memory>

#include "../core/error.hpp"
#include "../core/sendable.hpp"
#include "handle.hpp"

namespace jaal::platform {

class display_clock {
  public:
    display_clock(display_clock&&) noexcept;
    display_clock& operator=(display_clock&&) noexcept;
    display_clock(const display_clock&)            = delete;
    display_clock& operator=(const display_clock&) = delete;
    ~display_clock();

    /// The compositor's clock, or errc::not_supported where there is none
    /// to ask (a host then keeps its own pacing).
    [[nodiscard]] static result<display_clock> open();

    /// Readable once per compositor frame while armed. Watch it with
    /// interest::read; reading it is calling tick().
    [[nodiscard]] borrowed_handle handle() const noexcept;

    /// Tick while on. Idempotent; cheap enough to call every turn.
    void arm(bool on) noexcept;

    /// Ask for the display's higher refresh rate while on (no-op where the
    /// OS has none). For user-driven motion only: it costs power.
    void boost(bool on) noexcept;

    struct frame {
        std::chrono::steady_clock::time_point at{};   ///< when the compositor frame began
        std::chrono::nanoseconds period{};            ///< its cadence (0: not known yet)
        std::uint64_t seq = 0;                        ///< bumps per frame
    };
    /// The latest frame. Clears the handle's readiness.
    [[nodiscard]] frame tick() noexcept;

  private:
    display_clock();
    struct impl;
    std::unique_ptr<impl> p_;
};

}  // namespace jaal::platform

template <> inline constexpr bool jaal::sendable_opt_in<jaal::platform::display_clock> = true;
