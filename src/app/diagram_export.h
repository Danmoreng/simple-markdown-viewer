#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "render/mermaid_renderer.h"
#include "render/theme.h"

namespace mdviewer {

std::filesystem::path SuggestedDiagramExportPath(const std::filesystem::path& documentPath);
bool SaveDiagramSvg(const MermaidLayout& diagram, ThemeMode theme,
    const std::filesystem::path& destination, std::string& error);

// Preview files remain in the system temp directory so external viewers can
// continue reading them after this process closes. They are left to normal
// temporary-file cleanup. A private directory isolates each exported preview.
std::optional<std::filesystem::path> CreateDiagramPreview(
    const MermaidLayout& diagram, ThemeMode theme, std::string& error);

} // namespace mdviewer
