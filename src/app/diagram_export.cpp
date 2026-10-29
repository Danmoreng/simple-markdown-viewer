#include "app/diagram_export.h"

#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <tinyxml2.h>

namespace mdviewer {

std::filesystem::path SuggestedDiagramExportPath(const std::filesystem::path& documentPath) {
    if (documentPath.empty()) return "diagram.svg";
    auto filename = documentPath.stem();
    filename += "-diagram.svg";
    return documentPath.parent_path() / filename;
}

bool SaveDiagramSvg(const MermaidLayout& diagram, ThemeMode theme,
    const std::filesystem::path& destination, std::string& error) {
    error.clear();
    const auto svg = GetMermaidSvg(diagram, theme == ThemeMode::Dark);
    if (svg.empty()) {
        error = "This diagram has no rendered SVG to save.";
        return false;
    }
    tinyxml2::XMLDocument document;
    if (document.Parse(svg.data(), svg.size()) != tinyxml2::XML_SUCCESS || !document.RootElement()) {
        error = "The rendered diagram could not be exported as SVG.";
        return false;
    }
    // Give standalone images the same opaque panel as the document. In
    // particular, dark-theme text must remain readable in light image viewers.
    auto* background = document.RootElement()->FirstChildElement("rect");
    if (!background) {
        error = "The rendered diagram has no export background.";
        return false;
    }
    std::ostringstream color;
    color << '#' << std::hex << std::setfill('0') << std::setw(6)
          << (GetThemePalette(theme).codeBlockBackground & 0x00ffffff);
    background->SetAttribute("fill", color.str().c_str());
    tinyxml2::XMLPrinter printer(nullptr, true);
    document.Print(&printer);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "The SVG file could not be opened for writing.";
        return false;
    }
    output.write(printer.CStr(), static_cast<std::streamsize>(printer.CStrSize() - 1));
    output.close();
    if (!output) {
        error = "The SVG file could not be completely written.";
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> CreateDiagramPreview(
    const MermaidLayout& diagram, ThemeMode theme, std::string& error) {
    error.clear();
    if (!diagram.IsValid()) {
        error = "This diagram has no rendered SVG to open.";
        return std::nullopt;
    }
    try {
        const auto root = std::filesystem::temp_directory_path();
        std::random_device random;
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::ostringstream name;
            name << "mdviewer-mermaid-" << std::hex << std::setfill('0');
            for (int part = 0; part < 4; ++part) name << std::setw(8) << random();
            const auto directory = root / name.str();
            std::error_code status;
            if (!std::filesystem::create_directory(directory, status)) {
                if (status) break;
                continue;
            }
            std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                std::filesystem::perm_options::replace, status);
            if (status) {
                std::filesystem::remove(directory, status);
                break;
            }
            const auto path = directory / "diagram.svg";
            if (SaveDiagramSvg(diagram, theme, path, error)) return path;
            std::filesystem::remove(path, status);
            std::filesystem::remove(directory, status);
            return std::nullopt;
        }
    } catch (const std::exception&) {
        // Native callers show one concise error instead of exposing temp paths.
    }
    error = "A temporary SVG file could not be created.";
    return std::nullopt;
}

} // namespace mdviewer
