/*
 * npad - Undo history tests
 * Drives the undo stack against a tiny in-memory document, the way a
 * platform layer would: perform an edit, record it, then undo/redo through
 * the callback table and compare the text.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/undo.h"
#include <stdlib.h>
#include <string.h>

// The document: UTF-8, addressed in code points like a GtkTextBuffer
typedef struct {
    char text[1024];
} Doc;

static size_t byte_offset(const char *text, long pos) {
    size_t i = 0;
    long cp = 0;
    while (text[i] && cp < pos) {
        i++;
        while (((unsigned char) text[i] & 0xC0) == 0x80)
            i++;
        cp++;
    }
    return i;
}

static void doc_insert(void *ctx, long pos, const char *text, size_t len) {
    Doc *doc = ctx;
    size_t at = byte_offset(doc->text, pos);
    size_t tail = strlen(doc->text + at);
    memmove(doc->text + at + len, doc->text + at, tail + 1);
    memcpy(doc->text + at, text, len);
}

static void doc_remove(void *ctx, long pos, long count) {
    Doc *doc = ctx;
    size_t from = byte_offset(doc->text, pos);
    size_t to = from + byte_offset(doc->text + from, count);
    memmove(doc->text + from, doc->text + to, strlen(doc->text + to) + 1);
}

static const UndoTarget k_target = { doc_insert, doc_remove };

// A user edit: change the document, then record it - what a buffer signal
// handler does
static void type_text(Doc *doc, UndoStack *u, long pos, const char *text) {
    doc_insert(doc, pos, text, strlen(text));
    undo_record_insert(u, pos, text, strlen(text));
}

// Type one character at a time, the way keystrokes arrive
static void type_chars(Doc *doc, UndoStack *u, long pos, const char *text) {
    for (size_t i = 0; text[i]; i++) {
        char one[2] = { text[i], '\0' };
        type_text(doc, u, pos + (long) i, one);
    }
}

static void delete_text(Doc *doc, UndoStack *u, long pos, long count) {
    size_t from = byte_offset(doc->text, pos);
    size_t len = byte_offset(doc->text + from, count);
    char removed[256];
    memcpy(removed, doc->text + from, len);
    removed[len] = '\0';
    doc_remove(doc, pos, count);
    undo_record_delete(u, pos, removed, len);
}

TEST_CASE(empty_stack) {
    UndoStack *u = undo_create(100);
    Doc doc = { "abc" };
    TEST_ASSERT(!undo_can_undo(u), "nothing to undo");
    TEST_ASSERT(!undo_can_redo(u), "nothing to redo");
    TEST_ASSERT(!undo_undo(u, &k_target, &doc, NULL, NULL), "undo on empty fails");
    TEST_ASSERT_STR_EQ("abc", doc.text, "document untouched");
    undo_destroy(u);
}

TEST_CASE(typing_coalesces_per_word) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "hello world");
    TEST_ASSERT_STR_EQ("hello world", doc.text, "typed");

    long s = -1, e = -1;
    TEST_ASSERT(undo_undo(u, &k_target, &doc, &s, &e), "undo word");
    TEST_ASSERT_STR_EQ("hello ", doc.text, "second word undone as one step");
    TEST_ASSERT_EQ(6, s, "caret at edit");
    TEST_ASSERT_EQ(6, e, "empty selection");

    TEST_ASSERT(undo_undo(u, &k_target, &doc, NULL, NULL), "undo first word");
    TEST_ASSERT_STR_EQ("", doc.text, "first word plus its space undone together");
    TEST_ASSERT(!undo_can_undo(u), "history exhausted");
    undo_destroy(u);
}

TEST_CASE(redo_restores) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "one two");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("", doc.text, "both undone");

    long s = -1, e = -1;
    TEST_ASSERT(undo_redo(u, &k_target, &doc, &s, &e), "redo");
    TEST_ASSERT_STR_EQ("one ", doc.text, "first step redone");
    TEST_ASSERT_EQ(4, s, "caret after redone insert");
    TEST_ASSERT(undo_redo(u, &k_target, &doc, NULL, NULL), "redo again");
    TEST_ASSERT_STR_EQ("one two", doc.text, "fully redone");
    TEST_ASSERT(!undo_can_redo(u), "redo exhausted");
    undo_destroy(u);
}

TEST_CASE(new_edit_discards_redo) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "abc");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT(undo_can_redo(u), "redo available");
    type_chars(&doc, u, 0, "x");
    TEST_ASSERT(!undo_can_redo(u), "a new edit forks the history");
    undo_destroy(u);
}

TEST_CASE(caret_move_breaks_coalescing) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "ab");
    undo_break(u); // The user clicked elsewhere
    type_chars(&doc, u, 2, "cd");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("ab", doc.text, "only the second run undone");
    undo_destroy(u);
}

TEST_CASE(non_adjacent_typing_is_separate) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "abc");
    type_chars(&doc, u, 0, "X"); // Typed at the start without a break
    TEST_ASSERT_STR_EQ("Xabc", doc.text, "typed");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("abc", doc.text, "non-contiguous insert is its own step");
    undo_destroy(u);
}

TEST_CASE(backspace_coalesces) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_text(&doc, u, 0, "hello");
    undo_break(u);
    delete_text(&doc, u, 4, 1);
    delete_text(&doc, u, 3, 1);
    delete_text(&doc, u, 2, 1);
    TEST_ASSERT_STR_EQ("he", doc.text, "backspaced");

    long s = -1, e = -1;
    undo_undo(u, &k_target, &doc, &s, &e);
    TEST_ASSERT_STR_EQ("hello", doc.text, "three backspaces undone together");
    TEST_ASSERT_EQ(2, s, "restored text selected (start)");
    TEST_ASSERT_EQ(5, e, "restored text selected (end)");
    undo_destroy(u);
}

TEST_CASE(forward_delete_coalesces) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_text(&doc, u, 0, "hello");
    undo_break(u);
    delete_text(&doc, u, 1, 1);
    delete_text(&doc, u, 1, 1);
    TEST_ASSERT_STR_EQ("hlo", doc.text, "deleted forwards");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("hello", doc.text, "both deletes undone together");
    undo_destroy(u);
}

TEST_CASE(direction_change_splits) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_text(&doc, u, 0, "abcdef");
    undo_break(u);
    delete_text(&doc, u, 3, 1); // Forward delete at 3: "abcef"
    delete_text(&doc, u, 3, 1); // Forward again: "abcf"
    delete_text(&doc, u, 2, 1); // Backspace: "abf"
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("abcf", doc.text, "the backspace is its own step");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("abcdef", doc.text, "the forward deletes were one step");
    undo_destroy(u);
}

TEST_CASE(paste_stands_alone) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "ab");
    type_text(&doc, u, 2, "PASTED"); // Multi-character: a paste
    type_chars(&doc, u, 8, "cd");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("abPASTED", doc.text, "typing after a paste is separate");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("ab", doc.text, "the paste is one step");
    undo_destroy(u);
}

TEST_CASE(group_is_one_step) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_text(&doc, u, 0, "a-b-c");
    // Replace All "-" with "+" as a compound edit
    undo_begin_group(u);
    delete_text(&doc, u, 1, 1);
    type_text(&doc, u, 1, "+");
    delete_text(&doc, u, 3, 1);
    type_text(&doc, u, 3, "+");
    undo_end_group(u);
    TEST_ASSERT_STR_EQ("a+b+c", doc.text, "replaced");

    long s = -1, e = -1;
    undo_undo(u, &k_target, &doc, &s, &e);
    TEST_ASSERT_STR_EQ("a-b-c", doc.text, "whole group undone at once");
    TEST_ASSERT_EQ(1, s, "selection from the group's first edit");
    undo_redo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("a+b+c", doc.text, "whole group redone at once");
    undo_destroy(u);
}

TEST_CASE(nested_and_empty_groups) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    undo_begin_group(u);
    undo_end_group(u);
    TEST_ASSERT(!undo_can_undo(u), "an empty group records nothing");

    undo_begin_group(u);
    type_text(&doc, u, 0, "x");
    undo_begin_group(u);
    type_text(&doc, u, 1, "y");
    undo_end_group(u);
    type_text(&doc, u, 2, "z");
    undo_end_group(u);
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("", doc.text, "nested groups collapse into the outer step");
    undo_destroy(u);
}

TEST_CASE(typing_after_group_does_not_join_it) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    undo_begin_group(u);
    type_text(&doc, u, 0, "a");
    undo_end_group(u);
    type_text(&doc, u, 1, "b");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("a", doc.text, "typed character is its own step");
    undo_destroy(u);
}

// A one-character paste after typing: the platform groups it (GTK flushes a
// user action holding one edit as a plain record, which would otherwise extend
// the word being typed)
TEST_CASE(one_character_group_after_typing_stands_alone) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "abc");
    undo_begin_group(u);
    type_text(&doc, u, 3, "x");
    undo_end_group(u);
    type_chars(&doc, u, 4, "de");
    TEST_ASSERT_STR_EQ("abcxde", doc.text, "typed, pasted, typed");

    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("abcx", doc.text, "typing after the paste is its own step");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("abc", doc.text, "the paste is one step, not merged into abc");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("", doc.text, "the typing is intact");
    undo_destroy(u);
}

// Without the group the same record merges: the reason callers must group
TEST_CASE(ungrouped_one_character_insert_merges) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "abc");
    type_text(&doc, u, 3, "x");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("", doc.text, "a bare single insert extends the word");
    undo_destroy(u);
}

// Replace All with one match and an empty replacement, or a one-character cut,
// right after Backspaces: a single delete that must not join them
TEST_CASE(one_character_delete_group_after_backspace_stands_alone) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_text(&doc, u, 0, "hello");
    undo_break(u);
    delete_text(&doc, u, 4, 1); // Backspace: "hell"
    undo_begin_group(u);
    delete_text(&doc, u, 3, 1); // Cut / Replace All of the "l" before it: "hel"
    undo_end_group(u);
    delete_text(&doc, u, 2, 1); // Another Backspace: "he"
    TEST_ASSERT_STR_EQ("he", doc.text, "edited");

    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("hel", doc.text, "the later Backspace alone");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("hell", doc.text, "the grouped delete alone");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("hello", doc.text, "the first Backspace alone");
    undo_destroy(u);
}

// undo_break on both sides isolates a single edit the platform cannot group
// (GTK's own middle-click paste and text drops, whose ends it only observes)
TEST_CASE(break_on_both_sides_isolates_a_single_edit) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_chars(&doc, u, 0, "ab");
    undo_break(u);
    type_text(&doc, u, 2, "x");
    undo_break(u);
    type_chars(&doc, u, 3, "cd");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("abx", doc.text, "typing after it separate");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    TEST_ASSERT_STR_EQ("ab", doc.text, "the edit itself separate");
    undo_destroy(u);
}

TEST_CASE(multibyte_positions) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    // Two-byte, three-byte and four-byte characters: positions are code points
    type_text(&doc, u, 0, "\xC3\xA9"); // e-acute
    type_text(&doc, u, 1, "\xE2\x82\xAC"); // euro sign
    type_text(&doc, u, 2, "\xF0\x9F\x98\x80"); // grinning face
    TEST_ASSERT_EQ(3, undo_utf8_length(doc.text, strlen(doc.text)), "three code points");

    undo_break(u);
    delete_text(&doc, u, 1, 1); // Remove the euro sign by code point
    TEST_ASSERT_STR_EQ("\xC3\xA9\xF0\x9F\x98\x80", doc.text, "euro removed");
    long s = -1, e = -1;
    undo_undo(u, &k_target, &doc, &s, &e);
    TEST_ASSERT_STR_EQ("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", doc.text, "euro restored");
    TEST_ASSERT_EQ(1, s, "selection start in code points");
    TEST_ASSERT_EQ(2, e, "selection end in code points");
    undo_destroy(u);
}

TEST_CASE(history_limit_drops_oldest) {
    UndoStack *u = undo_create(3);
    Doc doc = { "" };
    for (int i = 0; i < 5; i++) {
        undo_break(u);
        type_text(&doc, u, i, "x");
    }
    int undone = 0;
    while (undo_undo(u, &k_target, &doc, NULL, NULL))
        undone++;
    TEST_ASSERT_EQ(3, undone, "only the newest three steps kept");
    TEST_ASSERT_STR_EQ("xx", doc.text, "the two oldest edits remain");
    undo_destroy(u);
}

// A target whose callbacks record what they do, as a platform's buffer signal
// handlers would if they forgot to check undo_is_applying
typedef struct {
    Doc doc;
    UndoStack *u;
} RecordingDoc;

static void rec_insert(void *ctx, long pos, const char *text, size_t len) {
    RecordingDoc *r = ctx;
    doc_insert(&r->doc, pos, text, len);
    undo_record_insert(r->u, pos, text, len);
}

static void rec_remove(void *ctx, long pos, long count) {
    RecordingDoc *r = ctx;
    size_t from = byte_offset(r->doc.text, pos);
    size_t len = byte_offset(r->doc.text + from, count);
    char removed[256];
    memcpy(removed, r->doc.text + from, len);
    removed[len] = '\0';
    doc_remove(&r->doc, pos, count);
    undo_record_delete(r->u, pos, removed, len);
}

TEST_CASE(replay_is_not_recorded) {
    RecordingDoc r = { { "" }, undo_create(100) };
    const UndoTarget target = { rec_insert, rec_remove };
    type_text(&r.doc, r.u, 0, "abc");
    TEST_ASSERT(!undo_is_applying(r.u), "idle");

    TEST_ASSERT(undo_undo(r.u, &target, &r, NULL, NULL), "undo");
    TEST_ASSERT_STR_EQ("", r.doc.text, "undone");
    TEST_ASSERT(undo_can_redo(r.u), "replayed removal did not fork the history");
    TEST_ASSERT(!undo_can_undo(r.u), "and did not add an undo step");

    TEST_ASSERT(undo_redo(r.u, &target, &r, NULL, NULL), "redo");
    TEST_ASSERT_STR_EQ("abc", r.doc.text, "redone");
    TEST_ASSERT(!undo_can_redo(r.u), "redo consumed");
    undo_destroy(r.u);
}

// From undo.c, compiled in for this test only (-DUNDO_FAULT_INJECTION): the
// Nth allocation from now fails
void undo_test_fail_allocation(int nth);

TEST_CASE(failed_group_record_drops_history) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_text(&doc, u, 0, "abc");
    // Replace "a" with "X" as one group, but the insertion cannot be recorded
    undo_begin_group(u);
    delete_text(&doc, u, 0, 1);
    undo_test_fail_allocation(1); // The insertion's text copy
    type_text(&doc, u, 0, "X");
    TEST_ASSERT(!undo_can_undo(u), "history dropped, not left describing a lost edit");
    type_text(&doc, u, 1, "Y"); // Later in the same group: ignored, not half-recorded
    undo_end_group(u);
    TEST_ASSERT_STR_EQ("XYbc", doc.text, "document as edited");
    TEST_ASSERT(!undo_undo(u, &k_target, &doc, NULL, NULL), "nothing to replay");
    TEST_ASSERT_STR_EQ("XYbc", doc.text, "undo did not corrupt the text (was aXbc-style)");

    // The next edit after the group records normally again
    type_text(&doc, u, 4, "!");
    TEST_ASSERT(undo_undo(u, &k_target, &doc, NULL, NULL), "fresh history works");
    TEST_ASSERT_STR_EQ("XYbc", doc.text, "and undoes correctly");
    undo_destroy(u);
}

TEST_CASE(failed_record_outside_group_drops_history) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_text(&doc, u, 0, "ab");
    undo_break(u);
    undo_test_fail_allocation(1); // The new step's op array
    type_text(&doc, u, 2, "cd");
    TEST_ASSERT(!undo_can_undo(u), "older steps dropped: their positions no longer hold");
    type_text(&doc, u, 4, "ef");
    TEST_ASSERT(undo_undo(u, &k_target, &doc, NULL, NULL), "recording resumes");
    TEST_ASSERT_STR_EQ("abcd", doc.text, "only the edit after the failure undone");
    TEST_ASSERT(!undo_can_undo(u), "nothing older survives");
    undo_destroy(u);
}

TEST_CASE(clear_forgets_everything) {
    UndoStack *u = undo_create(100);
    Doc doc = { "" };
    type_text(&doc, u, 0, "abc");
    undo_undo(u, &k_target, &doc, NULL, NULL);
    undo_clear(u);
    TEST_ASSERT(!undo_can_undo(u), "no undo after clear");
    TEST_ASSERT(!undo_can_redo(u), "no redo after clear");
    undo_destroy(u);
}

int main(void) {
    TEST_INIT();
    RUN_TEST(empty_stack);
    RUN_TEST(typing_coalesces_per_word);
    RUN_TEST(redo_restores);
    RUN_TEST(new_edit_discards_redo);
    RUN_TEST(caret_move_breaks_coalescing);
    RUN_TEST(non_adjacent_typing_is_separate);
    RUN_TEST(backspace_coalesces);
    RUN_TEST(forward_delete_coalesces);
    RUN_TEST(direction_change_splits);
    RUN_TEST(paste_stands_alone);
    RUN_TEST(group_is_one_step);
    RUN_TEST(nested_and_empty_groups);
    RUN_TEST(typing_after_group_does_not_join_it);
    RUN_TEST(one_character_group_after_typing_stands_alone);
    RUN_TEST(ungrouped_one_character_insert_merges);
    RUN_TEST(one_character_delete_group_after_backspace_stands_alone);
    RUN_TEST(break_on_both_sides_isolates_a_single_edit);
    RUN_TEST(multibyte_positions);
    RUN_TEST(history_limit_drops_oldest);
    RUN_TEST(replay_is_not_recorded);
    RUN_TEST(failed_group_record_drops_history);
    RUN_TEST(failed_record_outside_group_drops_history);
    RUN_TEST(clear_forgets_everything);
    TEST_SUMMARY();
}
