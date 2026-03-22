#include "platform/linux/linux_font_dialog.h"
#include <iostream>
#include <gtk/gtk.h>

namespace mdviewer::linux_platform {

std::optional<std::string> ShowFontDialog() {
    if (!gtk_init_check(nullptr, nullptr)) {
        std::cerr << "Failed to initialize GTK for font dialog." << std::endl;
        return std::nullopt;
    }

    GtkWidget* dialog = gtk_font_chooser_dialog_new("Select Font", nullptr);

    std::optional<std::string> selectedFontFamily;

    gint res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        PangoFontDescription* fontDesc = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(dialog));
        if (fontDesc) {
            const char* family = pango_font_description_get_family(fontDesc);
            if (family) {
                selectedFontFamily = std::string(family);
            }
            pango_font_description_free(fontDesc);
        }
    }

    gtk_widget_destroy(dialog);
    
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    return selectedFontFamily;
}

} // namespace mdviewer::linux_platform
