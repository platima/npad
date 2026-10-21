/*
 * npad - Page Setup, Print and Print Preview (GTK 3)
 *
 * Author: Platima
 * https://npad.dev
 */

#include "print_gtk.h"

#include <langinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/settings.h"
#include "../ui_interface.h"

// notepad.exe's defaults, in thousandths of an inch from the sheet edge. The
// same unit and keys as the Windows build, so a settings file moves between them.
#define MARGIN_DEFAULT_SIDE 750
#define MARGIN_DEFAULT_TOPBOTTOM 1000

#define HEADER_DEFAULT "&f"
#define FOOTER_DEFAULT "Page &p"

// A paragraph longer than this is laid out in pieces, so one multi-megabyte
// line does not make Pango wrap the whole thing in a single pass. All but the
// last line of a piece are final; the next piece starts at that last line.
#define PARAGRAPH_CHUNK 65536

// DMPAPER_* codes (the Windows build's print_paper_size values) and the PWG
// names GTK knows them by. Anything else is stored as 256 + a size in tenths
// of a millimetre, which is how Windows stores a custom sheet.
#define PAPER_CUSTOM 256

static const struct {
    int code;
    const char *name;
} g_paper_codes[] = {
    { 1, "na_letter" },     { 3, "na_ledger" }, { 5, "na_legal" }, { 7, "na_executive" },
    { 8, "iso_a3" },        { 9, "iso_a4" },    { 11, "iso_a5" },  { 13, "jis_b5" },
    { 20, "na_number-10" }, { 27, "iso_dl" },   { 28, "iso_c5" },
};

// Nearest integer, without pulling in libm for one function
static int round_int(double v) {
    return (int) (v < 0 ? v - 0.5 : v + 0.5);
}

// ---------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------

// The printer, copies, output file and so on survive between jobs, like the
// Windows print dialog remembering its last printer. Paper, orientation and
// margins are not kept here: they live in settings.json, shared with Page Setup.
static GtkPrintSettings *g_print_settings = NULL;

// ---------------------------------------------------------------------------
// Page setup <-> settings
// ---------------------------------------------------------------------------

static void margins_load(int margins[4]) {
    margins[0] = settings_get_int("print_margin_left", MARGIN_DEFAULT_SIDE);
    margins[1] = settings_get_int("print_margin_right", MARGIN_DEFAULT_SIDE);
    margins[2] = settings_get_int("print_margin_top", MARGIN_DEFAULT_TOPBOTTOM);
    margins[3] = settings_get_int("print_margin_bottom", MARGIN_DEFAULT_TOPBOTTOM);
    for (int i = 0; i < 4; i++) {
        if (margins[i] < 0)
            margins[i] = 0;
    }
}

// Paper and orientation as Page Setup saved them. 0 for either means "leave
// the default": the locale's paper (Letter or A4), portrait.
static GtkPageSetup *page_setup_load(void) {
    GtkPageSetup *setup = gtk_page_setup_new();

    int code = settings_get_int("print_paper_size", 0);
    GtkPaperSize *paper = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(g_paper_codes) && code != 0; i++) {
        if (g_paper_codes[i].code == code)
            paper = gtk_paper_size_new(g_paper_codes[i].name);
    }
    if (!paper && code == PAPER_CUSTOM) {
        int w = settings_get_int("print_paper_width", 0);
        int l = settings_get_int("print_paper_length", 0);
        if (w > 0 && l > 0)
            paper =
                gtk_paper_size_new_custom("npad-custom", "Custom", w / 10.0, l / 10.0, GTK_UNIT_MM);
    }
    if (paper) {
        gtk_page_setup_set_paper_size(setup, paper);
        gtk_paper_size_free(paper);
    }

    gtk_page_setup_set_orientation(setup, settings_get_int("print_orientation", 0) == 2
                                              ? GTK_PAGE_ORIENTATION_LANDSCAPE
                                              : GTK_PAGE_ORIENTATION_PORTRAIT);
    return setup;
}

static void page_setup_save(GtkPageSetup *setup, const int margins[4]) {
    GtkPaperSize *paper = gtk_page_setup_get_paper_size(setup);
    const char *name = gtk_paper_size_get_name(paper);
    int code = PAPER_CUSTOM;
    for (size_t i = 0; i < G_N_ELEMENTS(g_paper_codes); i++) {
        if (strcmp(g_paper_codes[i].name, name) == 0)
            code = g_paper_codes[i].code;
    }
    settings_set_int("print_paper_size", code);
    settings_set_int(
        "print_paper_width",
        code == PAPER_CUSTOM ? round_int(gtk_paper_size_get_width(paper, GTK_UNIT_MM) * 10) : 0);
    settings_set_int(
        "print_paper_length",
        code == PAPER_CUSTOM ? round_int(gtk_paper_size_get_height(paper, GTK_UNIT_MM) * 10) : 0);

    GtkPageOrientation o = gtk_page_setup_get_orientation(setup);
    bool landscape =
        (o == GTK_PAGE_ORIENTATION_LANDSCAPE || o == GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE);
    settings_set_int("print_orientation", landscape ? 2 : 1);

    settings_set_int("print_margin_left", margins[0]);
    settings_set_int("print_margin_right", margins[1]);
    settings_set_int("print_margin_top", margins[2]);
    settings_set_int("print_margin_bottom", margins[3]);
    settings_save();
    ui_notify_settings_changed(); // Other windows print with the same page setup
}

