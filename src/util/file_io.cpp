#include "file_io.h"
#include <fstream>
#include <sstream>

namespace mdviewer {

std::optional<std::string> ReadFileToString(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    // Check for UTF-8 BOM
    unsigned char bom[3];
    file.read(reinterpret_cast<char*>(bom), 3);
    if (file.gcount() == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        // UTF-8 BOM found, skip it (already read)
    } else {
        // No BOM, reset to beginning
        file.seekg(0);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace mdviewer
