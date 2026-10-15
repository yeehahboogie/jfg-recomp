#include "jfg_crash_handler.h"

#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <initializer_list>
#include <unistd.h>

namespace {
const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default: return "?";
    }
}

void on_crash(int sig, siginfo_t* info, void*) {
    char line[160];
    const int len = std::snprintf(line, sizeof line, "[crash] signal %d (%s) at address 0x%llx\n[crash] backtrace:\n", sig,
                                  signal_name(sig), (unsigned long long)(uintptr_t)(info != nullptr ? info->si_addr : nullptr));
    if (len > 0) (void)!write(STDERR_FILENO, line, (size_t)len);
    void* frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    // Die by the same signal so the exit status still says what happened.
    std::signal(sig, SIG_DFL);
    raise(sig);
}
}

void jfg::install_crash_handler() {
    // A separate stack, so a stack overflow can still be reported.
    static char alt_stack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof alt_stack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
}
