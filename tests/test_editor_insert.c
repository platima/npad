/*
 * npad - Ctrl+drop insert tests
 * editor_read_file_for_insert must ask the questions Open asks before a file
 * is inserted into the document: a binary-looking file and one over the
 * large-file threshold are confirmed, a file npad can only read in part is
 * confirmed, and a read failure is reported. Before, Ctrl+drop inserted
 * anything without a word (and on Windows a failed read showed nothing).
 *
 * editor.c is linked against scripted UI stubs below: every message box is
 * counted and answered from g_answer.
 *
 * Author: Platima
 * https://npad.dev
 */

// mkdir/rmdir (c99 mode hides them otherwise)
#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"
#include "../src/core/editor.h"
#include "../src/core/settings.h"
#include "../src/core/thread_safety.h"
#include "../src/ui_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir((p), 0755)
#endif

// --- Scripted UI -----------------------------------------------------------

static int g_boxes;     // Message boxes shown
static int g_questions; // ...of which were yes/no questions
static bool g_answer;   // What every question is answered
static char g_last_box[1024];

bool ui_show_message_box(Window *parent, const char *title, const char *message, bool is_question) {
    (void) parent;
    (void) title;
    g_boxes++;
    if (is_question)
        g_questions++;
    snprintf(g_last_box, sizeof g_last_box, "%s", message ? message : "");
    return is_question ? g_answer : true;
}

UiAnswer ui_ask_question(Window *parent, const char *title, const char *message) {
    return ui_show_message_box(parent, title, message, true) ? UI_ANSWER_YES : UI_ANSWER_NO;
}

static void reset_ui(bool answer) {
    g_boxes = 0;
    g_questions = 0;
    g_answer = answer;
    g_last_box[0] = '\0';
}

// The rest of the UI surface editor.c links against; none is reached here
void ui_clear_text(Window *window) {
    (void) window;
}
void ui_copy(Window *window) {
    (void) window;
}
void ui_cut(Window *window) {
    (void) window;
}
void ui_paste(Window *window) {
    (void) window;
}
void ui_undo(Window *window) {
    (void) window;
}
void ui_redo(Window *window) {
    (void) window;
}
void ui_select_all(Window *window) {
    (void) window;
}
char *ui_get_text(Window *window) {
    (void) window;
    return NULL;
}
bool ui_set_text(Window *window, const char *text) {
    (void) window;
    (void) text;
    return true;
}
bool ui_text_altered_by_load(const Window *window) {
    (void) window;
    return false;
}
void ui_append_time_date_line(Window *window) {
    (void) window;
}
void ui_set_text_modified(Window *window, bool modified) {
    (void) window;
    (void) modified;
}
void ui_set_window_title(Window *window, const char *title) {
    (void) window;
    (void) title;
}
void ui_set_status_info(Window *window, const char *encoding_name, const char *eol_name) {
    (void) window;
    (void) encoding_name;
    (void) eol_name;
}
void ui_set_status_message(Window *window, const char *message) {
    (void) window;
    (void) message;
}
bool ui_is_dark_mode(void) {
    return false;
}
void ui_set_dark_mode(bool enabled) {
    (void) enabled;
}
void ui_launch_recovery_instance(const char *slot_id, int cascade_index) {
    (void) slot_id;
    (void) cascade_index;
}
void ui_open_with_default_app(const char *filename) {
    (void) filename;
}
bool ui_pid_is_running(long pid, long long *start_time) {
    (void) pid;
    if (start_time)
        *start_time = 0;
    return false;
}
UiOpenChoice ui_prompt_binary_open(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    return UI_OPEN_CANCEL;
}
void ui_quit(void) {
}
void ui_set_auto_save_timer(Window *window, int seconds) {
    (void) window;
    (void) seconds;
}
void ui_set_session_timer(Window *window, int seconds) {
    (void) window;
    (void) seconds;
}
void ui_set_event_handler(UIEventHandler handler) {
    (void) handler;
}
void ui_show_find_dialog(Window *parent) {
    (void) parent;
}
void ui_show_replace_dialog(Window *parent) {
    (void) parent;
}
char *ui_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) parent;
    (void) params;
    return NULL;
}
char *ui_show_save_dialog(Window *parent, FileDialogParams *params) {
    (void) parent;
    (void) params;
    return NULL;
}
SavePromptResult ui_show_save_prompt(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    return UI_SAVE_PROMPT_CANCEL;
}
size_t ui_system_memory_mb(void) {
    return 0; // The default threshold then falls back to a fixed size
}

// --- Fixtures ----------------------------------------------------------------

#define DIR_NAME "test_insert_tmp"
#define TEXT_FILE DIR_NAME "/notes.txt"
#define BINARY_FILE DIR_NAME "/image.png"
#define LATE_NUL_FILE DIR_NAME "/late_nul.log"
#define LARGE_FILE DIR_NAME "/large.txt"
#define MISSING_FILE DIR_NAME "/missing.txt"

