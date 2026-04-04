#include "platform/win/win_crash_handler.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

#include <dbghelp.h>

namespace mdviewer::win {
namespace {

std::filesystem::path g_outputDirectory;

std::filesystem::path ResolveWritableOutputDirectory() {
    std::error_code error;
    if (!g_outputDirectory.empty()) {
        std::filesystem::create_directories(g_outputDirectory, error);

        const std::filesystem::path probePath = g_outputDirectory / L".mdviewer-crash-probe";
        std::ofstream probe(probePath, std::ios::out | std::ios::trunc);
        if (probe.is_open()) {
            probe.close();
            std::filesystem::remove(probePath, error);
            return g_outputDirectory;
        }
    }

    const std::filesystem::path tempDirectory = std::filesystem::temp_directory_path(error);
    if (!error && !tempDirectory.empty()) {
        return tempDirectory;
    }

    return std::filesystem::current_path(error);
}

std::wstring MakeTimestampSuffix() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime = {};
    localtime_s(&localTime, &currentTime);

    std::wostringstream stream;
    stream << std::put_time(&localTime, L"%Y%m%d-%H%M%S");
    return stream.str();
}

void WriteCrashReport(
    const std::filesystem::path& reportPath,
    const std::filesystem::path& dumpPath,
    const EXCEPTION_POINTERS* exceptionInfo) {
    std::ofstream report(reportPath, std::ios::out | std::ios::trunc);
    if (!report.is_open()) {
        return;
    }

    report << "mdviewer unhandled exception\n";
    if (exceptionInfo && exceptionInfo->ExceptionRecord) {
        report << std::hex;
        report << "exception_code=0x" << exceptionInfo->ExceptionRecord->ExceptionCode << '\n';
        report << "exception_address=0x"
               << reinterpret_cast<std::uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionAddress) << '\n';
    }
    report << "dump_path=" << dumpPath.string() << '\n';
}

LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* exceptionInfo) {
    const std::filesystem::path outputDirectory = ResolveWritableOutputDirectory();
    const std::wstring stamp = MakeTimestampSuffix();
    const std::filesystem::path dumpPath = outputDirectory / (L"mdviewer-crash-" + stamp + L".dmp");
    const std::filesystem::path reportPath = outputDirectory / (L"mdviewer-crash-" + stamp + L".txt");

    HANDLE dumpFile = CreateFileW(
        dumpPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (dumpFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo = {};
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = exceptionInfo;
        dumpInfo.ClientPointers = FALSE;
        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            dumpFile,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
            exceptionInfo ? &dumpInfo : nullptr,
            nullptr,
            nullptr);
        CloseHandle(dumpFile);
    }

    WriteCrashReport(reportPath, dumpPath, exceptionInfo);

    const std::wstring message =
        L"Markdown Viewer encountered an unexpected error.\n\nCrash data was written to:\n" +
        dumpPath.wstring();
    MessageBoxW(nullptr, message.c_str(), L"Markdown Viewer Crash", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void InstallCrashHandler(const std::filesystem::path& preferredOutputDirectory) {
    g_outputDirectory = preferredOutputDirectory;
    SetUnhandledExceptionFilter(HandleUnhandledException);
}

} // namespace mdviewer::win
