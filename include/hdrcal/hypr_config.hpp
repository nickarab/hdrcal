// Read-only scan of the Hyprland configuration directory for monitor rules.
// Used by the ICC preflight check and by the temporary luminance rule guard.

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hdrcal {

// One logical line of a Hyprland configuration file, with Lua `--` / `--[[ ]]`
// and legacy `#` comments removed. Quoted strings are preserved verbatim so a
// rule value can be compared without a full Lua interpreter.
struct HyprConfigLine {
    std::filesystem::path filePath;
    std::size_t lineNumber = 0;
    std::string text;
};

struct HyprConfigLinesResult {
    std::vector<HyprConfigLine> lines;
    std::optional<std::string> error;
};

// Recursively reads every `.lua` and `.conf` file below configDirectory, sorted
// by path. An unreadable file or directory is returned as an error: a
// conservative caller must not assume the configuration is safe when it cannot
// inspect it.
[[nodiscard]] HyprConfigLinesResult readHyprConfigLines(const std::filesystem::path& configDirectory);

struct MonitorRuleLocation {
    // "path:line" of the rule that names outputName literally, when found.
    std::optional<std::string> location;
    std::optional<std::string> error;
};

enum class MonitorRuleSyntax {
    Lua,
    Legacy,
};

struct MonitorRuleMatch {
    std::filesystem::path filePath;
    std::size_t lineNumber = 0;
    MonitorRuleSyntax syntax = MonitorRuleSyntax::Lua;
};

struct MonitorRuleMatchesResult {
    std::vector<MonitorRuleMatch> matches;
};

// Returns every monitor rule whose selector is exactly outputName, across all
// scanned files. The integration command needs the full list to refuse an
// ambiguous configuration instead of editing the first match. The single-match
// helper below keeps the preflight/guard behavior.
[[nodiscard]] MonitorRuleMatchesResult findMonitorRuleMatches(const std::vector<HyprConfigLine>& lines, std::string_view outputName);

// Finds a monitor rule whose selector is exactly outputName. Both the Lua form
// (`hl.monitor({ output = "DP-2", ... })`, including multi-line calls) and the
// legacy form (`monitor=DP-2,...` in a .conf file) are recognized. Rules keyed
// by `desc:...`, `make:...` or a wildcard do not match, because Hyprland's
// partial `hl.monitor` merge only copies a rule whose m_name is equal.
[[nodiscard]] MonitorRuleLocation findMonitorRuleLocation(const std::vector<HyprConfigLine>& lines, std::string_view outputName);

} // namespace hdrcal
