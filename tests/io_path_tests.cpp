#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>

#include "app/document_loader.h"
#include "app/link_resolver.h"
#include "app/viewer_controller.h"
#include "render/typography.h"
#include "util/file_io.h"
#include "util/path_utils.h"
#include "util/skia_font_utils.h"
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace mdviewer::tests {
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Directory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("mdviewer-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { Check(std::filesystem::create_directory(path), "create isolated IO fixture"); }
    ~Directory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    Check(output.good(), "write IO fixture");
}
}

void RunIoPathTests() {
    Directory directory;
    const auto path = directory.path / "short.md";
    for (const auto& text : {std::string(), std::string("x"), std::string("xy"), std::string("xyz")}) {
        Write(path, text);
        auto read = ReadFileToString(path);
        Check(read && *read == text, "short file preserves content without a BOM");
    }
    Write(path, "\xEF\xBB\xBFx");
    Check(ReadFileToString(path) == std::optional<std::string>("x"), "strip UTF8 BOM");
    Write(path, "123456");
    Check(ReadFileBounded(path, 5).status == FileReadStatus::TooLarge, "read byte limit enforced");
    Check(ReadFileBounded(path, 6).content == "123456", "exact read limit accepted");
    Check(ReadFileBounded(directory.path, 6).status == FileReadStatus::ReadError, "reject directory");
#ifndef _WIN32
    const auto fifo = directory.path / "pipe";
    Check(mkfifo(fifo.c_str(), 0600) == 0, "create FIFO fixture");
    Check(ReadFileBounded(fifo, 16).status == FileReadStatus::ReadError, "reject FIFO without opening");
    Check(ReadFileBounded("/dev/zero", 16).status == FileReadStatus::ReadError, "reject character device");
#endif
    const auto current = directory.path / "index.md";
    Write(current, "# Index\n");
    for (const auto& name : {std::string("A+B.md"), std::string("日本語.md"), std::string("A#B.md")}) {
        const auto target = directory.path / PathFromUtf8(name);
        Write(target, "# Target\n");
        const auto encoded = name == "A#B.md" ? std::string("A%23B.md") : name;
        const auto resolved = ResolveLinkTarget(current, encoded, false);
        Check(resolved.kind == LinkTargetKind::InternalDocument && resolved.path == target,
            "UTF8/plus/percent-encoded-hash links resolve");
        std::string uri = "file:///" + PathToUtf8(target);
#ifndef _WIN32
        uri = "file://" + PathToUtf8(target);
#endif
        const auto hash = uri.find('#');
        if (hash != std::string::npos) uri.replace(hash, 1, "%23");
        Check(ResolveLinkTarget(current, uri, false).path == target, "file URI preserves UTF8 and encoded delimiters");
    }
    for (const auto* invalid : {"bad%.md", "bad%GG.md", "bad%0.md", "%00.md"})
        Check(ResolveLinkTarget(current, invalid, false).kind == LinkTargetKind::Invalid, "invalid percent encoding rejected");
    Check(ResolveLinkTarget(current, "#A+B", false).fragment == "A+B", "fragment plus is literal");
    Check(NormalizeDocumentPath("README.md").is_absolute(), "document paths become absolute");
    Check(ClampBaseFontSize(std::numeric_limits<float>::quiet_NaN()) == kDefaultBaseFontSize,
        "NaN font size uses safe default");
    Check(ClampBaseFontSize(std::numeric_limits<float>::infinity()) == kDefaultBaseFontSize,
        "infinite font size uses safe default");
}

void RunAsyncLayoutTests() {
    Directory directory;
    const auto first = directory.path / "first.md";
    const auto second = directory.path / "second.md";
    std::string source;
    for (int i = 0; i < 500; ++i) source += "A paragraph with enough words to wrap across several lines.\n\n";
    source += "![image](https://example.invalid/image.png)\n";
    Write(first, source);
    Write(second, "# Second\n");
    auto manager = CreateFontManager();
    auto regular = CreateDefaultTypeface(manager);
    DocumentTypefaceSet fonts{manager.get(), regular.get(), regular.get(), regular.get(), regular.get()};
    ViewerController controller;
    Check(controller.OpenFile(first, 900, fonts, {}, {}) == OpenDocumentStatus::Success, "load layout fixture");
    const auto callerThread = std::this_thread::get_id();
    LayoutOptions options;
    options.imageLoadInfoProvider = [callerThread](const std::string& url) {
        Check(std::this_thread::get_id() == callerThread, "image state must be snapshotted on caller thread");
        return DescribeImageSource(url, {}).info;
    };
    auto imageSizes = [callerThread](const std::string&) {
        Check(std::this_thread::get_id() == callerThread, "image sizes must be snapshotted on caller thread");
        return std::pair{0.0f, 0.0f};
    };
    controller.QueueRelayout(250, fonts, imageSizes, options);
    controller.QueueRelayout(700, fonts, imageSizes, options);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool applied = false;
    while (controller.IsRelayoutPending() && std::chrono::steady_clock::now() < deadline) {
        applied = controller.PollRelayout() || applied;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(applied && !controller.IsRelayoutPending() && controller.GetLastLayoutError().empty(), "latest background layout completes");
    const auto expected = LayoutEngine::ComputeLayout(controller.GetAppState().docModel, 700, fonts,
        kDefaultBaseFontSize, imageSizes, options);
    Check(controller.GetAppState().docLayout.totalHeight == expected.totalHeight, "only latest resize is committed");
    controller.QueueRelayout(200, fonts, imageSizes, options);
    Check(controller.OpenFile(second, 700, fonts, {}, {}) == OpenDocumentStatus::Success, "open replacement document");
    Check(!controller.PollRelayout() && !controller.IsRelayoutPending(), "old layout cannot replace new document");
    Check(controller.GetAppState().currentFilePath == second, "new document remains active");
}
} // namespace mdviewer::tests
