#include "platform/linux/linux_viewer_host.h"

#include "app/document_loader.h"
#include "platform/linux/linux_menu.h"
#include "render/menu_renderer.h"
#include "render/typography.h"
#include "platform/linux/linux_shell.h"
#include "util/skia_font_utils.h"

#include "include/core/SkCanvas.h"

namespace mdviewer::linux_platform {

namespace {

std::vector<MenuBarItem> GetMenuBarItems() {
    return {
        {"File", 1},
        {"View", 2},
        {"Theme", 3},
    };
}

} // namespace

AppState& GetAppState(const LinuxHostContext& context) {
    return context.controller.GetMutableAppState();
}

ThemePalette GetCurrentThemePalette(const LinuxHostContext& context) {
    return GetThemePalette(context.controller.GetTheme());
}

bool EnsureFontSystem(LinuxHostContext context) {
    auto& appState = GetAppState(context);
    if (!appState.fontSystem) {
        appState.fontSystem = CreateSkiaFontSystem();
    }
    context.typefaces.EnsureInitialized(context.controller.GetFontFamilyUtf8());
    return appState.fontSystem != nullptr;
}

SkTypeface* GetRegularTypeface(LinuxHostContext context) {
    const std::string& family = context.controller.GetFontFamilyUtf8();
    return context.typefaces.GetOrCreateTypeface(family, SkFontStyle::Normal());
}

float GetContentTopInset() {
    // Height of the custom menu bar
    return 30.0f; 
}

float GetViewportHeight(GLFWwindow* window, const LinuxHostContext context) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    return static_cast<float>(height) - GetContentTopInset();
}

float GetMaxScroll(GLFWwindow* window, const LinuxHostContext context) {
    const auto& appState = GetAppState(context);
    const float viewportHeight = GetViewportHeight(window, context);
    return std::max(0.0f, appState.docLayout.totalHeight - viewportHeight);
}

void ClampScrollOffset(GLFWwindow* window, const LinuxHostContext context) {
    auto& appState = GetAppState(context);
    const float maxScroll = GetMaxScroll(window, context);
    if (appState.scrollOffset > maxScroll) {
        appState.scrollOffset = maxScroll;
    }
    if (appState.scrollOffset < 0) {
        appState.scrollOffset = 0;
    }
}

void Render(GLFWwindow* window, LinuxHostContext context) {
    if (!context.surface) return;

    SkCanvas* canvas = context.surface->getCanvas();
    auto& appState = GetAppState(context);
    const auto palette = GetCurrentThemePalette(context);

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    canvas->clear(palette.windowBackground);

    DocumentSceneParams params;
    params.canvas = canvas;
    params.appState = &appState;
    params.palette = palette;
    params.typefaces = context.typefaces.GetTypefaceSet();
    params.baseFontSize = appState.baseFontSize;
    params.contentTopInset = GetContentTopInset();
    params.viewportHeight = GetViewportHeight(window, context);
    params.surfaceWidth = static_cast<float>(width);
    params.surfaceHeight = static_cast<float>(height);
    params.scrollbarWidth = kScrollbarWidth;
    params.scrollbarMargin = kScrollbarMargin;
    params.currentTickCount = static_cast<uint64_t>(glfwGetTime() * 1000.0);
    params.visibleDocumentTop = appState.scrollOffset;
    params.visibleDocumentBottom = appState.scrollOffset + params.viewportHeight;
    params.scrollbarThumbRect = GetScrollbarThumbRect(window, context);
    
    params.resolveImage = [&context, &appState](const std::string& url, float dw, float dh) {
        return context.imageCache.GetImage(url, appState.currentFilePath.parent_path(), dw, dh);
    };
    params.addCodeBlockButton = [&appState](const SkRect& rect, size_t start, size_t end) {
        appState.codeBlockButtons.push_back({rect, {start, end}});
    };

    appState.codeBlockButtons.clear();
    RenderDocumentScene(params);

    // Render custom menu bar
    appState.menuBarState.canGoBack = appState.CanGoBack();
    appState.menuBarState.canGoForward = appState.CanGoForward();
    appState.menuBarState.canZoomIn = context.controller.CanZoomIn();
    appState.menuBarState.canZoomOut = context.controller.CanZoomOut();

    DrawMenuBar(
        *canvas,
        static_cast<float>(width),
        GetContentTopInset(),
        GetMenuBarItems(),
        appState.menuBarState,
        GetRegularTypeface(context),
        palette);

    // Render active dropdown
    if (appState.menuBarState.activeIndex >= 0) {
        auto menus = GetLinuxMenus();
        if (appState.menuBarState.activeIndex < static_cast<int>(menus.size())) {
            const auto& menu = menus[appState.menuBarState.activeIndex];
            std::vector<DropdownItem> dropItems;
            for (const auto& item : menu.items) {
                dropItems.push_back({item.label, item.isSeparator});
            }

            float currentX = 12.0f;
            SkFont font(sk_ref_sp(GetRegularTypeface(context)), 17.5f);
            for (int i = 0; i < appState.menuBarState.activeIndex; ++i) {
                SkRect b;
                auto barItems = GetMenuBarItems();
                font.measureText(barItems[i].label.c_str(), barItems[i].label.size(), SkTextEncoding::kUTF8, &b);
                currentX += b.width() + 20.0f + 8.0f;
            }

            DrawDropdown(
                *canvas,
                currentX,
                GetContentTopInset(),
                dropItems,
                appState.menuBarState.hoveredItemIndex,
                GetRegularTypeface(context),
                palette);
        }
    }
}

