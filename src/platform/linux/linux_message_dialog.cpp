#include "platform/linux/linux_message_dialog.h"

#include <iostream>

#include <gtk/gtk.h>

namespace mdviewer::linux_platform {

void ShowErrorMessage(const std::string& title, const std::string& message) {
    if (!gtk_init_check(nullptr, nullptr)) {
        std::cerr << title << ": " << message << std::endl;
        return;
    }

    GtkWidget* dialog = gtk_message_dialog_new(
        nullptr,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s",
        message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

bool ConfirmWarning(const std::string& title, const std::string& message) {
    if (!gtk_init_check(nullptr, nullptr)) {
        std::cerr << title << ": " << message << std::endl;
        return false;
    }

    GtkWidget* dialog = gtk_message_dialog_new(
        nullptr,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE,
        "%s",
        message.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    gtk_dialog_add_buttons(
        GTK_DIALOG(dialog),
        "_Cancel",
        GTK_RESPONSE_CANCEL,
        "_Open",
        GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    return response == GTK_RESPONSE_ACCEPT;
}

} // namespace mdviewer::linux_platform