// ---------------------------------------------------------------------------
// Page Setup dialog
// ---------------------------------------------------------------------------

// GTK's own page setup dialog has paper and orientation but no margins, which
// Notepad's has. So this dialog owns the margins and hands the paper and
// orientation to GTK's through a button.
typedef struct {
    GtkWidget *dialog;
    GtkWidget *summary;
    GtkPageSetup *setup;
} PageSetupDialog;

static bool locale_is_imperial(void) {
    // GTK picks Letter or A4 from the locale's measurement system
    return g_str_has_prefix(gtk_paper_size_get_default(), "na_");
}

static void page_setup_dialog_refresh(PageSetupDialog *psd) {
    GtkPaperSize *paper = gtk_page_setup_get_paper_size(psd->setup);
    GtkPageOrientation o = gtk_page_setup_get_orientation(psd->setup);
    bool landscape =
        (o == GTK_PAGE_ORIENTATION_LANDSCAPE || o == GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE);
    char *text = g_strdup_printf("%s, %s", gtk_paper_size_get_display_name(paper),
                                 landscape ? "Landscape" : "Portrait");
    gtk_label_set_text(GTK_LABEL(psd->summary), text);
    g_free(text);
}

static void on_paper_button_clicked(GtkButton *button, gpointer data) {
    (void) button;
    PageSetupDialog *psd = data;
    GtkPageSetup *chosen =
        gtk_print_run_page_setup_dialog(GTK_WINDOW(psd->dialog), psd->setup, g_print_settings);
    if (chosen) {
        g_object_unref(psd->setup);
        psd->setup = chosen;
        page_setup_dialog_refresh(psd);
    }
}

void npad_gtk_print_page_setup(GtkWindow *parent) {
    PageSetupDialog psd = { 0 };
    psd.setup = page_setup_load();

    psd.dialog = gtk_dialog_new_with_buttons(
        "Page Setup", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
        GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(psd.dialog), GTK_RESPONSE_OK);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(psd.dialog))), grid, TRUE,
                       TRUE, 0);

    GtkWidget *label = gtk_label_new("Paper:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    psd.summary = gtk_label_new(NULL);
    gtk_widget_set_halign(psd.summary, GTK_ALIGN_START);
    gtk_widget_set_hexpand(psd.summary, TRUE);
    gtk_grid_attach(GTK_GRID(grid), psd.summary, 1, 0, 1, 1);
    GtkWidget *paper_button = gtk_button_new_with_mnemonic("Paper and _Orientation...");
    g_signal_connect(paper_button, "clicked", G_CALLBACK(on_paper_button_clicked), &psd);
    gtk_grid_attach(GTK_GRID(grid), paper_button, 2, 0, 1, 1);
    page_setup_dialog_refresh(&psd);

    // Margins are held in thousandths of an inch; the dialog shows the
    // locale's unit. A field is written back only if the user moved it, so
    // opening and accepting the dialog in millimetres does not nudge 0.75 in
    // to 19.1 mm (752 thousandths).
    bool imperial = locale_is_imperial();
    int margins[4];
    margins_load(margins);
    static const char *const names[4] = { "_Left:", "_Right:", "_Top:", "_Bottom:" };
    GtkWidget *spins[4];
    double initial[4];
    for (int i = 0; i < 4; i++) {
        initial[i] = imperial ? margins[i] / 1000.0 : margins[i] * 25.4 / 1000.0;
        spins[i] = imperial ? gtk_spin_button_new_with_range(0, 20, 0.05)
                            : gtk_spin_button_new_with_range(0, 500, 1);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spins[i]), imperial ? 2 : 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spins[i]), initial[i]);
        initial[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spins[i])); // As the spin rounded it
        gtk_entry_set_activates_default(GTK_ENTRY(spins[i]), TRUE);

        GtkWidget *text = gtk_label_new_with_mnemonic(names[i]);
        gtk_label_set_mnemonic_widget(GTK_LABEL(text), spins[i]);
        gtk_widget_set_halign(text, GTK_ALIGN_START);
        // Left/Right on one row, Top/Bottom on the next
        GtkWidget *pair = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(pair), text, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(pair), spins[i], FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(grid), pair, (i % 2) ? 2 : 0, 1 + i / 2, (i % 2) ? 1 : 2, 1);
    }
    GtkWidget *unit =
        gtk_label_new(imperial ? "Margins are in inches." : "Margins are in millimetres.");
    gtk_widget_set_halign(unit, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), unit, 0, 3, 3, 1);

    gtk_widget_show_all(psd.dialog);
    if (gtk_dialog_run(GTK_DIALOG(psd.dialog)) == GTK_RESPONSE_OK) {
        for (int i = 0; i < 4; i++) {
            // Commit what was typed but not yet parsed (OK can be clicked mid-edit)
            gtk_spin_button_update(GTK_SPIN_BUTTON(spins[i]));
            double v = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spins[i]));
            if (v != initial[i])
                margins[i] = round_int(imperial ? v * 1000.0 : v / 25.4 * 1000.0);
        }
        page_setup_save(psd.setup, margins);
    }
    gtk_widget_destroy(psd.dialog);
    g_object_unref(psd.setup);
}

// ---------------------------------------------------------------------------
// The paginated job
// ---------------------------------------------------------------------------

