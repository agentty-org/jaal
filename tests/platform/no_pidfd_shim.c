// tests/platform/no_pidfd_shim.c — preload this to make pidfd_open and
// pidfd_send_signal fail with ENOSYS, as on a kernel older than 5.3 or under
// a seccomp policy that denies them. The process conformance suite is run a
// second time under it, so the fallback exit path faces the same checks.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/syscall.h>

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif

long syscall(long n, ...) {
    va_list ap;
    va_start(ap, n);
    long a[6];
    for (int i = 0; i < 6; ++i) a[i] = va_arg(ap, long);
    va_end(ap);
    if (n == SYS_pidfd_open || n == SYS_pidfd_send_signal) {
        errno = ENOSYS;
        return -1;
    }
    static long (*real)(long, ...) = 0;
    if (!real) real = (long (*)(long, ...))dlsym(RTLD_NEXT, "syscall");
    return real(n, a[0], a[1], a[2], a[3], a[4], a[5]);
}
