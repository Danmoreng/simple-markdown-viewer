#include "platform/linux/linux_print.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>

#include <gtk/gtk.h>

#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include "platform/linux/linux_viewer_host.h"
#include "render/print_document.h"

namespace mdviewer::linux_platform {
namespace {

constexpr float kPrintRasterScale = 2.0f;

struct LinuxPrintState {
    LinuxHostContext context;
    PrintDocumentRequest request;
    PreparedPrintDocument prepared;
    std::filesystem::path baseDirectory;
    bool preparationFailed = false;
};

void BeginPrint(GtkPrintOperation* operation, GtkPrintContext* printContext, gpointer userData) {
    auto& state = *static_cast<LinuxPrintState*>(userData);
    state.request.pageWidth = static_cast<float>(gtk_print_context_get_width(printContext));
    state.request.pageHeight = static_cast<float>(gtk_print_context_get_height(printContext));
    state.request.imageSizeProvider = [&state](const std::string& url) {
        return state.context.imageCache.GetImageSize(url, state.baseDirectory);
    };

    if (!PreparePrintDocument(state.request, state.prepared)) {
        state.preparationFailed = true;
        gtk_print_operation_cancel(operation);
        return;
    }

    gtk_print_operation_set_n_pages(operation, static_cast<gint>(state.prepared.pages.size()));
}

void DrawPrintPage(
    GtkPrintOperation* operation,
    GtkPrintContext* printContext,
    gint pageNumber,
    gpointer userData) {
    (void)operation;
    auto& state = *static_cast<LinuxPrintState*>(userData);
    if (state.preparationFailed || pageNumber < 0 ||
        static_cast<size_t>(pageNumber) >= state.prepared.pages.size()) {
        return;
    }

    const int pixelWidth = std::max(
        1,
        static_cast<int>(std::ceil(state.prepared.pageWidth * kPrintRasterScale)));
    const int pixelHeight = std::max(
        1,
        static_cast<int>(std::ceil(state.prepared.pageHeight * kPrintRasterScale)));
    sk_sp<SkSurface> pageSurface = SkSurfaces::Raster(
        SkImageInfo::MakeN32Premul(pixelWidth, pixelHeight));
    if (!pageSurface) {
        state.preparationFailed = true;
        return;
    }

    SkCanvas* canvas = pageSurface->getCanvas();
    canvas->scale(kPrintRasterScale, kPrintRasterScale);
    RenderPrintDocumentPage(
        state.prepared,
        static_cast<size_t>(pageNumber),
        canvas,
        [&state](const std::string& url, float displayWidth, float displayHeight) {
            return state.context.imageCache.GetImage(
                url,
                state.baseDirectory,
                displayWidth,
                displayHeight);
        });

    SkPixmap pixmap;
    if (!pageSurface->peekPixels(&pixmap)) {
        state.preparationFailed = true;
        return;
    }

    cairo_surface_t* cairoSurface = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(const_cast<void*>(pixmap.addr())),
        CAIRO_FORMAT_ARGB32,
        pixelWidth,
        pixelHeight,
        static_cast<int>(pixmap.rowBytes()));
    if (cairo_surface_status(cairoSurface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(cairoSurface);
        state.preparationFailed = true;
        return;
    }

    cairo_t* cairo = gtk_print_context_get_cairo_context(printContext);
    cairo_save(cairo);
    cairo_scale(cairo, 1.0 / kPrintRasterScale, 1.0 / kPrintRasterScale);
    cairo_set_source_surface(cairo, cairoSurface, 0.0, 0.0);
    cairo_paint(cairo);
    cairo_restore(cairo);
    cairo_surface_destroy(cairoSurface);
}

void DrainGtkEvents() {
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

} // namespace

bool PrintCurrentDocument(LinuxHostContext context) {
    if (!gtk_init_check(nullptr, nullptr)) {
        std::cerr << "Failed to initialize GTK for printing." << std::endl;
        return false;
    }
    if (!EnsureFontSystem(context)) {
        std::cerr << "Font initialization failed. The document cannot be printed." << std::endl;
        return false;
    }

    LinuxPrintState state{context};
    state.request.typefaces = context.typefaces.GetTypefaceSet();
    state.request.layoutTypeface = GetRegularTypeface(context);

    AppState& appState = GetAppState(context);
    {
        std::lock_guard<std::mutex> lock(appState.mtx);
        state.request.sourcePath = appState.currentFilePath;
        state.request.sourceText = appState.sourceText;
        state.request.document = appState.docModel;
        state.request.theme = appState.theme;
        state.request.baseFontSize = appState.baseFontSize;
    }
    if (state.request.sourcePath.empty()) {
        std::cerr << "Open a Markdown file before printing." << std::endl;
        return false;
    }
    state.baseDirectory = state.request.sourcePath.parent_path();

    GtkPrintOperation* operation = gtk_print_operation_new();
    gtk_print_operation_set_unit(operation, GTK_UNIT_POINTS);
    gtk_print_operation_set_use_full_page(operation, FALSE);
    const std::string jobName = state.request.sourcePath.filename().empty()
        ? "Markdown document"
        : state.request.sourcePath.filename().string();
    gtk_print_operation_set_job_name(operation, jobName.c_str());
    g_signal_connect(operation, "begin-print", G_CALLBACK(BeginPrint), &state);
    g_signal_connect(operation, "draw-page", G_CALLBACK(DrawPrintPage), &state);

    GError* error = nullptr;
    const GtkPrintOperationResult result = gtk_print_operation_run(
        operation,
        GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
        nullptr,
        &error);

    bool success = result == GTK_PRINT_OPERATION_RESULT_APPLY && !state.preparationFailed;
    if (error) {
        std::cerr << "Print job failed: " << error->message << std::endl;
        g_error_free(error);
        success = false;
    } else if (state.preparationFailed) {
        std::cerr << "The document could not be prepared or rendered for printing." << std::endl;
    }

    g_object_unref(operation);
    DrainGtkEvents();
    return success;
}

} // namespace mdviewer::linux_platform