// One drawn line: a wrapped line of a paragraph. y is the top of the line,
// in print-context units, from the top of the sheet.
typedef struct {
    int page; // 1-based
    double y;
    gsize offset; // Into PrintJob::text
    gsize len;
} PrintRun;

typedef struct {
    char *text;
    char *title;
    char *header; // Header/footer specs, with the Notepad codes unexpanded
    char *footer;
    char *date_text; // Expanded once per job so every page agrees
    char *time_text;
    PangoContext *pango; // Fixed-resolution, unhinted: text measures the same everywhere
    PangoFontDescription *font;
    char *font_family; // What font is built from, so a preview can start the job again
    double font_size;
    int font_weight;
    bool font_italic;

    // Geometry in points, whatever resolution the output device has
    double body_left;
    double body_right;
    double body_top;
    double body_bottom;
    double line_h;
    double text_top;
    int lines_per_page;

    GArray *runs;       // PrintRun, in page order
    GArray *page_first; // guint: index of each page's first run, plus a final sentinel
    int page_count;
    bool began; // begin-print ran: a printer was found and the job started
} PrintJob;

static void job_free(gpointer data) {
    PrintJob *job = data;
    if (!job)
        return;
    g_free(job->text);
    g_free(job->title);
    free(job->header);
    free(job->footer);
    g_free(job->date_text);
    g_free(job->time_text);
    g_free(job->font_family);
    if (job->font)
        pango_font_description_free(job->font);
    if (job->pango)
        g_object_unref(job->pango);
    if (job->runs)
        g_array_free(job->runs, TRUE);
    if (job->page_first)
        g_array_free(job->page_first, TRUE);
    g_free(job);
}

static PrintJob *job_new(const NpadPrintDoc *doc) {
    PrintJob *job = g_new0(PrintJob, 1);
    job->text = g_strdup(doc->text ? doc->text : "");
    job->title = g_strdup(doc->title && doc->title[0] ? doc->title : "Untitled");
    job->header = settings_get_string("print_header", HEADER_DEFAULT);
    job->footer = settings_get_string("print_footer", FOOTER_DEFAULT);

    job->font_family =
        g_strdup(doc->font_family && doc->font_family[0] ? doc->font_family : "Monospace");
    job->font_size = doc->font_size;
    job->font_weight = doc->font_weight;
    job->font_italic = doc->font_italic;
    job->font = pango_font_description_new();
    pango_font_description_set_family(job->font, job->font_family);
    pango_font_description_set_size(job->font, round_int(job->font_size * PANGO_SCALE));
    pango_font_description_set_weight(job->font, (PangoWeight) job->font_weight);
    pango_font_description_set_style(job->font,
                                     job->font_italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);

    // Measure and draw in points on a context of our own rather than the print
    // context's. At 72 dpi GTK hints glyph advances to whole points, so a
    // 14 pt face would measure (and print) at a different width than on a
    // preview at another resolution; with metric hinting off a glyph is the
    // same width on every device and the preview cannot disagree with the page.
    job->pango = pango_font_map_create_context(pango_cairo_font_map_get_default());
    pango_cairo_context_set_resolution(job->pango, 72);
    cairo_font_options_t *options = cairo_font_options_create();
    cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
    pango_cairo_context_set_font_options(job->pango, options);
    cairo_font_options_destroy(options);
#if PANGO_VERSION_CHECK(1, 44, 0)
    // Pango 1.44+ also rounds each glyph position to a whole unit by default
    pango_context_set_round_glyph_positions(job->pango, FALSE);
#endif

    job->runs = g_array_new(FALSE, FALSE, sizeof(PrintRun));
    job->page_first = g_array_new(FALSE, FALSE, sizeof(guint));
    return job;
}

// The date and time for &d and &t: the locale's short date, and its time
// without seconds, as the Windows build's DATE_SHORTDATE / TIME_NOSECONDS.
static void job_stamp(PrintJob *job) {
    GDateTime *now = g_date_time_new_now_local();
    g_free(job->date_text);
    job->date_text = g_date_time_format(now, "%x");

    // The locale's own time format, minus its seconds field
    char *fmt = g_strdup(nl_langinfo(T_FMT));
    if (!fmt || !fmt[0]) {
        g_free(fmt);
        fmt = g_strdup("%H:%M");
    }
    GString *trimmed = g_string_new(NULL);
    for (const char *p = fmt; *p; p++) {
        if (strncmp(p, ":%S", 3) == 0 || strncmp(p, ".%S", 3) == 0) {
            p += 2;
        } else if (strncmp(p, "%T", 2) == 0) {
            g_string_append(trimmed, "%R");
            p++;
        } else if (strncmp(p, "%r", 2) == 0) {
            g_string_append(trimmed, "%I:%M %p");
            p++;
        } else {
            g_string_append_c(trimmed, *p);
        }
    }
    g_free(job->time_text);
    job->time_text = g_date_time_format(now, trimmed->str);
    if (!job->time_text)
        job->time_text = g_date_time_format(now, "%H:%M");
    g_string_free(trimmed, TRUE);
    g_free(fmt);
    g_date_time_unref(now);
}

// ---------------------------------------------------------------------------
// Header and footer codes
// ---------------------------------------------------------------------------