bool LoadFile(GLFWwindow* window, LinuxHostContext context, const std::filesystem::path& path, bool pushHistory) {
    if (!std::filesystem::exists(path)) {
        return false;
    }

    int width, height;
    glfwGetWindowSize(window, &width, &height);
    
    const float contentWidth = static_cast<float>(width) - kScrollbarWidth - kScrollbarMargin * 2.0f;

    auto* regularTypeface = GetRegularTypeface(context);
    if (!regularTypeface) {
        return false;
    }

    auto status = context.controller.OpenFile(
        path,
        contentWidth,
        regularTypeface,
        [&context](const DocumentModel& doc, const std::filesystem::path& base) {
            context.imageCache.PreloadDocumentImages(doc, base);
        },
        [&context, path](const std::string& url) {
            return context.imageCache.GetImageSize(url, path.parent_path());
        },
        pushHistory);

    if (status == OpenDocumentStatus::Success) {
        auto& appState = GetAppState(context);
        appState.scrollOffset = 0;
        appState.needsRepaint = true;
        return true;
    }
    return false;
}

void GoBack(GLFWwindow* window, LinuxHostContext context) {
    if (auto target = context.controller.GetHistoryNavigationTarget(HistoryDirection::Back)) {
        if (LoadFile(window, context, target->path, false)) {
            context.controller.CommitHistoryNavigation(target->index);
        }
    }
}

void GoForward(GLFWwindow* window, LinuxHostContext context) {
    if (auto target = context.controller.GetHistoryNavigationTarget(HistoryDirection::Forward)) {
        if (LoadFile(window, context, target->path, false)) {
            context.controller.CommitHistoryNavigation(target->index);
        }
    }
}

void HandleLinkClick(GLFWwindow* window, LinuxHostContext context, const std::string& url, bool forceExternal) {
    if (url.empty()) {
        return;
    }

    const auto target = context.controller.ResolveLinkTarget(url, forceExternal);
    switch (target.kind) {
        case LinkTargetKind::InternalDocument:
            LoadFile(window, context, target.path);
            break;
        case LinkTargetKind::ExternalUrl:
            OpenExternalUrl(target.externalUrl);
            break;
        case LinkTargetKind::ExternalPath:
            OpenPath(target.path);
            break;
        case LinkTargetKind::Invalid:
            break;
    }
}

void RelayoutCurrentDocument(GLFWwindow* window, LinuxHostContext context) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    const float contentWidth = static_cast<float>(width) - kScrollbarWidth - kScrollbarMargin * 2.0f;

    auto& appState = GetAppState(context);
    const auto currentPath = appState.currentFilePath;

    context.controller.Relayout(
        contentWidth,
        GetRegularTypeface(context),
        [&context](const DocumentModel& doc, const std::filesystem::path& base) {
            context.imageCache.PreloadDocumentImages(doc, base);
        },
        [&context, currentPath](const std::string& url) {
            return context.imageCache.GetImageSize(url, currentPath.parent_path());
        });
    
    ClampScrollOffset(window, context);
}

void AdjustBaseFontSize(GLFWwindow* window, LinuxHostContext context, float delta) {
    if (delta > 0) {
        if (context.controller.ZoomIn(delta)) {
            RelayoutCurrentDocument(window, context);
        }
    } else {
        if (context.controller.ZoomOut(-delta)) {
            RelayoutCurrentDocument(window, context);
        }
    }
}

std::optional<SkRect> GetScrollbarThumbRect(GLFWwindow* window, const LinuxHostContext context) {
    const auto& appState = GetAppState(context);
    const float viewportHeight = GetViewportHeight(window, context);
    
    if (appState.docLayout.totalHeight <= viewportHeight) {
        return std::nullopt;
    }

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    const float trackHeight = viewportHeight - kScrollbarMargin * 2.0f;
    const float thumbHeight = std::max(20.0f, (viewportHeight / appState.docLayout.totalHeight) * trackHeight);
    const float scrollRange = appState.docLayout.totalHeight - viewportHeight;
    const float thumbRange = trackHeight - thumbHeight;
    const float thumbY = GetContentTopInset() + kScrollbarMargin + (appState.scrollOffset / scrollRange) * thumbRange;

    return SkRect::MakeXYWH(
        static_cast<float>(width) - kScrollbarWidth - kScrollbarMargin,
        thumbY,
        kScrollbarWidth,
        thumbHeight);
}

} // namespace mdviewer::linux_platform