static bool write_bytes(const char *path, const unsigned char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static void make_fixtures(void) {
    make_dir(DIR_NAME);

    const char *text = "first line\nsecond line\n";
    write_bytes(TEXT_FILE, (const unsigned char *) text, strlen(text));

    // A PNG signature and a header chunk: NUL bytes in the first few bytes
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
                                         0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                                         0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10 };
    write_bytes(BINARY_FILE, png, sizeof png);

    // Text for well past the 8 KB the binary sniff reads, then a NUL: it does
    // not look binary, but npad can only read the part before the NUL
    size_t late_len = 12000;
    unsigned char *late = malloc(late_len);
    if (late) {
        for (size_t i = 0; i < late_len; i++)
            late[i] = (i % 40 == 39) ? (unsigned char) '\n' : (unsigned char) 'a';
        late[10000] = 0x00;
        write_bytes(LATE_NUL_FILE, late, late_len);
        free(late);
    }

    // 1.5 MB of text, over a 1 MB threshold
    size_t large_len = (size_t) 1536 * 1024;
    unsigned char *large = malloc(large_len);
    if (large) {
        for (size_t i = 0; i < large_len; i++)
            large[i] = (i % 64 == 63) ? (unsigned char) '\n' : (unsigned char) 'x';
        write_bytes(LARGE_FILE, large, large_len);
        free(large);
    }
}

static void remove_fixtures(void) {
    remove(TEXT_FILE);
    remove(BINARY_FILE);
    remove(LATE_NUL_FILE);
    remove(LARGE_FILE);
#ifdef _WIN32
    _rmdir(DIR_NAME);
#else
    rmdir(DIR_NAME);
#endif
}

// --- Cases -------------------------------------------------------------------

TEST_CASE(text_file_inserts_without_a_prompt) {
    reset_ui(true);
    char *text = editor_read_file_for_insert(TEXT_FILE);
    TEST_ASSERT_NOT_NULL(text, "a text file is read");
    TEST_ASSERT_STR_EQ("first line\nsecond line\n", text, "the whole text comes back");
    TEST_ASSERT_EQ(0, g_boxes, "and nothing is asked");
    free(text);
}

TEST_CASE(missing_file_is_reported) {
    reset_ui(true);
    char *text = editor_read_file_for_insert(MISSING_FILE);
    TEST_ASSERT_NULL(text, "nothing to insert");
    TEST_ASSERT_EQ(1, g_boxes, "one box");
    TEST_ASSERT_EQ(0, g_questions, "an error, not a question");
    TEST_ASSERT(strstr(g_last_box, "Could not insert file") != NULL, "it says what failed");
    TEST_ASSERT(strstr(g_last_box, "missing.txt") != NULL, "and names the file");
}

TEST_CASE(binary_file_is_confirmed_first) {
    reset_ui(false);
    char *text = editor_read_file_for_insert(BINARY_FILE);
    TEST_ASSERT_NULL(text, "declined: nothing is inserted");
    TEST_ASSERT_EQ(1, g_questions, "the binary question was asked");
    TEST_ASSERT(strstr(g_last_box, "image.png") != NULL, "naming the file");

    reset_ui(true);
    text = editor_read_file_for_insert(BINARY_FILE);
    TEST_ASSERT_NOT_NULL(text, "accepted: the readable part is inserted");
    TEST_ASSERT_EQ(1, g_questions, "and only once: no second truncation question");
    free(text);
}

TEST_CASE(partly_readable_file_is_confirmed) {
    reset_ui(false);
    char *text = editor_read_file_for_insert(LATE_NUL_FILE);
    TEST_ASSERT_NULL(text, "declined: nothing is inserted");
    TEST_ASSERT_EQ(1, g_questions, "a file cut short by a NUL is asked about");
    TEST_ASSERT(strstr(g_last_box, "binary data") != NULL, "saying why");

    reset_ui(true);
    text = editor_read_file_for_insert(LATE_NUL_FILE);
    TEST_ASSERT_NOT_NULL(text, "accepted: the part before the NUL");
    TEST_ASSERT_EQ(10000, (long) strlen(text), "exactly that part");
    free(text);
}

TEST_CASE(large_file_uses_the_open_threshold) {
    settings_set_int("large_file_warning_mb", 1);

    reset_ui(false);
    char *text = editor_read_file_for_insert(LARGE_FILE);
    TEST_ASSERT_NULL(text, "declined: nothing is read");
    TEST_ASSERT_EQ(1, g_questions, "over the threshold is asked about");
    TEST_ASSERT(strstr(g_last_box, "MB") != NULL, "with the size");

    reset_ui(true);
    text = editor_read_file_for_insert(LARGE_FILE);
    TEST_ASSERT_NOT_NULL(text, "accepted: the file is read");
    TEST_ASSERT_EQ((long) 1536 * 1024, (long) strlen(text), "in full");
    free(text);

    // 0 turns the warning off, as it does for Open
    settings_set_int("large_file_warning_mb", 0);
    reset_ui(false);
    text = editor_read_file_for_insert(LARGE_FILE);
    TEST_ASSERT_NOT_NULL(text, "no threshold: read straight away");
    TEST_ASSERT_EQ(0, g_boxes, "without asking");
    free(text);
    settings_remove_key("large_file_warning_mb");
}

TEST_CASE(null_and_empty_names_are_ignored) {
    reset_ui(true);
    TEST_ASSERT_NULL(editor_read_file_for_insert(NULL), "NULL");
    TEST_ASSERT_NULL(editor_read_file_for_insert(""), "empty");
    TEST_ASSERT_EQ(0, g_boxes, "quietly");
}

int main(void) {
    thread_safety_init();
    TEST_INIT();
    make_fixtures();

    RUN_TEST(text_file_inserts_without_a_prompt);
    RUN_TEST(missing_file_is_reported);
    RUN_TEST(binary_file_is_confirmed_first);
    RUN_TEST(partly_readable_file_is_confirmed);
    RUN_TEST(large_file_uses_the_open_threshold);
    RUN_TEST(null_and_empty_names_are_ignored);

    remove_fixtures();
    TEST_SUMMARY();
    thread_safety_cleanup();
    return 0;
}