// Expand the value codes of one section: &f file, &p page, &d date, &t time,
// && for a literal ampersand. Anything else after an & is dropped, as
// notepad.exe does.
static char *hf_expand(const char *src, const PrintJob *job, int page) {
    GString *out = g_string_new(NULL);
    for (const char *p = src; *p; p++) {
        if (*p != '&') {
            g_string_append_c(out, *p);
            continue;
        }
        p++;
        if (!*p)
            break;
        switch (*p) {
            case 'f':
            case 'F':
                g_string_append(out, job->title);
                break;
            case 'p':
            case 'P':
                g_string_append_printf(out, "%d", page);
                break;
            case 'd':
            case 'D':
                g_string_append(out, job->date_text ? job->date_text : "");
                break;
            case 't':
            case 'T':
                g_string_append(out, job->time_text ? job->time_text : "");
                break;
            case '&':
                g_string_append_c(out, '&');
                break;
            default:
                break;
        }
    }
    return g_string_free(out, FALSE);
}

// Split a header/footer spec on &l / &c / &r into its left, centre and right
// parts. Text before any marker is left-aligned. Other &x pairs are kept as
// they are for hf_expand.
static void hf_split(const char *spec, GString *sections[3]) {
    int current = 0;
    for (const char *p = spec; *p; p++) {
        if (*p == '&' && p[1]) {
            char c = p[1];
            if (c == 'l' || c == 'L') {
                current = 0;
            } else if (c == 'c' || c == 'C') {
                current = 1;
            } else if (c == 'r' || c == 'R') {
                current = 2;
            } else {
                g_string_append_c(sections[current], '&');
                g_string_append_c(sections[current], c);
            }
            p++;
            continue;
        }
        g_string_append_c(sections[current], *p);
    }
}

// Draw one header or footer line with its top at y
static void hf_draw(cairo_t *cr, PangoLayout *layout, const PrintJob *job, const char *spec,
                    int page, double y) {
    if (!spec || !spec[0])
        return;
    GString *sections[3] = { g_string_new(NULL), g_string_new(NULL), g_string_new(NULL) };
    hf_split(spec, sections);
    for (int i = 0; i < 3; i++) {
        if (sections[i]->len > 0) {
            char *text = hf_expand(sections[i]->str, job, page);
            pango_layout_set_text(layout, text, -1);
            int w = 0, h = 0;
            pango_layout_get_size(layout, &w, &h);
            double width = (double) w / PANGO_SCALE;
            double x = job->body_left;
            if (i == 1)
                x = (job->body_left + job->body_right - width) / 2;
            else if (i == 2)
                x = job->body_right - width;
            cairo_move_to(cr, x, y);
            pango_cairo_show_layout(cr, layout);
            g_free(text);
        }
        g_string_free(sections[i], TRUE);
    }
}

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

static PangoLayout *job_layout(const PrintJob *job) {
    PangoLayout *layout = pango_layout_new(job->pango);
    pango_layout_set_font_description(layout, job->font);
    return layout;
}

typedef struct {
    int page;
    int line_on_page;
} Cursor;

static void cursor_next_line(const PrintJob *job, Cursor *c) {
    c->line_on_page++;
    if (c->line_on_page >= job->lines_per_page) {
        c->page++;
        c->line_on_page = 0;
    }
}

static void job_add_run(PrintJob *job, const Cursor *c, gsize offset, gsize len) {
    PrintRun run = { c->page, job->text_top + c->line_on_page * job->line_h, offset, len };
    g_array_append_val(job->runs, run);
}

// Lay out one paragraph [start, end) of the text, adding a run per wrapped line
static void job_paginate_paragraph(PrintJob *job, PangoLayout *layout, gsize start, gsize end,
                                   Cursor *c) {
    if (start == end) { // A blank line
        cursor_next_line(job, c);
        return;
    }
    while (start < end) {
        gsize chunk_end = end;
        if (end - start > PARAGRAPH_CHUNK) {
            chunk_end = start + PARAGRAPH_CHUNK;
            while (chunk_end > start && ((guchar) job->text[chunk_end] & 0xC0) == 0x80)
                chunk_end--; // Never split a character
        }
        pango_layout_set_text(layout, job->text + start, (int) (chunk_end - start));

        int lines = pango_layout_get_line_count(layout);
        // In a partial piece the last line may continue into the next one
        int emit = (chunk_end < end && lines > 1) ? lines - 1 : lines;
        gsize next = chunk_end;
        for (int i = 0; i < lines; i++) {
            PangoLayoutLine *line = pango_layout_get_line_readonly(layout, i);
            if (i < emit) {
                job_add_run(job, c, start + (gsize) line->start_index, (gsize) line->length);
                cursor_next_line(job, c);
            } else {
                next = start + (gsize) line->start_index;
            }
        }
        start = next;
    }
}

