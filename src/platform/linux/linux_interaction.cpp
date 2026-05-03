#include "platform/linux/linux_interaction.h"

#include "platform/linux/linux_viewer_host.h"
#include "platform/linux/linux_context_menu.h"
#include "platform/linux/linux_menu.h"
#include "platform/linux/linux_clipboard.h"
#include "platform/linux/linux_file_dialog.h"
#include "platform/linux/linux_font_dialog.h"
#include "platform/linux/linux_shell.h"
#include "view/document_context_menu.h"
#include "view/document_hit_test.h"
#include "view/document_interaction.h"
#include "util/skia_font_utils.h"
#include "render/typography.h"

#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"

#include <algorithm>
#include <cstdint>
#include <iostream>

namespace mdviewer::linux_platform {

namespace {

bool CopySelection(GLFWwindow* window, LinuxApp& app) {
    auto& appState = GetAppState(app.GetHostContext());
    if (!appState.HasSelection()) {
        return false;
    }

    const size_t selectionStart = GetSelectionStart(appState);
    const size_t selectionEnd = GetSelectionEnd(appState);
    if (selectionEnd > appState.docLayout.plainText.size() || selectionStart >= selectionEnd) {
        return false;
    }

    SetClipboardText(window, appState.docLayout.plainText.substr(selectionStart, selectionEnd - selectionStart));
    return true;
}

const std::vector<MenuBarItem>& GetLinuxMenuBarItems() {
    static const std::vector<MenuBarItem> items = {
        {"File", 0},
        {"View", 1},
        {"Theme", 2},
    };
    return items;
}

MenuBarLayout GetMenuBarLayout(GLFWwindow* window, LinuxApp& app) {
    int width = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);

