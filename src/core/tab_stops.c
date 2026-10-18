/*
 * npad - Tab stops
 *
 * Author: Platima
 * https://npad.dev
 */

#include "tab_stops.h"
#include "settings.h"

bool tab_inserts_spaces_enabled(void) {
    return settings_get_bool(TAB_INSERTS_SPACES_KEY, false);
}

int tab_clamp_width(int width) {
    if (width < TAB_WIDTH_MIN)
        return TAB_WIDTH_MIN;
    if (width > TAB_WIDTH_MAX)
        return TAB_WIDTH_MAX;
    return width;
}

int tab_width_setting(void) {
    return tab_clamp_width(settings_get_int(TAB_WIDTH_KEY, TAB_WIDTH_DEFAULT));
}

int tab_list_indent_width(void) {
    return tab_inserts_spaces_enabled() ? tab_width_setting() : TAB_WIDTH_DEFAULT;
}

size_t tab_column_after(const char *line_prefix, size_t len, int width) {
    size_t w = (size_t) tab_clamp_width(width);
    size_t col = 0;
    if (!line_prefix)
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char) line_prefix[i];
        if (c == '\n' || c == '\r')
            col = 0;
        else if (c == '\t')
            col += w - col % w;
        else if ((c & 0xC0) != 0x80) // not a UTF-8 continuation byte
            col++;
    }
    return col;
}

int tab_spaces_to_stop(size_t column, int width) {
    size_t w = (size_t) tab_clamp_width(width);
    return (int) (w - column % w);
}

int tab_spaces_for_line_prefix(const char *line_prefix, size_t len, int width) {
    return tab_spaces_to_stop(tab_column_after(line_prefix, len, width), width);
}