// Compute the whole layout on the print context: the font, the page area, then
// every wrapped line with its page. The preview and the printout both replay
// these stored runs, so they cannot disagree about where a line breaks.
static void job_paginate(PrintJob *job, GtkPrintContext *context) {
    // The context's units are device pixels; everything below is in points
    double width =
        gtk_print_context_get_width(context) * 72.0 / gtk_print_context_get_dpi_x(context);
    double height =
        gtk_print_context_get_height(context) * 72.0 / gtk_print_context_get_dpi_y(context);

    // Margins run from the sheet edge, like Notepad's. The operation is set to
    // use the full page, so the context's origin is the sheet's corner and no
    // hardware margin sneaks into what the user chose.
    int margins[4];
    margins_load(margins);
    job->body_left = margins[0] / 1000.0 * 72.0;
    job->body_right = width - margins[1] / 1000.0 * 72.0;
    job->body_top = margins[2] / 1000.0 * 72.0;
    job->body_bottom = height - margins[3] / 1000.0 * 72.0;
    if (job->body_right - job->body_left < 1)
        job->body_right = job->body_left + 1;
    if (job->body_bottom - job->body_top < 1)
        job->body_bottom = job->body_top + 1;

    PangoLayout *layout = job_layout(job);
    pango_layout_set_text(layout, "Ag", -1);
    int line_units = 0;
    pango_layout_line_get_height(pango_layout_get_line_readonly(layout, 0), &line_units);
    job->line_h = (double) line_units / PANGO_SCALE;
    if (job->line_h < 1)
        job->line_h = 1; // Everything below divides by this

    // One line of header and one of footer, each with a blank line of padding
    double band = job->line_h * 2;
    double text_bottom = job->body_bottom;
    job->text_top = job->body_top;
    if (job->header && job->header[0])
        job->text_top += band;
    if (job->footer && job->footer[0])
        text_bottom -= band;
    job->lines_per_page = (int) ((text_bottom - job->text_top) / job->line_h);
    if (job->lines_per_page < 1)
        job->lines_per_page = 1;

    // Wrap at the body width, at spaces rather than mid-word where there is one
    pango_layout_set_width(layout, (int) ((job->body_right - job->body_left) * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);

    job_stamp(job);
    g_array_set_size(job->runs, 0);

    Cursor c = { 1, 0 };
    gsize total = strlen(job->text);
    gsize pos = 0;
    while (pos < total) {
        const char *nl = memchr(job->text + pos, '\n', total - pos);
        gsize eol = nl ? (gsize) (nl - job->text) : total;
        job_paginate_paragraph(job, layout, pos, eol, &c);
        pos = eol + 1; // A trailing newline leaves no extra empty line
    }
    g_object_unref(layout);

    // Pages past the last drawn line are blank, so they are not pages
    job->page_count = 1;
    for (guint i = 0; i < job->runs->len; i++) {
        int page = g_array_index(job->runs, PrintRun, i).page;
        if (page > job->page_count)
            job->page_count = page;
    }
    g_array_set_size(job->page_first, 0);
    guint r = 0;
    for (int page = 1; page <= job->page_count; page++) {
        g_array_append_val(job->page_first, r);
        while (r < job->runs->len && g_array_index(job->runs, PrintRun, r).page == page)
            r++;
    }
    g_array_append_val(job->page_first, r);
}

// ---------------------------------------------------------------------------
// GtkPrintOperation callbacks
// ---------------------------------------------------------------------------

static void on_begin_print(GtkPrintOperation *op, GtkPrintContext *context, gpointer data) {
    PrintJob *job = data;
    job->began = true;
    job_paginate(job, context);
    gtk_print_operation_set_n_pages(op, job->page_count);
}

static void on_draw_page(GtkPrintOperation *op, GtkPrintContext *context, gint page_nr,
                         gpointer data) {
    (void) op;
    PrintJob *job = data;
    if (page_nr < 0 || page_nr >= job->page_count)
        return;
    int page = page_nr + 1;

    cairo_t *cr = gtk_print_context_get_cairo_context(context);
    cairo_save(cr);
    // The context's units are device pixels; the job's are points
    cairo_scale(cr, 72.0 / gtk_print_context_get_dpi_x(context),
                72.0 / gtk_print_context_get_dpi_y(context));
    cairo_set_source_rgb(cr, 0, 0,
                         0); // Paper is white and ink is black, whatever the colour scheme

    PangoLayout *layout = job_layout(job);

    if (job->header && job->header[0])
        hf_draw(cr, layout, job, job->header, page, job->body_top);
    if (job->footer && job->footer[0])
        hf_draw(cr, layout, job, job->footer, page, job->body_bottom - job->line_h);

    guint first = g_array_index(job->page_first, guint, page_nr);
    guint last = g_array_index(job->page_first, guint, page_nr + 1);
    for (guint i = first; i < last; i++) {
        const PrintRun *run = &g_array_index(job->runs, PrintRun, i);
        pango_layout_set_text(layout, job->text + run->offset, (int) run->len);
        cairo_move_to(cr, job->body_left, run->y);
        pango_cairo_show_layout(cr, layout);
    }
    g_object_unref(layout);
    cairo_restore(cr);
}

// ---------------------------------------------------------------------------
// Print Preview
//
// GTK's own preview hands a PDF to whatever viewer the desktop has, which may
// be none (and cannot offer Print or Page Setup). So the preview is drawn here
// instead: the "preview" signal hands over the operation's pages to render onto
// any cairo context, and the job's stored runs are replayed onto a drawing
// area exactly as they go to the printer.
// ---------------------------------------------------------------------------

#define PREVIEW_ZOOM_MIN 25
#define PREVIEW_ZOOM_MAX 300
#define PREVIEW_ZOOM_STEP 25
#define PREVIEW_PAD 16

typedef enum {
    PREVIEW_THEN_NOTHING,
    PREVIEW_THEN_PRINT,
    PREVIEW_THEN_PAGE_SETUP,
} PreviewFollowUp;

// A job's inputs, owned, so a follow-up can start it again once the preview's
// own operation (and the PrintJob that dies with it) is gone
typedef struct {
    GtkWindow *parent;
    NpadPrintDoc doc;
    PreviewFollowUp follow_up;
} FollowUp;

typedef struct {
    GtkPrintOperationPreview *preview;
    GtkPrintContext *context;
    const PrintJob *job;
    GtkWindow *parent;
    GtkWidget *window;
    GtkWidget *area;
    GtkWidget *page_label;
    GtkWidget *prev_button;
    GtkWidget *next_button;
    double paper_w; // The sheet, in points
    double paper_h;
    int page; // 0-based
    int zoom; // Percent of the page's true size at 96 dpi
    PreviewFollowUp follow_up;
} PreviewState;

static void follow_up_free(FollowUp *f) {
    g_free((char *) f->doc.text);
    g_free((char *) f->doc.title);
    g_free((char *) f->doc.font_family);
    g_free(f);
}

static double preview_scale(const PreviewState *ps) {
    return ps->zoom / 100.0 * 96.0 / 72.0;
}

static void preview_update(PreviewState *ps) {
    int n = ps->job->page_count;
    char *text = g_strdup_printf("Page %d of %d", ps->page + 1, n);
    gtk_label_set_text(GTK_LABEL(ps->page_label), text);
    g_free(text);
    gtk_widget_set_sensitive(ps->prev_button, ps->page > 0);
    gtk_widget_set_sensitive(ps->next_button, ps->page < n - 1);
    gtk_widget_set_size_request(ps->area, (int) (ps->paper_w * preview_scale(ps)) + 2 * PREVIEW_PAD,
                                (int) (ps->paper_h * preview_scale(ps)) + 2 * PREVIEW_PAD);
    gtk_widget_queue_draw(ps->area);
}

static void preview_goto(PreviewState *ps, int page) {
    if (page > ps->job->page_count - 1)
        page = ps->job->page_count - 1;
    if (page < 0)
        page = 0;
    ps->page = page;
    preview_update(ps);
}

static void preview_set_zoom(PreviewState *ps, int zoom) {
    if (zoom < PREVIEW_ZOOM_MIN)
        zoom = PREVIEW_ZOOM_MIN;
    if (zoom > PREVIEW_ZOOM_MAX)
        zoom = PREVIEW_ZOOM_MAX;
    ps->zoom = zoom;
    preview_update(ps);
}

static gboolean on_preview_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    PreviewState *ps = data;
    double scale = preview_scale(ps);
    double w = ps->paper_w * scale;
    double h = ps->paper_h * scale;
    double x = (gtk_widget_get_allocated_width(widget) - w) / 2;
    if (x < PREVIEW_PAD)
        x = PREVIEW_PAD;
    double y = PREVIEW_PAD;

    // The sheet is white in every colour scheme, because it is paper
    cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
    cairo_rectangle(cr, x - 1, y - 1, w + 2, h + 2);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_translate(cr, x, y);
    // The job is in points: scale points to screen pixels, and tell the
    // context its units are points too (72 dpi)
    cairo_scale(cr, scale, scale);
    gtk_print_context_set_cairo_context(ps->context, cr, 72, 72);
    gtk_print_operation_preview_render_page(ps->preview, ps->page);
    cairo_restore(cr);
    return TRUE;
}

