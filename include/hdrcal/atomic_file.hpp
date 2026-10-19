#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hdrcal {

// Writes exactly the given bytes to destinationPath using a temporary file in
// the same directory, fsync and rename. Missing parent directories are created.
// On success the destination contains either the complete previous content or
// the complete new content, never a truncated file. Binary content, including
// NUL bytes, is preserved.
[[nodiscard]] std::optional<std::string> writeFileAtomically(const std::filesystem::path& destinationPath, std::string_view content);

} // namespace hdrcal
