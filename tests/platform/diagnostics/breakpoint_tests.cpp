#include "bw/platform/diagnostics/debugger.hpp"

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>

int main() {
  const pid_t child = fork();
  if (child < 0) {
    std::perror("fork");
    return 1;
  }
  if (child == 0) {
    if (ptrace(PT_TRACE_ME, 0, nullptr, 0) != 0) {
      _exit(2);
    }
    // Exercise the actual caller-site native trap.
    BW_PLATFORM_BREAK();
    _exit(3); // A parent that has not resumed the trap must never observe this.
  }
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, WUNTRACED);
  } while (waited < 0 && errno == EINTR);
  const bool trapped = waited == child && WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP;
  if (waited == child && WIFSTOPPED(status)) {
    // Darwin ptrace(2): PT_KILL resumes the traced stop with SIGKILL; a plain
    // kill leaves this stopped tracee waiting for debugger continuation.
    if (ptrace(PT_KILL, child, nullptr, 0) != 0) {
      std::perror("ptrace kill traced child");
      return 1;
    }
    do {
      waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child || !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
      std::fprintf(stderr, "failed to reap traced child\n");
      return 1;
    }
  }
  if (!trapped) {
    std::fprintf(stderr, "native breakpoint did not stop with SIGTRAP\n");
    return 1;
  }
  return 0;
}