    return ComputeMenuBarLayout(
        static_cast<float>(width),
        GetContentTopInset(),
        MeasureMenuBarItemWidths(GetLinuxMenuBarItems(), GetMenuTypeface(app.GetHostContext())));
}

SkRect GetDropdownRect(GLFWwindow* window, LinuxApp& app, int menuIndex) {
    const auto layout = GetMenuBarLayout(window, app);
    if (menuIndex < 0 || menuIndex >= static_cast<int>(layout.itemRects.size())) return SkRect::MakeEmpty();

    auto menus = GetLinuxMenus();
    if (menuIndex >= static_cast<int>(menus.size())) return SkRect::MakeEmpty();

    const float x = layout.itemRects[menuIndex].left();
    const float y = GetContentTopInset();
    
    auto* tf = GetMenuTypeface(app.GetHostContext());
    if (!tf) return SkRect::MakeEmpty();

    std::vector<DropdownItem> dropItems;
    dropItems.reserve(menus[menuIndex].items.size());
    for (const auto& item : menus[menuIndex].items) {
        dropItems.push_back({item.label, item.isSeparator});
    }

    return SkRect::MakeXYWH(
        x,
        y,
        MeasureDropdownWidth(dropItems, tf),
        static_cast<float>(menus[menuIndex].items.size()) * 30.0f);
}

int HitTestMenuBar(GLFWwindow* window, LinuxApp& app, double x, double y) {
    if (y > GetContentTopInset()) return -100; 

    return HitTestMenuBarLayout(GetMenuBarLayout(window, app), static_cast<float>(x), static_cast<float>(y));
}

int HitTestDropdown(GLFWwindow* window, LinuxApp& app, double x, double y) {
    auto& appState = GetAppState(app.GetHostContext());
    if (appState.menuBarState.activeIndex == -1) return -1;

    SkRect dr = GetDropdownRect(window, app, appState.menuBarState.activeIndex);
    if (dr.contains(static_cast<float>(x), static_cast<float>(y))) {
        return static_cast<int>((y - dr.fTop) / 30.0f);
    }
    return -1;
}

InteractionTextHit HitTest(GLFWwindow* window, LinuxApp& app, double x, double y) {
    const auto& appState = GetAppState(app.GetHostContext());
    const float contentTop = GetContentTopInset();
    
    if (y < contentTop) return {};

    HitTestCallbacks callbacks;
    callbacks.get_run_visual_width = [&](const BlockLayout& block, const LineLayout& line, const RunLayout& run) {
        if (run.text.empty()) return run.imageWidth;
        SkFont font;
        ConfigureDocumentFont(font, app.GetHostContext().typefaces.GetTypefaceSet(), block.type, run.style, appState.baseFontSize);
        return font.measureText(run.text.data(), run.text.size(), SkTextEncoding::kUTF8);
    };

    callbacks.find_text_position_in_run = [&](const BlockLayout& block, const LineLayout& line, const RunLayout& run, float x_in_run) {
        if (run.text.empty()) return run.textStart;
        SkFont font;
        ConfigureDocumentFont(font, app.GetHostContext().typefaces.GetTypefaceSet(), block.type, run.style, appState.baseFontSize);
        
        const char* text = run.text.data();
        size_t len = run.text.size();
        float currentX = 0;
        for (size_t i = 0; i < len; ++i) {
            float w = font.measureText(text + i, 1, SkTextEncoding::kUTF8);
            if (x_in_run < currentX + w * 0.5f) return run.textStart + i;
            currentX += w;
        }
        return run.textStart + len;
    };

    auto docHit = HitTestDocument(appState.docLayout, appState.scrollOffset, contentTop, static_cast<float>(x), static_cast<float>(y), callbacks);
    return InteractionTextHit{docHit.position, docHit.valid, docHit.url};
}

void ExecuteMenuCommand(GLFWwindow* window, LinuxApp& app, MenuCommand cmd) {
    auto host = app.GetHostContext();
    switch (cmd) {
        case MenuCommand::Exit: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
        case MenuCommand::OpenFile: {
            if (auto path = ShowOpenFileDialog()) {
                LoadFile(window, host, *path);
            }
        } break;
        case MenuCommand::SelectFont: {
            if (auto fontName = ShowFontDialog()) {
                ApplySelectedFont(window, host, *fontName);
            }
        } break;
        case MenuCommand::ThemeLight: ApplyTheme(window, host, ThemeMode::Light); break;
        case MenuCommand::ThemeSepia: ApplyTheme(window, host, ThemeMode::Sepia); break;
        case MenuCommand::ThemeDark: ApplyTheme(window, host, ThemeMode::Dark); break;
        case MenuCommand::UseDefaultFont:
            ApplySelectedFont(window, host, {});
            break;
        default: break;
    }
}

void OnMouseMove(GLFWwindow* window, double xpos, double ypos) {
    auto* app = static_cast<LinuxApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    auto& appState = GetAppState(app->GetHostContext());
    
    if (appState.menuBarState.activeIndex != -1) {
        int itemIdx = HitTestDropdown(window, *app, xpos, ypos);
        if (appState.menuBarState.hoveredItemIndex != itemIdx) {
            appState.menuBarState.hoveredItemIndex = itemIdx;
            appState.needsRepaint = true;
        }
        return;
    }

    int menuIdx = HitTestMenuBar(window, *app, xpos, ypos);
    if (menuIdx != -100) {
        if (appState.menuBarState.hoveredIndex != menuIdx) {
            appState.menuBarState.hoveredIndex = menuIdx;
            appState.needsRepaint = true;
        }
        return;
    } else if (appState.menuBarState.hoveredIndex != -1) {
        appState.menuBarState.hoveredIndex = -1;
        appState.needsRepaint = true;
    }

    const auto hit = HitTest(window, *app, xpos, ypos);
    if (appState.isSelecting) {
        UpdateSelectionFromHit(appState, hit, GetContentTopInset(), GetViewportHeight(window, app->GetHostContext()), GetMaxScroll(window, app->GetHostContext()));
        appState.needsRepaint = true;
    } else if (appState.isDraggingScrollbar) {
        const auto thumbRect = GetScrollbarThumbRect(window, app->GetHostContext());
        if (thumbRect) {
            UpdateScrollOffsetFromThumb(appState, static_cast<int>(ypos - GetContentTopInset()), *thumbRect, GetViewportHeight(window, app->GetHostContext()), GetMaxScroll(window, app->GetHostContext()), kScrollbarMargin);
            appState.needsRepaint = true;
        }
    } else {
        if (UpdateHoveredUrl(appState, hit)) {
            appState.needsRepaint = true;
        }
    }
}

void OnMouseButton(GLFWwindow* window, int button, int action, int mods) {
    auto* app = static_cast<LinuxApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    auto& appState = GetAppState(app->GetHostContext());
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        if (ypos < GetContentTopInset()) {
            return;
        }

        const auto hit = HitTest(window, *app, xpos, ypos);
        const auto menu = BuildDocumentContextMenu(appState, hit);
        const auto command = ShowDocumentContextMenu(menu);
        if (!command) {
            return;
        }

        switch (*command) {
            case DocumentContextCommand::CopySelection:
                if (CopySelection(window, *app)) {
                    appState.needsRepaint = true;
                }
                break;
            case DocumentContextCommand::OpenLink:
                HandleLinkClick(window, app->GetHostContext(), menu.linkUrl, false);
                break;
            case DocumentContextCommand::CopyLink:
                SetClipboardText(window, menu.linkUrl);
                break;
        }
        appState.needsRepaint = true;
        return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_PRESS) {
        if (appState.menuBarState.activeIndex != -1) {
            int itemIdx = HitTestDropdown(window, *app, xpos, ypos);
            if (itemIdx != -1) {
                auto menus = GetLinuxMenus();
                auto& menu = menus[appState.menuBarState.activeIndex];
                if (itemIdx < static_cast<int>(menu.items.size()) && !menu.items[itemIdx].isSeparator) {
                    ExecuteMenuCommand(window, *app, menu.items[itemIdx].command);
                }
            }
            appState.menuBarState.activeIndex = -1;
            appState.menuBarState.hoveredItemIndex = -1;
            appState.needsRepaint = true;
            return;
        }

        int menuIdx = HitTestMenuBar(window, *app, xpos, ypos);
        if (menuIdx != -100) {
            if (menuIdx == -2) GoBack(window, app->GetHostContext());
            else if (menuIdx == -3) GoForward(window, app->GetHostContext());
            else if (menuIdx == -4) AdjustBaseFontSize(window, app->GetHostContext(), -1.0f);
            else if (menuIdx == -5) AdjustBaseFontSize(window, app->GetHostContext(), 1.0f);
            else if (menuIdx >= 0) {
                appState.menuBarState.activeIndex = menuIdx;
                appState.menuBarState.hoveredItemIndex = -1;
                appState.needsRepaint = true;
            }
            return;
        }

        const auto hit = HitTest(window, *app, xpos, ypos);
        {
            auto& lockedState = GetAppState(app->GetHostContext());
            const float docX = static_cast<float>(xpos);
            const float docY = static_cast<float>(ypos) - GetContentTopInset() + lockedState.scrollOffset;
            for (const auto& buttonRegion : lockedState.codeBlockButtons) {
                if (!buttonRegion.first.contains(docX, docY)) {
                    continue;
                }

                const size_t start = buttonRegion.second.first;
                const size_t end = buttonRegion.second.second;
                if (end > start && end <= lockedState.docLayout.plainText.size()) {
                    SetClipboardText(window, lockedState.docLayout.plainText.substr(start, end - start));
                    lockedState.copiedFeedbackTimeout = static_cast<uint64_t>(glfwGetTime() * 1000.0) + 2000;
                    lockedState.needsRepaint = true;
                }
                return;
            }
        }
        const auto thumbRect = GetScrollbarThumbRect(window, app->GetHostContext());
        if (thumbRect && thumbRect->contains(static_cast<float>(xpos), static_cast<float>(ypos))) {
            BeginScrollbarDrag(appState, static_cast<float>(ypos) - thumbRect->fTop);
            appState.needsRepaint = true;
        } else {
            BeginSelection(appState, hit, (mods & GLFW_MOD_CONTROL), static_cast<int>(xpos), static_cast<int>(ypos));
            appState.needsRepaint = true;
        }
    } else if (action == GLFW_RELEASE) {
        const auto hit = HitTest(window, *app, xpos, ypos);
        auto result = FinishPrimaryPointerInteraction(appState, hit);
        if (result.activateLink) HandleLinkClick(window, app->GetHostContext(), result.linkUrl, result.forceExternal);
        FinalizeSelectionInteraction(appState);
        appState.needsRepaint = true;
    }
}
void OnScroll(GLFWwindow* window, double xoffset, double yoffset) {
    auto* app = static_cast<LinuxApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;
    ApplyWheelScroll(GetAppState(app->GetHostContext()), static_cast<float>(yoffset) * 40.0f, GetMaxScroll(window, app->GetHostContext()));
    GetAppState(app->GetHostContext()).needsRepaint = true;
}

