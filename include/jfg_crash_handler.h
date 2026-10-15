#ifndef JFG_CRASH_HANDLER_H
#define JFG_CRASH_HANDLER_H
namespace jfg {
// On SIGSEGV, SIGBUS, SIGILL, SIGFPE or SIGABRT: print the signal, the faulting address and a backtrace to stderr
// (recompiled functions keep their func_/mNNN_ names, so the trace names the N64 routine), then die by the signal.
void install_crash_handler();
}
#endif
