#include "bw/platform/diagnostics/debugger.hpp"

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace {
// Real tracing establishes process state independently of the production query.
// A denied PT_TRACE_ME is failure, not a silently skipped contract.
bool child_query(bool traced) {
  const pid_t child = fork();
  if (child < 0) {
    std::perror("fork");
    return false;
  }
  if (child == 0) {
    if (traced && ptrace(PT_TRACE_ME, 0, nullptr, 0) != 0) {
      _exit(2);
    }
    _exit(bw::platform::is_debugger_present() == traced ? 0 : 1);
  }
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::fprintf(stderr, "native debugger query mismatch or tracing unavailable (status %d)\n",
                 status);
    return false;
  }
  return true;
}
} // namespace

int main() { return child_query(false) && child_query(true) ? 0 : 1; }
