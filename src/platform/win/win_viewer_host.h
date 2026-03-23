#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

#include "app/viewer_controller.h"
#include "render/document_renderer.h"
#include "render/document_typefaces.h"
#include "render/image_cache.h"
#include "render/theme.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#pragma warning(pop)

namespace mdviewer::win {

inline constexpr float kScrollbarWidth = 10.0f;
inline constexpr float kScrollbarMargin = 4.0f;

class WinFileWatcher;

struct ViewerHostContext {
    ViewerController& controller;
    sk_sp<SkSurface>& surface;
    DocumentTypefaceCache& typefaces;
    DocumentImageCache& imageCache;
    WinFileWatcher& fileWatcher;
};

AppState& GetAppState(const ViewerHostContext& context);

ThemePalette GetCurrentThemePalette(const ViewerHostContext& context);
bool CanZoomIn(const ViewerHostContext& context);
bool CanZoomOut(const ViewerHostContext& context);
bool EnsureFontSystem(ViewerHostContext& context);
DocumentTypefaceSet GetDocumentTypefaces(ViewerHostContext& context);
SkTypeface* GetRegularTypeface(ViewerHostContext& context);
SkTypeface* GetMenuTypeface(ViewerHostContext& context);

float GetContentTopInset();
float GetViewportHeight(HWND hwnd, const ViewerHostContext& context);
float GetMaxScroll(HWND hwnd, const ViewerHostContext& context);
void ClampScrollOffset(HWND hwnd, const ViewerHostContext& context);

void SyncMenuState(HWND hwnd, const ViewerHostContext& context);
void UpdateSurface(HWND hwnd, ViewerHostContext& context);
std::optional<SkRect> GetScrollbarThumbRect(HWND hwnd, const ViewerHostContext& context);

void Render(HWND hwnd, ViewerHostContext& context);
bool LoadFile(HWND hwnd, ViewerHostContext& context, const std::filesystem::path& path, bool pushHistory = true);
void GoBack(HWND hwnd, ViewerHostContext& context);
void GoForward(HWND hwnd, ViewerHostContext& context);
void HandleLinkClick(HWND hwnd, ViewerHostContext& context, const std::string& url, bool forceExternal);
void RelayoutCurrentDocument(HWND hwnd, ViewerHostContext& context);
bool ReloadCurrentFile(HWND hwnd, ViewerHostContext& context, bool preserveScrollOffset = true);
void ReloadIfFileChanged(HWND hwnd, ViewerHostContext& context);
void SetBaseFontSize(HWND hwnd, ViewerHostContext& context, float baseFontSize);
void AdjustBaseFontSize(HWND hwnd, ViewerHostContext& context, float delta);
void ApplySelectedFont(HWND hwnd, ViewerHostContext& context, const std::string& familyUtf8);
void ApplyTheme(HWND hwnd, ViewerHostContext& context, ThemeMode theme);

} // namespace mdviewer::win
