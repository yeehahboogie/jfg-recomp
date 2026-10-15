#include <cassert>
#include <csignal>
#include <cstdio>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include "jfg_crash_handler.h"

// A crash reports the signal, the faulting address and a backtrace on stderr, then still dies by the signal.
int main() {
    int fds[2];
    assert(pipe(fds) == 0);
    const pid_t child = fork();
    if (child == 0) {
        dup2(fds[1], 2);
        close(fds[0]);
        jfg::install_crash_handler();
        volatile int* null_pointer = nullptr;
        return *null_pointer;
    }
    close(fds[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
    int status = 0;
    waitpid(child, &status, 0);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    assert(out.find("[crash] signal 11 (SIGSEGV) at address 0x0") != std::string::npos);
    assert(out.find("[crash] backtrace:") != std::string::npos);
    std::puts("crash_handler_test: OK");
}
