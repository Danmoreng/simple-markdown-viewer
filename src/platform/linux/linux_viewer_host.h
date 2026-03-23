#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "platform/linux/linux_app.h"
#include "render/document_renderer.h"
#include "render/theme.h"

#include "include/core/SkRect.h"

namespace mdviewer::linux_platform {

inline constexpr float kScrollbarWidth = 10.0f;
inline constexpr float kScrollbarMargin = 4.0f;

AppState& GetAppState(const LinuxHostContext& context);
ThemePalette GetCurrentThemePalette(const LinuxHostContext& context);

bool EnsureFontSystem(LinuxHostContext context);
SkTypeface* GetRegularTypeface(LinuxHostContext context);
SkTypeface* GetMenuTypeface(LinuxHostContext context);

float GetContentTopInset();
float GetViewportHeight(GLFWwindow* window, const LinuxHostContext context);
float GetMaxScroll(GLFWwindow* window, const LinuxHostContext context);
void ClampScrollOffset(GLFWwindow* window, const LinuxHostContext context);

void Render(GLFWwindow* window, LinuxHostContext context);
bool LoadFile(GLFWwindow* window, LinuxHostContext context, const std::filesystem::path& path, bool pushHistory = true);
void GoBack(GLFWwindow* window, LinuxHostContext context);
void GoForward(GLFWwindow* window, LinuxHostContext context);
void RelayoutCurrentDocument(GLFWwindow* window, LinuxHostContext context);
void HandleLinkClick(GLFWwindow* window, LinuxHostContext context, const std::string& url, bool forceExternal);
void ApplyTheme(GLFWwindow* window, LinuxHostContext context, ThemeMode theme);
void ApplySelectedFont(GLFWwindow* window, LinuxHostContext context, const std::string& familyUtf8);
void AdjustBaseFontSize(GLFWwindow* window, LinuxHostContext context, float delta);

std::optional<SkRect> GetScrollbarThumbRect(GLFWwindow* window, const LinuxHostContext context);

} // namespace mdviewer::linux_platform
