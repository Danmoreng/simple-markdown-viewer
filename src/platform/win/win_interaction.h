#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "platform/win/win_viewer_host.h"

namespace mdviewer::win {

struct ViewerInteractionContext {
    ViewerHostContext& host;
    UINT_PTR autoScrollTimerId = 0;
    UINT_PTR copiedFeedbackTimerId = 0;
    UINT autoScrollTimerMs = 0;
    float autoScrollDeadZone = 0.0f;
    int linkClickSlop = 0;
};

bool HandleDropFiles(HWND hwnd, ViewerInteractionContext& context, HDROP drop);
bool HandlePrimaryButtonDown(HWND hwnd, ViewerInteractionContext& context, int x, int y);
bool HandlePointerMove(HWND hwnd, ViewerInteractionContext& context, WPARAM mouseButtons, int x, int y);
bool HandlePrimaryButtonUp(HWND hwnd, ViewerInteractionContext& context, int x, int y);
bool HandleContextMenu(HWND hwnd, ViewerInteractionContext& context, int screenX, int screenY);
bool HandleMiddleButtonDown(HWND hwnd, ViewerInteractionContext& context, int x, int y);
bool HandleXButtonDown(HWND hwnd, ViewerInteractionContext& context, WPARAM wParam);
bool HandleMouseWheel(HWND hwnd, ViewerInteractionContext& context, int delta);
bool HandleKeyDown(HWND hwnd, ViewerInteractionContext& context, WPARAM wParam);
bool HandleTimer(HWND hwnd, ViewerInteractionContext& context, WPARAM timerId);
bool HandleCaptureChanged(HWND hwnd, ViewerInteractionContext& context, LPARAM capturedWindow);
void StopAutoScroll(HWND hwnd, ViewerInteractionContext& context);

} // namespace mdviewer::win
