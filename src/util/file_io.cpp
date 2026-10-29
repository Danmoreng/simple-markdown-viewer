#include "util/file_io.h"
#include <algorithm>
#include <array>
#include <fstream>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mdviewer {

FileReadResult ReadFileBounded(const std::filesystem::path& path, size_t maximumBytes) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return {};
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size > maximumBytes) return {FileReadStatus::TooLarge, {}};
    std::string content;
    if (!error) content.reserve(static_cast<size_t>(std::min<uintmax_t>(size, maximumBytes)));
    std::array<char, 64 * 1024> buffer{};
#ifdef _WIN32
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    for (;;) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const size_t count = static_cast<size_t>(input.gcount());
        if (count > maximumBytes - content.size()) return {FileReadStatus::TooLarge, {}};
        content.append(buffer.data(), count);
        if (input.bad()) return {};
        if (input.eof()) break;
        if (input.fail()) return {};
    }
#else
    // Avoid blocking on a path replaced with a FIFO between stat and open.
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) return {};
    struct CloseFile { int value; ~CloseFile() { close(value); } } closeFile{descriptor};
    struct stat info{};
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)) return {};
    for (;;) {
        const auto count = read(descriptor, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return {};
        if (count == 0) break;
        if (static_cast<size_t>(count) > maximumBytes - content.size())
            return {FileReadStatus::TooLarge, {}};
        content.append(buffer.data(), static_cast<size_t>(count));
    }
#endif
    if (content.starts_with("\xEF\xBB\xBF")) content.erase(0, 3);
    return {FileReadStatus::Success, std::move(content)};
}

std::optional<std::string> ReadFileToString(const std::filesystem::path& path) {
    auto result = ReadFileBounded(path, kDefaultMaximumFileBytes);
    if (result.status != FileReadStatus::Success) return std::nullopt;
    return std::move(result.content);
}
} // namespace mdviewer