void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* app = static_cast<LinuxApp*>(glfwGetWindowUserPointer(window));
    if (!app || (action != GLFW_PRESS && action != GLFW_REPEAT)) return;

    KeyEvent ev;
    ev.key = InteractionKey::Unknown;
    ev.ctrl = (mods & GLFW_MOD_CONTROL);
    ev.alt = (mods & GLFW_MOD_ALT);
    switch (key) {
        case GLFW_KEY_ESCAPE:
            ev.key = InteractionKey::Escape;
            if (GetAppState(app->GetHostContext()).menuBarState.activeIndex != -1) {
                GetAppState(app->GetHostContext()).menuBarState.activeIndex = -1;
                GetAppState(app->GetHostContext()).needsRepaint = true;
            }
            break;
        case GLFW_KEY_C: ev.key = InteractionKey::Copy; break;
        case GLFW_KEY_EQUAL: if (ev.ctrl) ev.key = InteractionKey::ZoomIn; break;
        case GLFW_KEY_KP_ADD: if (ev.ctrl) ev.key = InteractionKey::ZoomIn; break;
        case GLFW_KEY_MINUS: if (ev.ctrl) ev.key = InteractionKey::ZoomOut; break;
        case GLFW_KEY_KP_SUBTRACT: if (ev.ctrl) ev.key = InteractionKey::ZoomOut; break;
        case GLFW_KEY_LEFT: ev.key = InteractionKey::Left; break;
        case GLFW_KEY_RIGHT: ev.key = InteractionKey::Right; break;
        case GLFW_KEY_BACKSPACE: ev.key = InteractionKey::Back; break;
        case GLFW_KEY_O: if (ev.ctrl) ExecuteMenuCommand(window, *app, MenuCommand::OpenFile); break;
    }

    auto result = HandleKeyDown(GetAppState(app->GetHostContext()), ev);
    if (result.goBack) GoBack(window, app->GetHostContext());
    if (result.goForward) GoForward(window, app->GetHostContext());
    if (result.copySelection && CopySelection(window, *app)) {
        GetAppState(app->GetHostContext()).needsRepaint = true;
    }
    if (result.zoomIn) AdjustBaseFontSize(window, app->GetHostContext(), 1.0f);
    if (result.zoomOut) AdjustBaseFontSize(window, app->GetHostContext(), -1.0f);
    if (result.handled) GetAppState(app->GetHostContext()).needsRepaint = true;
}

void OnFramebufferSize(GLFWwindow* window, int width, int height) {
    auto* app = static_cast<LinuxApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;
    EnsureSurfaceSize(window, app->SurfaceContext());
    RelayoutCurrentDocument(window, app->GetHostContext());
}

void OnDrop(GLFWwindow* window, int count, const char** paths) {
    std::cerr << "File drop received: " << count << " files" << std::endl;
    auto* app = static_cast<LinuxApp*>(glfwGetWindowUserPointer(window));
    if (app && count > 0) {
        std::cerr << "Loading first dropped file: " << paths[0] << std::endl;
        LoadFile(window, app->GetHostContext(), paths[0]);
    }
}

} // namespace

void SetupCallbacks(GLFWwindow* window, LinuxApp* app) {
    glfwSetWindowUserPointer(window, app);
    glfwSetCursorPosCallback(window, OnMouseMove);
    glfwSetMouseButtonCallback(window, OnMouseButton);
    glfwSetScrollCallback(window, OnScroll);
    glfwSetKeyCallback(window, OnKey);
    glfwSetFramebufferSizeCallback(window, OnFramebufferSize);
    glfwSetDropCallback(window, OnDrop);
}

} // namespace mdviewer::linux_platform
