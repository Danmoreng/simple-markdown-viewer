#include "platform/linux/linux_interaction.h"

#include "platform/linux/linux_viewer_host.h"
#include "platform/linux/linux_menu.h"
#include "platform/linux/linux_clipboard.h"
#include "platform/linux/linux_file_dialog.h"
#include "platform/linux/linux_font_dialog.h"
#include "platform/linux/linux_shell.h"
#include "view/document_hit_test.h"
#include "view/document_interaction.h"
#include "util/skia_font_utils.h"
#include "render/typography.h"

#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
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

struct MenuRect {
    SkRect rect;
    int index;
};

std::vector<MenuRect> GetMenuBarRects(GLFWwindow* window, LinuxApp& app) {
    const float height = GetContentTopInset();
    int width, w_height;
    glfwGetWindowSize(window, &width, &w_height);

    auto* tf = app.GetHostContext().typefaces.GetRegularTypeface();
    if (!tf) return {};
    SkFont font(sk_ref_sp(tf), 17.5f);
    
    std::vector<MenuRect> rects;
    float currentX = 12.0f; 
    const float itemGap = 8.0f;
    const float textPadding = 10.0f;

    auto items = { "File", "View", "Theme" };
    int idx = 0;
    for (const char* label : items) {
        SkRect bounds;
        font.measureText(label, strlen(label), SkTextEncoding::kUTF8, &bounds);
        float itemWidth = bounds.width() + textPadding * 2.0f;
        rects.push_back({SkRect::MakeXYWH(currentX, 0, itemWidth, height), idx++});
        currentX += itemWidth + itemGap;
    }

    const float btnSize = 34.0f;
    const float gap = 4.0f;
    float rightX = static_cast<float>(width) - 12.0f - btnSize;
    const float btnY = (height - btnSize) * 0.5f;

    rects.push_back({SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize), -3}); // Forward
    rightX -= (btnSize + gap);
    rects.push_back({SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize), -2}); // Back
    rightX -= (btnSize + gap);
    rects.push_back({SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize), -5}); // Zoom In
    rightX -= (btnSize + gap);
    rects.push_back({SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize), -4}); // Zoom Out

    return rects;
}

SkRect GetDropdownRect(GLFWwindow* window, LinuxApp& app, int menuIndex) {
    auto barRects = GetMenuBarRects(window, app);
    if (menuIndex < 0 || menuIndex >= static_cast<int>(barRects.size())) return SkRect::MakeEmpty();

    auto menus = GetLinuxMenus();
    if (menuIndex >= static_cast<int>(menus.size())) return SkRect::MakeEmpty();

    const float x = barRects[menuIndex].rect.fLeft;
    const float y = GetContentTopInset();
    
    auto* tf = app.GetHostContext().typefaces.GetRegularTypeface();
    if (!tf) return SkRect::MakeEmpty();

    SkFont font(sk_ref_sp(tf), 17.5f);
    float maxWidth = 150.0f;
    for (const auto& item : menus[menuIndex].items) {
        if (item.isSeparator) continue;
        SkRect bounds;
        font.measureText(item.label.c_str(), item.label.size(), SkTextEncoding::kUTF8, &bounds);
        maxWidth = std::max(maxWidth, bounds.width() + 40.0f);
    }

    return SkRect::MakeXYWH(x, y, maxWidth, menus[menuIndex].items.size() * 30.0f);
}

int HitTestMenuBar(GLFWwindow* window, LinuxApp& app, double x, double y) {
    if (y > GetContentTopInset()) return -100; 

    auto rects = GetMenuBarRects(window, app);
    for (const auto& mr : rects) {
        if (mr.rect.contains(static_cast<float>(x), static_cast<float>(y))) {
            return mr.index;
        }
    }
    return -1;
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
    if (!app || button != GLFW_MOUSE_BUTTON_LEFT) return;

    auto& appState = GetAppState(app->GetHostContext());
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

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
