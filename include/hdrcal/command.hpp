// Direct process execution without a shell, with captured stdout and stderr.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace hdrcal {

struct CommandResult {
    int exitCode = -1;
    std::string standardOutput;
    std::string standardError;
};

using CommandExecutor = std::function<CommandResult(const std::vector<std::string>&)>;

// Executes an argument vector directly (no shell). The returned exit code is
// 128 + signal number when the child is terminated by a signal, and -1 when
// the command could not be started or its output could not be read.
[[nodiscard]] CommandResult runCommand(const std::vector<std::string>& arguments);

} // namespace hdrcal
