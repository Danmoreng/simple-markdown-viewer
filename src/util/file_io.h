#pragma once
#include <string>
#include <optional>
#include <filesystem>
#include <cstddef>

namespace mdviewer {

inline constexpr size_t kDefaultMaximumFileBytes = 64ULL * 1024ULL * 1024ULL;
enum class FileReadStatus { Success, ReadError, TooLarge };
struct FileReadResult {
    FileReadStatus status = FileReadStatus::ReadError;
    std::string content;
};
FileReadResult ReadFileBounded(const std::filesystem::path& path, size_t maximumBytes);
std::optional<std::string> ReadFileToString(const std::filesystem::path& path);

} // namespace mdviewer
