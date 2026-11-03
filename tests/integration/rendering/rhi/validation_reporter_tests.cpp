// Integration test: the validation layer's default reporter
// (backend.hpp:383-385, report_validation_failure_and_abort: one diagnostic
// line naming the code, the resource and the command, then abort). It needs
// a child process, which is why it is an integration test: the reporter runs
// in a forked child whose stderr is a pipe, and the parent asserts the child
// was terminated by SIGABRT and that the one line it wrote names the code,
// the transition error (ValidationFailure::transition), the resource and the
// command. No GPU and no backend: the failure is constructed directly.
// POSIX only (fork, pipe, waitpid), like the hosts this test runs on.
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/transitions.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

#if !BW_RHI_DEBUG
#error "rhi_validation_reporter_tests is built only with BW_RHI_DEBUG 1"
#endif

namespace {

// Exit code of a child whose reporter returned instead of aborting.
constexpr int kReporterReturned = 3;

bool contains(std::string_view text, std::string_view part) {
  return text.find(part) != std::string_view::npos;
}

int fail(std::string_view message) {
  std::cerr << "default_reporter_prints_and_aborts: " << message << '\n';
  return 1;
}

int default_reporter_prints_and_aborts() {
  std::array<int, 2> pipe_ends{};
  if (pipe(pipe_ends.data()) != 0) {
    return fail("pipe failed");
  }
  const pid_t child = fork();
  if (child < 0) {
    return fail("fork failed");
  }
  if (child == 0) {
    close(pipe_ends[0]);
    if (dup2(pipe_ends[1], STDERR_FILENO) < 0) {
      _exit(kReporterReturned + 1);
    }
    close(pipe_ends[1]);
    const bw::rhi::ValidationFailure failure{.code = bw::rhi::ValidationCode::Transition,
                                             .transition =
                                                 bw::rhi::TransitionError::AccessMissingAtUse,
                                             .resource_name = "Probe resource",
                                             .command = "copy_buffer"};
    bw::rhi::report_validation_failure_and_abort(failure, nullptr);
    _exit(kReporterReturned);
  }
  close(pipe_ends[1]);
  std::string output;
  std::array<char, 256> chunk{};
  for (;;) {
    const ssize_t count = read(pipe_ends[0], chunk.data(), chunk.size());
    if (count <= 0) {
      break;
    }
    output.append(chunk.data(), static_cast<std::size_t>(count));
  }
  close(pipe_ends[0]);
  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    return fail("waitpid failed");
  }
  if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {
    return fail("the child was not terminated by SIGABRT");
  }
  if (output.empty() || output.back() != '\n' || output.find('\n') != output.size() - 1) {
    return fail("expected exactly one line, got: " + output);
  }
  for (const std::string_view part :
       {std::string_view("Transition"), std::string_view("AccessMissingAtUse"),
        std::string_view("Probe resource"), std::string_view("copy_buffer")}) {
    if (!contains(output, part)) {
      return fail("the line does not name '" + std::string(part) + "': " + output);
    }
  }
  return 0;
}

} // namespace

// The scenario builds strings, which may throw; main reports any exception
// as a failure instead of letting it escape.
int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      std::cerr << "Provide one scenario name\n";
      return 2;
    }
    if (std::string_view(argv[1]) == "default_reporter_prints_and_aborts") {
      return default_reporter_prints_and_aborts();
    }
    std::cerr << "Unknown scenario\n";
    return 2;
  } catch (...) {
    return 1;
  }
}