static void preview_close(PreviewState *ps, PreviewFollowUp follow_up) {
    ps->follow_up = follow_up;
    gtk_widget_destroy(ps->window);
}

static void on_preview_prev(GtkButton *button, gpointer data) {
    (void) button;
    PreviewState *ps = data;
    preview_goto(ps, ps->page - 1);
}

static void on_preview_next(GtkButton *button, gpointer data) {
    (void) button;
    PreviewState *ps = data;
    preview_goto(ps, ps->page + 1);
}

static void on_preview_zoom_in(GtkButton *button, gpointer data) {
    (void) button;
    PreviewState *ps = data;
    preview_set_zoom(ps, ps->zoom + PREVIEW_ZOOM_STEP);
}

static void on_preview_zoom_out(GtkButton *button, gpointer data) {
    (void) button;
    PreviewState *ps = data;
    preview_set_zoom(ps, ps->zoom - PREVIEW_ZOOM_STEP);
}

static void on_preview_print(GtkButton *button, gpointer data) {
    (void) button;
    preview_close(data, PREVIEW_THEN_PRINT);
}

static void on_preview_page_setup(GtkButton *button, gpointer data) {
    (void) button;
    preview_close(data, PREVIEW_THEN_PAGE_SETUP);
}

static void on_preview_close(GtkButton *button, gpointer data) {
    (void) button;
    preview_close(data, PREVIEW_THEN_NOTHING);
}

