#include "platform/linux/linux_context_menu.h"

#include <gtk/gtk.h>

namespace mdviewer::linux_platform {
namespace {

struct PopupState {
    std::optional<DocumentContextCommand> command;
    bool closed = false;
};

void QuitPopupLoop(PopupState* state) {
    if (!state->closed) {
        state->closed = true;
        gtk_main_quit();
    }
}

void OnMenuItemActivate(GtkMenuItem* item, gpointer userData) {
    auto* state = static_cast<PopupState*>(userData);
    const auto commandValue = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "mdviewer-command"));
    state->command = static_cast<DocumentContextCommand>(commandValue);
    QuitPopupLoop(state);
}

void OnMenuDeactivate(GtkMenuShell* shell, gpointer userData) {
    (void)shell;
    QuitPopupLoop(static_cast<PopupState*>(userData));
}

GdkEvent* CreatePopupTriggerEvent() {
    GdkDisplay* display = gdk_display_get_default();
    GdkSeat* seat = display ? gdk_display_get_default_seat(display) : nullptr;
    GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
    if (!pointer) {
        return nullptr;
    }

    GdkScreen* screen = nullptr;
    gint rootX = 0;
    gint rootY = 0;
    gdk_device_get_position(pointer, &screen, &rootX, &rootY);
    GdkWindow* rootWindow = screen ? gdk_screen_get_root_window(screen) : nullptr;
    if (!rootWindow) {
        return nullptr;
    }

    GdkEvent* event = gdk_event_new(GDK_BUTTON_PRESS);
    event->button.window = GDK_WINDOW(g_object_ref(rootWindow));
    event->button.send_event = TRUE;
    event->button.time = GDK_CURRENT_TIME;
    event->button.x = rootX;
    event->button.y = rootY;
    event->button.x_root = rootX;
    event->button.y_root = rootY;
    event->button.button = 3;
    gdk_event_set_device(event, pointer);
    return event;
}

} // namespace

std::optional<DocumentContextCommand> ShowDocumentContextMenu(const DocumentContextMenu& menu) {
    if (menu.items.empty()) {
        return std::nullopt;
    }

    if (!gtk_init_check(nullptr, nullptr)) {
        return std::nullopt;
    }

    PopupState state;
    GtkWidget* popup = gtk_menu_new();

    for (const auto& item : menu.items) {
        GtkWidget* menuItem = item.isSeparator
            ? gtk_separator_menu_item_new()
            : gtk_menu_item_new_with_label(item.label.c_str());
        if (item.isSeparator) {
            gtk_menu_shell_append(GTK_MENU_SHELL(popup), menuItem);
            continue;
        }
        gtk_widget_set_sensitive(menuItem, item.enabled ? TRUE : FALSE);
        g_object_set_data(
            G_OBJECT(menuItem),
            "mdviewer-command",
            GINT_TO_POINTER(static_cast<int>(item.command)));
        g_signal_connect(menuItem, "activate", G_CALLBACK(OnMenuItemActivate), &state);
        gtk_menu_shell_append(GTK_MENU_SHELL(popup), menuItem);
    }

    g_signal_connect(popup, "deactivate", G_CALLBACK(OnMenuDeactivate), &state);

    gtk_widget_show_all(popup);
    GdkEvent* triggerEvent = CreatePopupTriggerEvent();
    if (!triggerEvent) {
        gtk_widget_destroy(popup);
        return std::nullopt;
    }

    gtk_menu_popup_at_pointer(GTK_MENU(popup), triggerEvent);
    gdk_event_free(triggerEvent);
    if (!state.closed) {
        gtk_main();
    }

    gtk_widget_destroy(popup);
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    return state.command;
}

} // namespace mdviewer::linux_platform
