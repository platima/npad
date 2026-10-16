/*
 * npad - Undo history
 * Platform-neutral undo/redo for text widgets that have none of their own
 * (GTK3's GtkTextBuffer, the terminal builds). The Win32 build does not use
 * this: RichEdit keeps its own history.
 *
 * The platform layer records every edit as it happens - an insertion or a
 * deletion, with the text involved - and replays them through a small
 * callback table on undo/redo. Positions and lengths are Unicode code point
 * offsets; text is UTF-8. Code points rather than bytes because that is what
 * GTK's character offsets count, and rather than UTF-16 units because nothing
 * here is Win32.
 *
 * Grouping follows what a user thinks of as one action:
 *  - typed characters coalesce into one step per word (a run of non-space
 *    characters plus the spaces after it), like gedit and RichEdit
 *  - consecutive single-character deletions in one direction coalesce
 *  - anything multi-character (paste, drop) is a step of its own
 *  - undo_begin_group/undo_end_group make a compound edit (Replace All, a
 *    list transform, replacing a selection) a single step
 *  - undo_break ends coalescing: call it when the caret moves by any means
 *    other than the edit itself, so typing in two places is two steps
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef UNDO_H
#define UNDO_H

#include <stdbool.h>
#include <stddef.h>

typedef struct UndoStack UndoStack;

// How undo/redo applies its changes to the document. The callbacks run while
// undo_is_applying() is true, so the platform's own change handlers can tell
// a replayed edit from a user edit and not record it again.
typedef struct {
    // Insert len bytes of UTF-8 text at code point offset pos
    void (*insert)(void *ctx, long pos, const char *text, size_t len);
    // Delete count code points starting at code point offset pos
    void (*remove)(void *ctx, long pos, long count);
} UndoTarget;

// max_steps bounds the history; the oldest steps are dropped beyond it.
UndoStack *undo_create(size_t max_steps);
void undo_destroy(UndoStack *stack);

// Forget all history (loading a new document)
void undo_clear(UndoStack *stack);

// Record an edit that has just happened. len is in bytes; text need not be
// NUL-terminated. Recording discards any redo history.
void undo_record_insert(UndoStack *stack, long pos, const char *text, size_t len);
void undo_record_delete(UndoStack *stack, long pos, const char *text, size_t len);

// End coalescing: the next recorded edit starts a new step
void undo_break(UndoStack *stack);

// Everything recorded between these is one step. Nests; the outermost pair
// decides. An empty group records nothing.
void undo_begin_group(UndoStack *stack);
void undo_end_group(UndoStack *stack);

bool undo_can_undo(const UndoStack *stack);
bool undo_can_redo(const UndoStack *stack);

// True while undo_undo/undo_redo are calling back into the target
bool undo_is_applying(const UndoStack *stack);

// Revert (or re-apply) one step. On success sel_start/sel_end (either may be
// NULL) receive the range to select afterwards: the restored text when undo
// re-inserts something, otherwise an empty range at the edit. Returns false
// when there is nothing to undo/redo.
bool undo_undo(UndoStack *stack, const UndoTarget *target, void *ctx, long *sel_start,
               long *sel_end);
bool undo_redo(UndoStack *stack, const UndoTarget *target, void *ctx, long *sel_start,
               long *sel_end);

// Number of code points in len bytes of UTF-8 (exposed for the platform
// layers and tests; continuation bytes are not counted)
long undo_utf8_length(const char *text, size_t len);

#endif // UNDO_H