// The same keys as the Windows preview: Esc closes, Ctrl+P prints, Page
// Up/Down turn pages, Home/End go to the first/last page and +/- zoom - each
// of the last three with or without Ctrl, and "+" also on the "=" key and the
// keypad. Windows took them bare and GTK only with Ctrl, so each platform's
// muscle memory failed on the other.
static gboolean on_preview_key(GtkWidget *widget, const GdkEventKey *event, gpointer data) {
    (void) widget;
    PreviewState *ps = data;
    bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    // Alt combinations belong to the buttons' mnemonics
    if (event->state & GDK_MOD1_MASK)
        return FALSE;
    switch (event->keyval) {
        case GDK_KEY_Escape:
            preview_close(ps, PREVIEW_THEN_NOTHING);
            return TRUE;
        case GDK_KEY_p:
        case GDK_KEY_P:
            if (!ctrl)
                return FALSE;
            preview_close(ps, PREVIEW_THEN_PRINT);
            return TRUE;
        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            preview_goto(ps, ps->page + 1);
            return TRUE;
        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            preview_goto(ps, ps->page - 1);
            return TRUE;
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            preview_goto(ps, 0);
            return TRUE;
        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            preview_goto(ps, ps->job->page_count - 1);
            return TRUE;
        case GDK_KEY_plus:
        case GDK_KEY_equal:
        case GDK_KEY_KP_Add:
            preview_set_zoom(ps, ps->zoom + PREVIEW_ZOOM_STEP);
            return TRUE;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            preview_set_zoom(ps, ps->zoom - PREVIEW_ZOOM_STEP);
            return TRUE;
        default:
            return FALSE;
    }
}

// Runs once the preview, and with it the operation, has finished: a new job
// is the simplest way to re-paginate after Page Setup, or to print what was
// previewed
static gboolean follow_up_idle(gpointer data) {
    FollowUp *f = data;
    if (f->follow_up == PREVIEW_THEN_PAGE_SETUP) {
        npad_gtk_print_page_setup(f->parent);
        npad_gtk_print_run(f->parent, &f->doc, true);
    } else {
        npad_gtk_print_run(f->parent, &f->doc, false);
    }
    follow_up_free(f);
    return G_SOURCE_REMOVE;
}

static void on_preview_destroy(GtkWidget *widget, gpointer data) {
    (void) widget;
    PreviewState *ps = data;
    FollowUp *f = NULL;
    if (ps->follow_up != PREVIEW_THEN_NOTHING) {
        // Copy what the job was built from before ending the preview frees it
        f = g_new0(FollowUp, 1);
        f->parent = ps->parent;
        f->follow_up = ps->follow_up;
        f->doc.text = g_strdup(ps->job->text);
        f->doc.title = g_strdup(ps->job->title);
        f->doc.font_family = g_strdup(ps->job->font_family);
        f->doc.font_size = ps->job->font_size;
        f->doc.font_weight = ps->job->font_weight;
        f->doc.font_italic = ps->job->font_italic;
    }
    gtk_print_operation_preview_end_preview(ps->preview);
    g_free(ps);
    if (f)
        g_idle_add(follow_up_idle, f);
}

static void preview_button(const char *label, GCallback callback, PreviewState *ps, GtkWidget *box,
                           GtkWidget **out) {
    GtkWidget *button = gtk_button_new_with_mnemonic(label);
    g_signal_connect(button, "clicked", callback, ps);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    if (out)
        *out = button;
}

// The pages are paginated: show the window
static void on_preview_ready(GtkPrintOperationPreview *preview, GtkPrintContext *context,
                             gpointer data) {
    (void) preview;
    (void) context;
    PreviewState *ps = data;
    GtkPageSetup *setup = gtk_print_context_get_page_setup(ps->context);
    ps->paper_w = gtk_page_setup_get_paper_width(setup, GTK_UNIT_POINTS);
    ps->paper_h = gtk_page_setup_get_paper_height(setup, GTK_UNIT_POINTS);
    ps->zoom = 75;

    char *title = g_strdup_printf("Print Preview - %s", ps->job->title);
    ps->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ps->window), title);
    g_free(title);
    gtk_window_set_transient_for(GTK_WINDOW(ps->window), ps->parent);
    gtk_window_set_modal(GTK_WINDOW(ps->window), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(ps->window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(ps->window), 900, 760);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(ps->window), vbox);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 6);
    gtk_box_pack_start(GTK_BOX(vbox), bar, FALSE, FALSE, 0);
    preview_button("_Print...", G_CALLBACK(on_preview_print), ps, bar, NULL);
    preview_button("Page Set_up...", G_CALLBACK(on_preview_page_setup), ps, bar, NULL);
    preview_button("_Previous", G_CALLBACK(on_preview_prev), ps, bar, &ps->prev_button);
    preview_button("_Next", G_CALLBACK(on_preview_next), ps, bar, &ps->next_button);
    ps->page_label = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(bar), ps->page_label, FALSE, FALSE, 6);
    preview_button("Zoom _In", G_CALLBACK(on_preview_zoom_in), ps, bar, NULL);
    preview_button("Zoom _Out", G_CALLBACK(on_preview_zoom_out), ps, bar, NULL);
    GtkWidget *close_button = gtk_button_new_with_mnemonic("_Close");
    g_signal_connect(close_button, "clicked", G_CALLBACK(on_preview_close), ps);
    gtk_box_pack_end(GTK_BOX(bar), close_button, FALSE, FALSE, 0);

    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scroller, TRUE, TRUE, 0);
    ps->area = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(scroller), ps->area);
    g_signal_connect(ps->area, "draw", G_CALLBACK(on_preview_draw), ps);

    g_signal_connect(ps->window, "key-press-event", G_CALLBACK(on_preview_key), ps);
    g_signal_connect(ps->window, "destroy", G_CALLBACK(on_preview_destroy), ps);

    preview_update(ps);
    gtk_widget_show_all(ps->window);
}

