/*
 * npad - File type associations (Win32)
 *
 * Registers npad as an AVAILABLE editor for a set of file types - never as
 * the default one. Since Windows 8 the default handler lives in a
 * hash-protected UserChoice key that no application can write, and since 2024
 * a kernel filter driver enforces it and deny-lists reg.exe, regedit.exe and
 * powershell.exe as proxies. The honest surface is therefore: register, then
 * offer the Settings page where the user can choose.
 *
 * The key shapes here match installer/npad.iss exactly, with one deliberate
 * exception: nothing here ever writes an extension's default value (the act
 * of taking ownership of a file type). It overwrites whatever ProgID owned the
 * extension with no backup, Microsoft's own guidance says it must not be
 * removed on uninstall, and it loses to any existing UserChoice anyway.
 * Unregistering DOES remove that value when it names one of npad's ProgIDs,
 * so the pane can still undo what the installer did.
 *
 * Everything is applied as CHANGES, never as a whole state: only the groups
 * the user actually toggled are touched. A property sheet sends Apply to every
 * page that has been visited, so applying a whole state would rewrite
 * registrations whenever the tab had merely been looked at - repointing them
 * at whichever copy of npad happened to be running.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef ASSOC_WIN32_H
#define ASSOC_WIN32_H

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>

// Text, Markdown, Data, Config, Logs - the same five groups the installer's
// association tasks use, so the pane and the installer describe the world
// identically.
#define ASSOC_GROUP_COUNT 5

// Which groups are fully registered right now. A group counts as registered
// only when every extension in it is. out_groups must hold ASSOC_GROUP_COUNT.
void assoc_read_user_state(bool *out_groups);
void assoc_read_machine_state(bool *out_groups); // HKLM reads need no elevation

// Register the groups that are off in `before` and on in `after`, unregister
// the reverse, and leave every other group exactly as it is. Returns false if
// any write failed.
bool assoc_apply_user(const bool *before, const bool *after);

// Whether machine-wide registration is permitted: npad.exe must live under
// Program Files, which only administrators can write. A per-user install or a
// portable copy lives where other accounts cannot run it from - and where its
// owner could later replace it, which would turn a machine-wide open command
// into code run by every account that opens the file.
bool assoc_machine_available(void);

// Re-launch npad elevated to apply the same kind of change set to HKLM.
// Returns true without prompting when nothing changed. Returns false on
// failure, with *cancelled set when the user simply declined the prompt.
bool assoc_apply_machine(HWND owner, const bool *before, const bool *after, bool *cancelled);

// The elevated entry point: main() calls this before any UI exists and exits
// with its return value. It refuses (returns 2, writing nothing) unless npad
// is running from Program Files - the check is repeated here rather than
// trusted from the unelevated side.
int assoc_machine_cli(const wchar_t *changes);

// The change interpreter behind assoc_machine_cli, without the location
// check, so the tests can drive it against a sandbox. `changes` is a
// comma-separated list of `+group` (register) and `-group` (unregister)
// tokens; the group names are validated against the compiled-in table and any
// token that is not an exact match is ignored. An empty list changes nothing.
int assoc_machine_apply_changes(const wchar_t *changes);

// A /MERGETASKS="..." argument for npad's own installer that selects exactly
// the association tasks currently registered for the existing installation,
// so an update does not quietly re-apply the task ticks from the original
// install over changes made on this page. Returns false (and writes an empty
// string) when there is no existing Inno installation to match.
bool assoc_installer_merge_tasks(wchar_t *out, size_t cap);

// Windows Settings > Default apps, deep-linked to npad where that is supported
void assoc_open_default_apps(HWND owner);

#endif // ASSOC_WIN32_H
