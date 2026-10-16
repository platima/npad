/*
 * npad - Undo history
 * See undo.h for the model.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "undo.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum { UNDO_OP_INSERT, UNDO_OP_DELETE } UndoOpKind;

typedef struct {
    UndoOpKind kind;
    long pos;   // Code point offset the edit starts at
    long count; // Length in code points
    char *text; // NUL-terminated UTF-8 copy
    size_t len; // Length in bytes
} UndoOp;

typedef struct {
    UndoOp *ops;
    size_t count;
    size_t capacity;
} UndoStep;

// What the newest step may still absorb. Anything other than UNDO_MERGE_NONE
// means it holds exactly one single-purpose op that is still growing.
typedef enum {
    UNDO_MERGE_NONE,
    UNDO_MERGE_TYPING,        // Inserted characters, extended at the end
    UNDO_MERGE_DELETE_EITHER, // One deleted character; direction not yet known
    UNDO_MERGE_BACKSPACE,     // Deleting backwards, extended at the front
    UNDO_MERGE_DELETE         // Deleting forwards, extended at the end
} UndoMerge;

struct UndoStack {
    UndoStep *steps;
    size_t count;    // Steps stored: undoable plus redoable
    size_t current;  // steps[0..current) are undoable, the rest redoable
    size_t capacity; // Allocated slots in steps
    size_t max_steps;
    int group_depth;
    bool group_has_step; // The outermost open group already started its step
    UndoMerge merge;
    bool applying;
};

long undo_utf8_length(const char *text, size_t len) {
    long count = 0;
    if (!text)
        return 0;
    for (size_t i = 0; i < len; i++) {
        if (((unsigned char) text[i] & 0xC0) != 0x80)
            count++;
    }
    return count;
}

static bool is_space_byte(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void step_free(UndoStep *step) {
    for (size_t i = 0; i < step->count; i++)
        free(step->ops[i].text);
    free(step->ops);
    step->ops = NULL;
    step->count = 0;
    step->capacity = 0;
}

UndoStack *undo_create(size_t max_steps) {
    UndoStack *stack = calloc(1, sizeof(UndoStack));
    if (!stack)
        return NULL;
    stack->max_steps = max_steps ? max_steps : SIZE_MAX;
    return stack;
}

void undo_clear(UndoStack *stack) {
    if (!stack)
        return;
    for (size_t i = 0; i < stack->count; i++)
        step_free(&stack->steps[i]);
    stack->count = 0;
    stack->current = 0;
    stack->group_has_step = false;
    stack->merge = UNDO_MERGE_NONE;
}

void undo_destroy(UndoStack *stack) {
    if (!stack)
        return;
    undo_clear(stack);
    free(stack->steps);
    free(stack);
}

// Discard everything that could be redone: a new edit forks the history
static void drop_redo(UndoStack *stack) {
    for (size_t i = stack->current; i < stack->count; i++)
        step_free(&stack->steps[i]);
    stack->count = stack->current;
}

// Append an empty step, dropping the oldest when the history is full
static UndoStep *push_step(UndoStack *stack) {
    if (stack->count >= stack->max_steps && stack->count > 0) {
        step_free(&stack->steps[0]);
        memmove(&stack->steps[0], &stack->steps[1], (stack->count - 1) * sizeof(UndoStep));
        stack->count--;
        if (stack->current > 0)
            stack->current--;
    }
    if (stack->count == stack->capacity) {
        size_t capacity = stack->capacity ? stack->capacity * 2 : 64;
        UndoStep *grown = realloc(stack->steps, capacity * sizeof(UndoStep));
        if (!grown)
            return NULL;
        stack->steps = grown;
        stack->capacity = capacity;
    }
    UndoStep *step = &stack->steps[stack->count++];
    memset(step, 0, sizeof(*step));
    stack->current = stack->count;
    return step;
}

static bool step_append(UndoStep *step, UndoOpKind kind, long pos, long count, const char *text,
                        size_t len) {
    if (step->count == step->capacity) {
        size_t capacity = step->capacity ? step->capacity * 2 : 4;
        UndoOp *grown = realloc(step->ops, capacity * sizeof(UndoOp));
        if (!grown)
            return false;
        step->ops = grown;
        step->capacity = capacity;
    }
    char *copy = malloc(len + 1);
    if (!copy)
        return false;
    memcpy(copy, text, len);
    copy[len] = '\0';

    UndoOp *op = &step->ops[step->count++];
    op->kind = kind;
    op->pos = pos;
    op->count = count;
    op->text = copy;
    op->len = len;
    return true;
}

// Grow op's text by len bytes, at the front or the back
static bool op_extend(UndoOp *op, const char *text, size_t len, long count, bool at_front) {
    char *grown = malloc(op->len + len + 1);
    if (!grown)
        return false;
    if (at_front) {
        memcpy(grown, text, len);
        memcpy(grown + len, op->text, op->len);
    } else {
        memcpy(grown, op->text, op->len);
        memcpy(grown + op->len, text, len);
    }
    grown[op->len + len] = '\0';
    free(op->text);
    op->text = grown;
    op->len += len;
    op->count += count;
    return true;
}

// Fold a one-character edit into the newest step when it continues it
static bool try_merge(UndoStack *stack, UndoOpKind kind, long pos, long count, const char *text,
                      size_t len) {
    if (stack->merge == UNDO_MERGE_NONE || count != 1 || stack->current == 0)
        return false;
    UndoStep *step = &stack->steps[stack->current - 1];
    if (step->count != 1)
        return false;
    UndoOp *op = &step->ops[0];

    if (kind == UNDO_OP_INSERT) {
        if (stack->merge != UNDO_MERGE_TYPING || op->kind != UNDO_OP_INSERT ||
            pos != op->pos + op->count)
            return false;
        // One step per word: a non-space after a space starts the next one
        if (op->len > 0 && is_space_byte(op->text[op->len - 1]) && !is_space_byte(text[0]))
            return false;
        return op_extend(op, text, len, count, false);
    }

    if (op->kind != UNDO_OP_DELETE)
        return false;
    bool backspace_ok =
        stack->merge == UNDO_MERGE_BACKSPACE || stack->merge == UNDO_MERGE_DELETE_EITHER;
    bool delete_ok = stack->merge == UNDO_MERGE_DELETE || stack->merge == UNDO_MERGE_DELETE_EITHER;
    if (backspace_ok && pos + count == op->pos) {
        if (!op_extend(op, text, len, count, true))
            return false;
        op->pos = pos;
        stack->merge = UNDO_MERGE_BACKSPACE;
        return true;
    }
    if (delete_ok && pos == op->pos) {
        if (!op_extend(op, text, len, count, false))
            return false;
        stack->merge = UNDO_MERGE_DELETE;
        return true;
    }
    return false;
}

static void record(UndoStack *stack, UndoOpKind kind, long pos, const char *text, size_t len) {
    if (!stack || stack->applying || !text || len == 0 || pos < 0)
        return;

    drop_redo(stack);
    long count = undo_utf8_length(text, len);

    if (stack->group_depth > 0) {
        if (!stack->group_has_step) {
            if (!push_step(stack))
                return;
            stack->group_has_step = true;
        }
        step_append(&stack->steps[stack->current - 1], kind, pos, count, text, len);
        stack->merge = UNDO_MERGE_NONE;
        return;
    }

    if (try_merge(stack, kind, pos, count, text, len))
        return;

    UndoStep *step = push_step(stack);
    if (!step)
        return;
    if (!step_append(step, kind, pos, count, text, len)) {
        stack->count--; // Leave no empty step behind
        stack->current = stack->count;
        stack->merge = UNDO_MERGE_NONE;
        return;
    }
    if (count == 1)
        stack->merge = (kind == UNDO_OP_INSERT) ? UNDO_MERGE_TYPING : UNDO_MERGE_DELETE_EITHER;
    else
        stack->merge = UNDO_MERGE_NONE; // Pastes and drops stand alone
}

void undo_record_insert(UndoStack *stack, long pos, const char *text, size_t len) {
    record(stack, UNDO_OP_INSERT, pos, text, len);
}

void undo_record_delete(UndoStack *stack, long pos, const char *text, size_t len) {
    record(stack, UNDO_OP_DELETE, pos, text, len);
}

void undo_break(UndoStack *stack) {
    if (stack)
        stack->merge = UNDO_MERGE_NONE;
}

void undo_begin_group(UndoStack *stack) {
    if (!stack)
        return;
    if (stack->group_depth == 0)
        stack->group_has_step = false;
    stack->group_depth++;
}

void undo_end_group(UndoStack *stack) {
    if (!stack || stack->group_depth == 0)
        return;
    stack->group_depth--;
    if (stack->group_depth == 0) {
        stack->group_has_step = false;
        stack->merge = UNDO_MERGE_NONE; // The next edit never joins a group
    }
}

bool undo_can_undo(const UndoStack *stack) {
    return stack && stack->current > 0;
}

bool undo_can_redo(const UndoStack *stack) {
    return stack && stack->current < stack->count;
}

bool undo_is_applying(const UndoStack *stack) {
    return stack && stack->applying;
}

static void set_selection(long *sel_start, long *sel_end, long start, long end) {
    if (sel_start)
        *sel_start = start;
    if (sel_end)
        *sel_end = end;
}

bool undo_undo(UndoStack *stack, const UndoTarget *target, void *ctx, long *sel_start,
               long *sel_end) {
    if (!undo_can_undo(stack) || !target || !target->insert || !target->remove)
        return false;

    const UndoStep *step = &stack->steps[stack->current - 1];
    stack->applying = true;
    // Newest first, so every op sees the document exactly as it left it
    for (size_t i = step->count; i-- > 0;) {
        const UndoOp *op = &step->ops[i];
        if (op->kind == UNDO_OP_INSERT) {
            target->remove(ctx, op->pos, op->count);
            set_selection(sel_start, sel_end, op->pos, op->pos);
        } else {
            target->insert(ctx, op->pos, op->text, op->len);
            set_selection(sel_start, sel_end, op->pos, op->pos + op->count);
        }
    }
    stack->applying = false;
    stack->current--;
    stack->merge = UNDO_MERGE_NONE;
    return true;
}

bool undo_redo(UndoStack *stack, const UndoTarget *target, void *ctx, long *sel_start,
               long *sel_end) {
    if (!undo_can_redo(stack) || !target || !target->insert || !target->remove)
        return false;

    const UndoStep *step = &stack->steps[stack->current];
    stack->applying = true;
    for (size_t i = 0; i < step->count; i++) {
        const UndoOp *op = &step->ops[i];
        if (op->kind == UNDO_OP_INSERT) {
            target->insert(ctx, op->pos, op->text, op->len);
            set_selection(sel_start, sel_end, op->pos + op->count, op->pos + op->count);
        } else {
            target->remove(ctx, op->pos, op->count);
            set_selection(sel_start, sel_end, op->pos, op->pos);
        }
    }
    stack->applying = false;
    stack->current++;
    stack->merge = UNDO_MERGE_NONE;
    return true;
}