static gboolean on_preview_requested(GtkPrintOperation *op, GtkPrintOperationPreview *preview,
                                     GtkPrintContext *context, GtkWindow *parent, gpointer data) {
    (void) op;
    PreviewState *ps = g_new0(PreviewState, 1);
    ps->preview = preview;
    ps->context = context;
    ps->job = data;
    ps->parent = parent;

    // GTK insists on a cairo context before pagination starts, and a preview
    // has no real surface yet. Pagination reads only the resolution from it,
    // so a throwaway surface will do.
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t *cr = cairo_create(surface);
    gtk_print_context_set_cairo_context(context, cr, 72, 72);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    // Pagination finishes after this handler returns; "ready" says when
    g_signal_connect(preview, "ready", G_CALLBACK(on_preview_ready), ps);
    return TRUE;
}

static void show_error(GtkWindow *parent, const char *what, const char *detail) {
    GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK, "%s", what);
    if (detail && detail[0])
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", detail);
    gtk_window_set_title(GTK_WINDOW(dialog), "npad");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Run one print operation. action is PRINT_DIALOG (File > Print), PREVIEW
// (File > Print Preview) or PRINT (npad --print: the default printer, no
// dialog). Errors are reported to the user here. began (may be NULL) says
// whether the job got as far as begin-print.
static GtkPrintOperationResult print_operation_run(GtkWindow *parent, const NpadPrintDoc *doc,
                                                   GtkPrintOperationAction action, bool *began) {
    PrintJob *job = job_new(doc);
    bool preview = (action == GTK_PRINT_OPERATION_ACTION_PREVIEW);

    GtkPrintOperation *op = gtk_print_operation_new();
    // The job lives exactly as long as the operation, which a preview keeps
    // alive after run() returns
    g_object_set_data_full(G_OBJECT(op), "npad-print-job", job, job_free);

    GtkPageSetup *setup = page_setup_load();
    gtk_print_operation_set_default_page_setup(op, setup);
    g_object_unref(setup);
    if (g_print_settings)
        gtk_print_operation_set_print_settings(op, g_print_settings);
    gtk_print_operation_set_use_full_page(op, TRUE);
    gtk_print_operation_set_job_name(op, job->title);
    gtk_print_operation_set_embed_page_setup(op, TRUE);
    g_signal_connect(op, "begin-print", G_CALLBACK(on_begin_print), job);
    g_signal_connect(op, "draw-page", G_CALLBACK(on_draw_page), job);
    g_signal_connect(op, "preview", G_CALLBACK(on_preview_requested), job);

    // Print without a dialog, straight to a PDF: how the output is checked on
    // a machine with no printer (and the only way to script it)
    const char *export_path = g_getenv("NPAD_PRINT_EXPORT");
    if (export_path && export_path[0] && !preview) {
        gtk_print_operation_set_export_filename(op, export_path);
        action = GTK_PRINT_OPERATION_ACTION_EXPORT;
    } else if (!preview) {
        // GTK's own progress window, with Cancel, while the job is prepared
        // and sent - the same as the Windows build's
        gtk_print_operation_set_show_progress(op, TRUE);
    }

    GError *error = NULL;
    GtkPrintOperationResult result = gtk_print_operation_run(op, action, parent, &error);
    if (began)
        *began = job->began; // job is freed with op, below
    if (result == GTK_PRINT_OPERATION_RESULT_ERROR) {
        if (!parent) // npad --print: also say so where a script can see it
            fprintf(stderr, "npad: could not print this document%s%s\n", error ? ": " : "",
                    error ? error->message : "");
        show_error(parent, "npad could not print this document.", error ? error->message : NULL);
    } else if (result == GTK_PRINT_OPERATION_RESULT_APPLY) {
        // Take the new reference before dropping the old one: the operation may
        // hand back the very object we passed in
        GtkPrintSettings *used = g_object_ref(gtk_print_operation_get_print_settings(op));
        if (g_print_settings)
            g_object_unref(g_print_settings);
        g_print_settings = used;
    }
    g_clear_error(&error);
    g_object_unref(op);
    return result;
}

void npad_gtk_print_run(GtkWindow *parent, const NpadPrintDoc *doc, bool preview) {
    if (!doc)
        return;
    print_operation_run(parent, doc,
                        preview ? GTK_PRINT_OPERATION_ACTION_PREVIEW
                                : GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                        NULL);
}

int npad_gtk_print_file(const NpadPrintDoc *doc) {
    if (!doc)
        return 1;
    // ACTION_PRINT prints with the current print settings and no dialog;
    // with none (a fresh process) that is the default printer
    bool began = false;
    GtkPrintOperationResult result =
        print_operation_run(NULL, doc, GTK_PRINT_OPERATION_ACTION_PRINT, &began);
    if (result == GTK_PRINT_OPERATION_RESULT_APPLY)
        return 0;
    if (result == GTK_PRINT_OPERATION_RESULT_CANCEL && !began) {
        // GTK's unix backend starts an ACTION_PRINT at CANCEL and changes it
        // only when it finds a printer, so with none (virtual ones such as
        // Print to File are skipped) this is not the user's Cancel: say so,
        // as the Windows build does with no default printer
        const char *what = "There is no printer to print to.";
        fprintf(stderr, "npad: %s\n", what);
        show_error(NULL, what, NULL);
        return 1;
    }
    return result == GTK_PRINT_OPERATION_RESULT_CANCEL ? 2 : 1; // 2: Cancel in the progress window
}

void npad_gtk_print_cleanup(void) {
    if (g_print_settings)
        g_object_unref(g_print_settings);
    g_print_settings = NULL;
}
