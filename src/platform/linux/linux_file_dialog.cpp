#include "platform/linux/linux_file_dialog.h"
#include <iostream>
#include <gtk/gtk.h>

namespace mdviewer::linux_platform {

std::optional<std::filesystem::path> ShowOpenFileDialog() {
    // We need to ensure GTK is initialized, but we don't want it to take over the main loop or arguments.
    if (!gtk_init_check(nullptr, nullptr)) {
        std::cerr << "Failed to initialize GTK for file dialog." << std::endl;
        return std::nullopt;
    }

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Open Markdown File",
        nullptr, // Parent window (could potentially map X11 handle from GLFW if needed)
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        nullptr);

    // Filter for Markdown files
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Markdown files");
    gtk_file_filter_add_pattern(filter, "*.md");
    gtk_file_filter_add_pattern(filter, "*.markdown");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    // Allow all files
    GtkFileFilter* allFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(allFilter, "All files");
    gtk_file_filter_add_pattern(allFilter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), allFilter);

    std::optional<std::filesystem::path> selectedPath;

    // Run the dialog and block until the user responds
    gint res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            selectedPath = std::filesystem::path(filename);
            g_free(filename);
        }
    }

    // Destroy the dialog widget
    gtk_widget_destroy(dialog);
    
    // Process pending GTK events to ensure the window closes immediately
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    return selectedPath;
}

} // namespace mdviewer::linux_platform
