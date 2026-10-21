/*
 * npad - Atomic save tests
 * The failure paths and identity preservation of file_write_binary_atomic:
 * a failed save must never destroy or truncate the existing file, must leave
 * no temp file behind, and a successful one must keep what the file was
 * (mode, owner, links, extended attributes).
 *
 * POSIX only (built and run by `make test` on Linux). Built with
 * -DFILE_OPS_FAULT_INJECTION so each stage of a save can be made to fail.
 *
 * Author: Platima
 * https://npad.dev
 */

#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "test_framework.h"
#include "../src/core/file_ops.h"
#include "../src/core/error.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif

#define ROOT "test_atomic_tmp"
#define BACKUPS ROOT "/backups"
#define TMPDIR_ROOT ROOT "/tmp"

static char g_backup_dir[PATH_MAX + 64];
static char g_tmpdir[PATH_MAX + 64];

// --- helpers -----------------------------------------------------------------

static void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        chmod(path, 0755); // A read-only folder from a test cannot be emptied
        DIR *d = opendir(path);
        if (d) {
            const struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                    continue;
                char child[PATH_MAX];
                snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
                remove_tree(child);
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

// A fresh, empty ROOT, with the backup folder the saves are pointed at and
// the folder TMPDIR names
static void reset_root(void) {
    file_ops_inject_fault(FILE_OPS_FAULT_NONE);
    file_ops_set_backup_dir(g_backup_dir);
    remove_tree(ROOT);
    mkdir(ROOT, 0755);
    mkdir(BACKUPS, 0755);
    mkdir(TMPDIR_ROOT, 0755);
}

static void put(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

// Whole file as a NUL-terminated string (caller frees), NULL if unreadable
static char *get(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while (buf && (n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (cap - len < 2) {
            cap *= 2;
            char *grown = realloc(buf, cap);
            if (!grown) {
                free(buf);
                buf = NULL;
            }
            buf = grown;
        }
    }
    fclose(f);
    if (buf)
        buf[len] = '\0';
    return buf;
}

static bool content_is(const char *path, const char *expected) {
    char *got = get(path);
    bool same = got && strcmp(got, expected) == 0;
    free(got);
    return same;
}

// Entries in dir other than . and .., optionally only those starting with prefix
static int count_entries(const char *dir, const char *prefix) {
    int n = 0;
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (!prefix || strncmp(e->d_name, prefix, strlen(prefix)) == 0)
            n++;
    }
    closedir(d);
    return n;
}

static bool no_temp_files(const char *dir) {
    return count_entries(dir, ".npad-") == 0;
}

static bool no_backups(void) {
    return count_entries(BACKUPS, NULL) <= 0 && count_entries(TMPDIR_ROOT, NULL) <= 0;
}

static mode_t mode_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (st.st_mode & 07777) : (mode_t) -1;
}

static ino_t inode_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? st.st_ino : 0;
}

// Permission tests mean nothing as root, who may write anywhere
static bool is_root(void) {
    return geteuid() == 0;
}

static bool save(const char *path, const char *content) {
    return file_write_text_atomic(path, content);
}

static void sleep_ms(long ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

// --- the ordinary save keeps the file's identity ----------------------------

TEST_CASE(new_file_gets_the_usual_mode) {
    reset_root();
    mode_t old = umask(022);
    bool ok = save(ROOT "/new.txt", "hello\n");
    umask(old);
    TEST_ASSERT(ok, "saving a new file succeeds");
    TEST_ASSERT(content_is(ROOT "/new.txt", "hello\n"), "with the content");
    TEST_ASSERT_EQ(0644, (int) mode_of(ROOT "/new.txt"), "and 0666 & ~umask, like any new file");
    TEST_ASSERT(no_temp_files(ROOT), "no temp file is left behind");
}

TEST_CASE(private_file_stays_private) {
    reset_root();
    put(ROOT "/secret.txt", "password=1\n");
    chmod(ROOT "/secret.txt", 0600);
    mode_t old = umask(022);
    bool ok = save(ROOT "/secret.txt", "password=2\n");
    umask(old);
    TEST_ASSERT(ok, "save succeeds");
    TEST_ASSERT(content_is(ROOT "/secret.txt", "password=2\n"), "new content");
    TEST_ASSERT_EQ(0600, (int) mode_of(ROOT "/secret.txt"), "a 0600 file stays 0600, not 0644");
    TEST_ASSERT(no_temp_files(ROOT), "no temp file left");
}

TEST_CASE(script_keeps_its_execute_bit) {
    reset_root();
    put(ROOT "/deploy.sh", "#!/bin/sh\necho 1\n");
    chmod(ROOT "/deploy.sh", 0755);
    TEST_ASSERT(save(ROOT "/deploy.sh", "#!/bin/sh\necho 2\n"), "save succeeds");
    TEST_ASSERT_EQ(0755, (int) mode_of(ROOT "/deploy.sh"), "0755 stays 0755");
}

// Save a file with set-id bits and check they survive. The first write by a
// process without CAP_FSETID clears S_ISUID/S_ISGID, so the identity has to
// be set after the data. Skipped when this filesystem or group membership
// refuses the bits in the first place.
static void check_setid_kept(mode_t mode, bool in_place) {
    reset_root();
    put(ROOT "/tool", "#!/bin/sh\necho 1\n");
    if (in_place && link(ROOT "/tool", ROOT "/tool2") != 0) {
        printf("(no hard links here, skipped) ");
        return;
    }
    if (chmod(ROOT "/tool", mode) != 0 || mode_of(ROOT "/tool") != mode) {
        printf("(mode %o refused here, skipped) ", (unsigned) mode);
        return;
    }
    TEST_ASSERT(save(ROOT "/tool", "#!/bin/sh\necho 2\n"), "save succeeds");
    TEST_ASSERT(content_is(ROOT "/tool", "#!/bin/sh\necho 2\n"), "with the new content");
    TEST_ASSERT_EQ((int) mode, (int) mode_of(ROOT "/tool"), "and the set-id bits are kept");
    TEST_ASSERT(no_temp_files(ROOT) && no_backups(), "nothing left behind");
}

TEST_CASE(setuid_file_keeps_its_mode) {
    check_setid_kept(04755, false);
}

TEST_CASE(setgid_file_keeps_its_mode) {
    check_setid_kept(02755, false);
}

TEST_CASE(setuid_file_saved_in_place_keeps_its_mode) {
    check_setid_kept(04755, true);
}

TEST_CASE(symlink_stays_a_link_and_target_gets_the_edit) {
    reset_root();
    mkdir(ROOT "/dotfiles", 0755);
    put(ROOT "/dotfiles/bashrc", "original\n");
    TEST_ASSERT(symlink("dotfiles/bashrc", ROOT "/link") == 0, "relative symlink created");
    TEST_ASSERT(save(ROOT "/link", "edited\n"), "save through the link succeeds");

    struct stat st;
    TEST_ASSERT(lstat(ROOT "/link", &st) == 0 && S_ISLNK(st.st_mode), "the link is still a link");
    TEST_ASSERT(content_is(ROOT "/dotfiles/bashrc", "edited\n"), "the target has the edit");
    TEST_ASSERT(no_temp_files(ROOT) && no_temp_files(ROOT "/dotfiles"), "no temp files");
}

TEST_CASE(symlink_chain_and_dangling_link) {
    reset_root();
    put(ROOT "/real.txt", "v1\n");
    TEST_ASSERT(symlink("real.txt", ROOT "/a") == 0 && symlink("a", ROOT "/b") == 0,
                "a two-link chain");
    TEST_ASSERT(save(ROOT "/b", "v2\n"), "save through the chain");
    TEST_ASSERT(content_is(ROOT "/real.txt", "v2\n"), "lands on the real file");

    TEST_ASSERT(symlink("made.txt", ROOT "/dangling") == 0, "dangling link");
    TEST_ASSERT(save(ROOT "/dangling", "new\n"), "save through a dangling link");
    struct stat st;
    TEST_ASSERT(lstat(ROOT "/dangling", &st) == 0 && S_ISLNK(st.st_mode), "stays a link");
    TEST_ASSERT(content_is(ROOT "/made.txt", "new\n"), "and creates its target");
}

TEST_CASE(hard_links_are_not_split) {
    reset_root();
    put(ROOT "/h1.txt", "original\n");
    TEST_ASSERT(link(ROOT "/h1.txt", ROOT "/h2.txt") == 0, "hard link created");
    ino_t before = inode_of(ROOT "/h1.txt");
    TEST_ASSERT(save(ROOT "/h2.txt", "edited via h2\n"), "save one name");
    TEST_ASSERT(content_is(ROOT "/h1.txt", "edited via h2\n"), "the other name sees the edit");
    TEST_ASSERT(inode_of(ROOT "/h1.txt") == before && inode_of(ROOT "/h2.txt") == before,
                "both are still the same file");
    struct stat st;
    TEST_ASSERT(stat(ROOT "/h1.txt", &st) == 0 && st.st_nlink == 2, "still two links");
    TEST_ASSERT(no_temp_files(ROOT) && no_backups(), "no temp or backup files left");
}

TEST_CASE(shorter_in_place_save_truncates_to_the_new_length) {
    reset_root();
    put(ROOT "/h1.txt", "a much longer original line\n");
    TEST_ASSERT(link(ROOT "/h1.txt", ROOT "/h2.txt") == 0, "hard link (forces in-place)");
    TEST_ASSERT(save(ROOT "/h1.txt", "short\n"), "save");
    TEST_ASSERT(content_is(ROOT "/h2.txt", "short\n"), "no tail of the old content remains");
}

TEST_CASE(extended_attributes_are_kept) {
#ifdef __linux__
    reset_root();
    put(ROOT "/tagged.txt", "x\n");
    if (setxattr(ROOT "/tagged.txt", "user.npad_test", "kept", 4, 0) != 0) {
        printf("(user xattrs unsupported here, skipped) ");
        return;
    }
    TEST_ASSERT(save(ROOT "/tagged.txt", "y\n"), "save");
    char value[16] = { 0 };
    ssize_t n = getxattr(ROOT "/tagged.txt", "user.npad_test", value, sizeof(value) - 1);
    TEST_ASSERT(n == 4 && strcmp(value, "kept") == 0, "the user xattr survives the save");
#endif
}

TEST_CASE(long_file_name_saves) {
    // The old "<name>.tmp.<seconds>" temp name pushed 241-255 byte names past
    // NAME_MAX, so a file that opened fine could never be saved
    reset_root();
    char path[512];
    char name[256];
    memset(name, 'n', 250);
    memcpy(name + 246, ".txt", 5); // 250 bytes in all
    snprintf(path, sizeof(path), "%s/%s", ROOT, name);
    put(path, "old\n");
    TEST_ASSERT(save(path, "new\n"), "a 250-byte name saves");
    TEST_ASSERT(content_is(path, "new\n"), "with the new content");
    TEST_ASSERT(no_temp_files(ROOT), "and no temp file is left");
}

// --- failures leave the original alone ---------------------------------------

TEST_CASE(read_only_file_is_refused_intact) {
    if (is_root()) {
        printf("(root, skipped) ");
        return;
    }
    reset_root();
    put(ROOT "/ro.txt", "original\n");
    chmod(ROOT "/ro.txt", 0444);
    TEST_ASSERT(!save(ROOT "/ro.txt", "new\n"), "saving a 0444 file fails");
    TEST_ASSERT(content_is(ROOT "/ro.txt", "original\n"), "the original is intact");
    TEST_ASSERT(no_temp_files(ROOT), "no temp file left");
}

TEST_CASE(read_only_folder_writable_file_is_saved_in_place) {
    if (is_root()) {
        printf("(root, skipped) ");
        return;
    }
    // A group-writable file in a web root, a Docker bind mount: the folder
    // takes no new files, so the save writes in place, as Notepad would
    reset_root();
    mkdir(ROOT "/locked", 0755);
    put(ROOT "/locked/f.txt", "original\n");
    ino_t before = inode_of(ROOT "/locked/f.txt");
    chmod(ROOT "/locked", 0555);
    bool ok = save(ROOT "/locked/f.txt", "edited in place\n");
    chmod(ROOT "/locked", 0755);
    TEST_ASSERT(ok, "the save succeeds");
    TEST_ASSERT(content_is(ROOT "/locked/f.txt", "edited in place\n"), "with the new content");
    TEST_ASSERT(inode_of(ROOT "/locked/f.txt") == before, "in the same file");
    TEST_ASSERT(no_temp_files(ROOT "/locked") && no_backups(), "and leaves nothing behind");
}

TEST_CASE(read_only_folder_new_file_fails_cleanly) {
    if (is_root()) {
        printf("(root, skipped) ");
        return;
    }
    reset_root();
    mkdir(ROOT "/locked", 0555);
    TEST_ASSERT(!save(ROOT "/locked/new.txt", "x\n"), "a new file there cannot be created");
    TEST_ASSERT(count_entries(ROOT "/locked", NULL) == 0, "and nothing appears");
}

TEST_CASE(folder_as_target_is_refused) {
    reset_root();
    mkdir(ROOT "/adir", 0755);
    TEST_ASSERT(!save(ROOT "/adir", "x\n"), "saving over a folder fails");
    struct stat st;
    TEST_ASSERT(stat(ROOT "/adir", &st) == 0 && S_ISDIR(st.st_mode), "the folder is untouched");
    TEST_ASSERT(no_temp_files(ROOT), "no temp file left");
}

// A real disk-full: RLIMIT_FSIZE makes writes past the limit fail with EFBIG
static bool with_file_size_limit(rlim_t limit, struct rlimit *saved) {
    if (getrlimit(RLIMIT_FSIZE, saved) != 0)
        return false;
    struct rlimit lim = *saved;
    lim.rlim_cur = limit;
    signal(SIGXFSZ, SIG_IGN); // Fail the write instead of killing the test
    return setrlimit(RLIMIT_FSIZE, &lim) == 0;
}

static char *big_text(size_t n) {
    char *s = malloc(n + 1);
    if (s) {
        memset(s, 'z', n);
        s[n] = '\0';
    }
    return s;
}

TEST_CASE(disk_full_keeps_the_original) {
    reset_root();
    put(ROOT "/full.txt", "original\n");
    char *big = big_text(64 * 1024);
    struct rlimit saved;
    TEST_ASSERT(big && with_file_size_limit(4096, &saved), "file size limit set");
    bool ok = save(ROOT "/full.txt", big);
    setrlimit(RLIMIT_FSIZE, &saved);
    free(big);
    TEST_ASSERT(!ok, "a save that does not fit fails");
    TEST_ASSERT(content_is(ROOT "/full.txt", "original\n"), "the original is intact");
    TEST_ASSERT(no_temp_files(ROOT), "and the partial temp file is removed");
}

TEST_CASE(disk_full_in_place_keeps_the_original) {
    reset_root();
    put(ROOT "/h1.txt", "original\n");
    TEST_ASSERT(link(ROOT "/h1.txt", ROOT "/h2.txt") == 0, "hard link (forces in-place)");
    char *big = big_text(64 * 1024);
    struct rlimit saved;
    TEST_ASSERT(big && with_file_size_limit(4096, &saved), "file size limit set");
    bool ok = save(ROOT "/h1.txt", big);
    setrlimit(RLIMIT_FSIZE, &saved);
    free(big);
    TEST_ASSERT(!ok, "the in-place save that does not fit fails");
    TEST_ASSERT(content_is(ROOT "/h1.txt", "original\n"), "the original is intact");
    TEST_ASSERT(no_backups(), "and the safety copy is cleaned up");
}

TEST_CASE(copy_over_a_file_is_atomic) {
    reset_root();
    put(ROOT "/dest.json", "{\"keep\":true}\n");
    char *big = big_text(64 * 1024);
    put(ROOT "/src.json", big ? big : "");
    free(big);
    struct rlimit saved;
    TEST_ASSERT(with_file_size_limit(4096, &saved), "file size limit set");
    bool ok = file_copy(ROOT "/src.json", ROOT "/dest.json");
    setrlimit(RLIMIT_FSIZE, &saved);
    TEST_ASSERT(!ok, "the copy fails");
    TEST_ASSERT(content_is(ROOT "/dest.json", "{\"keep\":true}\n"),
                "and the destination keeps its old content (no truncating write)");
}

// --- each stage failing, by injection -----------------------------------------

static void check_failed_stage(FileOpsFault fault, const char *label) {
    (void) label;
    reset_root();
    put(ROOT "/doc.txt", "original\n");
    file_ops_inject_fault(fault);
    bool ok = save(ROOT "/doc.txt", "new content\n");
    file_ops_inject_fault(FILE_OPS_FAULT_NONE);
    TEST_ASSERT(!ok, "the save reports failure");
    TEST_ASSERT(content_is(ROOT "/doc.txt", "original\n"), "the original is intact");
    TEST_ASSERT(no_temp_files(ROOT), "no temp file left");
    TEST_ASSERT(strlen(file_get_last_error()) > 0, "and there is a reason to show");
}

TEST_CASE(failed_temp_write_keeps_original) {
    check_failed_stage(FILE_OPS_FAULT_WRITE, "write");
}

TEST_CASE(failed_sync_keeps_original) {
    check_failed_stage(FILE_OPS_FAULT_SYNC, "sync");
}

TEST_CASE(failed_verify_keeps_original) {
    check_failed_stage(FILE_OPS_FAULT_VERIFY, "verify");
}

TEST_CASE(failed_rename_keeps_original) {
    check_failed_stage(FILE_OPS_FAULT_RENAME, "rename");
}

TEST_CASE(failure_reports_the_os_reason) {
    reset_root();
    put(ROOT "/doc.txt", "original\n");
    file_ops_inject_fault(FILE_OPS_FAULT_WRITE);
    TEST_ASSERT(!save(ROOT "/doc.txt", "x\n"), "save fails");
    TEST_ASSERT(strstr(file_get_last_error(), strerror(ENOSPC)) != NULL,
                "the message names the OS error (no space)");
}

TEST_CASE(temp_create_refused_falls_back_to_in_place) {
    reset_root();
    put(ROOT "/doc.txt", "original\n");
    ino_t before = inode_of(ROOT "/doc.txt");
    file_ops_inject_fault(FILE_OPS_FAULT_TEMP_CREATE);
    TEST_ASSERT(save(ROOT "/doc.txt", "new content\n"), "the save still succeeds");
    TEST_ASSERT(content_is(ROOT "/doc.txt", "new content\n"), "with the new content");
    TEST_ASSERT(inode_of(ROOT "/doc.txt") == before, "written in place");
    TEST_ASSERT(no_backups(), "safety copy removed afterwards");
}

TEST_CASE(busy_rename_falls_back_to_in_place) {
    // EBUSY is what renaming over a bind-mounted single file gives
    reset_root();
    put(ROOT "/doc.txt", "original\n");
    file_ops_inject_fault(FILE_OPS_FAULT_RENAME_BUSY);
    TEST_ASSERT(save(ROOT "/doc.txt", "new content\n"), "the save succeeds in place");
    TEST_ASSERT(content_is(ROOT "/doc.txt", "new content\n"), "with the new content");
    TEST_ASSERT(no_temp_files(ROOT) && no_backups(), "nothing left behind");
}

TEST_CASE(failed_in_place_write_restores_original) {
    reset_root();
    put(ROOT "/h1.txt", "original content\n");
    TEST_ASSERT(link(ROOT "/h1.txt", ROOT "/h2.txt") == 0, "hard link (forces in-place)");
    file_ops_inject_fault(FILE_OPS_FAULT_INPLACE_WRITE);
    TEST_ASSERT(!save(ROOT "/h1.txt", "xyz\n"), "the save fails part-way");
    TEST_ASSERT(content_is(ROOT "/h1.txt", "original content\n"),
                "the original is put back, length included");
    TEST_ASSERT(no_backups(), "and the safety copy is removed once it is");
}

// The path of the one npad-backup-* file in dir ("" if none)
static void find_backup(const char *dir, char *path, size_t size) {
    path[0] = '\0';
    DIR *d = opendir(dir);
    const struct dirent *e = NULL;
    while (d && (e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "npad-backup-", 12) == 0)
            snprintf(path, size, "%s/%s", dir, e->d_name);
    }
    if (d)
        closedir(d);
}

TEST_CASE(failed_restore_keeps_a_backup) {
    reset_root();
    put(ROOT "/h1.txt", "precious\n");
    TEST_ASSERT(link(ROOT "/h1.txt", ROOT "/h2.txt") == 0, "hard link (forces in-place)");
    file_ops_inject_fault(FILE_OPS_FAULT_INPLACE_WRITE);
    file_ops_inject_fault(FILE_OPS_FAULT_INPLACE_RESTORE);
    TEST_ASSERT(!save(ROOT "/h1.txt", "xyz\n"), "the save fails");
    file_ops_inject_fault(FILE_OPS_FAULT_NONE);
    TEST_ASSERT(count_entries(BACKUPS, "npad-backup-") == 1,
                "the safety copy is kept, in the backup folder");
    TEST_ASSERT(count_entries(TMPDIR_ROOT, NULL) == 0, "not in the temp folder");
    TEST_ASSERT(strstr(file_get_last_error(), "npad-backup-") != NULL,
                "and the message says where it is");

    char path[PATH_MAX];
    find_backup(BACKUPS, path, sizeof(path));
    TEST_ASSERT(content_is(path, "precious\n"), "holding the original content");
    TEST_ASSERT_EQ(0600, (int) mode_of(path), "readable by its owner only");
}

// An in-place save of h1.txt that fails and cannot put the original back, so
// its safety copy stays where it was made
static bool failed_in_place_save(void) {
    put(ROOT "/h1.txt", "precious\n");
    if (link(ROOT "/h1.txt", ROOT "/h2.txt") != 0) // Forces in-place
        return false;
    file_ops_inject_fault(FILE_OPS_FAULT_INPLACE_WRITE);
    file_ops_inject_fault(FILE_OPS_FAULT_INPLACE_RESTORE);
    bool ok = save(ROOT "/h1.txt", "xyz\n");
    file_ops_inject_fault(FILE_OPS_FAULT_NONE);
    return !ok;
}

TEST_CASE(backup_folder_is_created_when_missing) {
    // The recovery folder may not exist yet when the first in-place save runs
    reset_root();
    remove_tree(BACKUPS);
    TEST_ASSERT(failed_in_place_save(), "the save fails");
    TEST_ASSERT(count_entries(BACKUPS, "npad-backup-") == 1, "the copy is in the new folder");
    TEST_ASSERT_EQ(0700, (int) mode_of(BACKUPS), "which only its owner can enter");
}

TEST_CASE(unusable_backup_folder_falls_back_to_tmpdir) {
    reset_root();
    remove_tree(BACKUPS);
    put(BACKUPS, "a file where the folder should be\n");
    TEST_ASSERT(failed_in_place_save(), "the save fails");
    char path[PATH_MAX];
    find_backup(TMPDIR_ROOT, path, sizeof(path));
    TEST_ASSERT(content_is(path, "precious\n"), "the copy went to TMPDIR instead");

    // And with no backup folder set, an in-place save still works
    reset_root();
    file_ops_set_backup_dir(NULL);
    put(ROOT "/h1.txt", "one\n");
    TEST_ASSERT(link(ROOT "/h1.txt", ROOT "/h2.txt") == 0, "hard link (forces in-place)");
    TEST_ASSERT(save(ROOT "/h1.txt", "two\n"), "the save succeeds");
    TEST_ASSERT(content_is(ROOT "/h2.txt", "two\n"), "with the new content");
    TEST_ASSERT(no_backups(), "and cleans up");
    file_ops_set_backup_dir(g_backup_dir);
}

// --- concurrent instances ----------------------------------------------------

TEST_CASE(instances_saving_at_once_do_not_collide) {
    // The old temp name was "<file>.tmp.<seconds>": windows closing in the
    // same second truncated and removed each other's temp files
    reset_root();
    put(ROOT "/settings.json", "{}\n");
    const int kids = 4, rounds = 40;
    for (int k = 0; k < kids; k++) {
        pid_t pid = fork();
        if (pid == 0) {
            int failed = 0;
            char text[64];
            for (int r = 0; r < rounds; r++) {
                snprintf(text, sizeof(text), "{\"writer\":%d,\"round\":%d}\n", k, r);
                if (!save(ROOT "/settings.json", text))
                    failed++;
            }
            _exit(failed > 0 ? 1 : 0);
        }
    }
    int bad = 0;
    for (int k = 0; k < kids; k++) {
        int status = 0;
        if (wait(&status) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            bad++;
    }
    TEST_ASSERT_EQ(0, bad, "every save from every instance succeeds");
    char *final_text = get(ROOT "/settings.json");
    TEST_ASSERT(final_text && strncmp(final_text, "{\"writer\":", 10) == 0,
                "the file holds one whole save");
    free(final_text);
    TEST_ASSERT(no_temp_files(ROOT), "and no temp files remain");
}

// --- reading: folders, sizes, stamps, error reasons ---------------------------

TEST_CASE(opening_a_folder_says_so) {
    reset_root();
    mkdir(ROOT "/adir", 0755);
    TEST_ASSERT(file_is_directory(ROOT "/adir"), "file_is_directory sees it");
    TEST_ASSERT(!file_is_directory(ROOT "/missing"), "and not a missing path");
    TEST_ASSERT_EQ(0, (long) file_get_size(ROOT "/adir"), "a folder has no size (was 8 EB)");
    char *text = file_read_text(ROOT "/adir");
    TEST_ASSERT_NULL(text, "reading a folder fails");
    TEST_ASSERT(strstr(file_get_last_error(), "is a folder") != NULL,
                "saying it is a folder, not out of memory");
    TEST_ASSERT_NULL(file_read_text_limited(ROOT "/adir", 64), "the limited read too");
}

TEST_CASE(size_is_exact) {
    reset_root();
    put(ROOT "/s.txt", "12345");
    TEST_ASSERT_EQ(5, (long) file_get_size(ROOT "/s.txt"), "size from metadata");
    TEST_ASSERT_EQ(0, (long) file_get_size(ROOT "/missing"), "missing file is 0");
}

TEST_CASE(same_size_rewrite_changes_the_stamp) {
    reset_root();
    put(ROOT "/w.txt", "aaaa\n");
    FileStamp before, after;
    TEST_ASSERT(file_get_stamp(ROOT "/w.txt", &before), "stamp");

    // Replaced by rename at once (as sed -i or another editor does): a new
    // inode even if the clock did not move
    TEST_ASSERT(save(ROOT "/w.txt", "bbbb\n"), "same-size replace");
    TEST_ASSERT(file_get_stamp(ROOT "/w.txt", &after), "stamp again");
    TEST_ASSERT(!file_stamp_equal(&before, &after), "a same-size replacement is noticed");

    // Rewritten in place, same size, a moment later: sub-second mtime
    before = after;
    sleep_ms(20);
    FILE *f = fopen(ROOT "/w.txt", "r+b");
    TEST_ASSERT_NOT_NULL(f, "open in place");
    if (f) {
        fputs("cccc\n", f);
        fclose(f);
    }
    TEST_ASSERT(file_get_stamp(ROOT "/w.txt", &after), "stamp after in-place edit");
    TEST_ASSERT(!file_stamp_equal(&before, &after),
                "an in-place same-size edit within the second is noticed");
}

TEST_CASE(open_error_names_the_real_reason_without_stderr) {
    // A GUI npad has no stderr; logging the failure to it used to replace
    // errno with EBADF, so every message said "Bad file descriptor"
    reset_root();
    fflush(stderr);
    int saved = dup(2);
    close(2);
    char *text = file_read_text(ROOT "/does-not-exist.txt");
    const char *msg = file_get_last_error();
    bool names_enoent = strstr(msg, strerror(ENOENT)) != NULL;
    bool names_ebadf = strstr(msg, strerror(EBADF)) != NULL;
    dup2(saved, 2);
    close(saved);
    TEST_ASSERT_NULL(text, "the read fails");
    TEST_ASSERT(names_enoent, "the message says no such file");
    TEST_ASSERT(!names_ebadf, "not bad file descriptor");
}

TEST_CASE(delete_removes_files_not_folders) {
    reset_root();
    put(ROOT "/d.txt", "x");
    mkdir(ROOT "/emptydir", 0755);
    TEST_ASSERT(file_delete(ROOT "/d.txt"), "a file is deleted");
    TEST_ASSERT(access(ROOT "/d.txt", F_OK) != 0, "and gone");
    TEST_ASSERT(!file_delete(ROOT "/emptydir"), "a folder is not");
    TEST_ASSERT(access(ROOT "/emptydir", F_OK) == 0, "and stays");
}

int main(void) {
    npad_error_init();
    TEST_INIT();

    // Safety copies of in-place saves go to the backup folder, or $TMPDIR
    // without one: point both somewhere the tests can inspect and clean,
    // never the real /tmp
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("getcwd failed\n");
        return 1;
    }
    snprintf(g_backup_dir, sizeof(g_backup_dir), "%s/%s", cwd, BACKUPS);
    snprintf(g_tmpdir, sizeof(g_tmpdir), "%s/%s", cwd, TMPDIR_ROOT);
    setenv("TMPDIR", g_tmpdir, 1);
    reset_root();

    RUN_TEST(new_file_gets_the_usual_mode);
    RUN_TEST(private_file_stays_private);
    RUN_TEST(script_keeps_its_execute_bit);
    RUN_TEST(setuid_file_keeps_its_mode);
    RUN_TEST(setgid_file_keeps_its_mode);
    RUN_TEST(setuid_file_saved_in_place_keeps_its_mode);
    RUN_TEST(symlink_stays_a_link_and_target_gets_the_edit);
    RUN_TEST(symlink_chain_and_dangling_link);
    RUN_TEST(hard_links_are_not_split);
    RUN_TEST(shorter_in_place_save_truncates_to_the_new_length);
    RUN_TEST(extended_attributes_are_kept);
    RUN_TEST(long_file_name_saves);
    RUN_TEST(read_only_file_is_refused_intact);
    RUN_TEST(read_only_folder_writable_file_is_saved_in_place);
    RUN_TEST(read_only_folder_new_file_fails_cleanly);
    RUN_TEST(folder_as_target_is_refused);
    RUN_TEST(disk_full_keeps_the_original);
    RUN_TEST(disk_full_in_place_keeps_the_original);
    RUN_TEST(copy_over_a_file_is_atomic);
    RUN_TEST(failed_temp_write_keeps_original);
    RUN_TEST(failed_sync_keeps_original);
    RUN_TEST(failed_verify_keeps_original);
    RUN_TEST(failed_rename_keeps_original);
    RUN_TEST(failure_reports_the_os_reason);
    RUN_TEST(temp_create_refused_falls_back_to_in_place);
    RUN_TEST(busy_rename_falls_back_to_in_place);
    RUN_TEST(failed_in_place_write_restores_original);
    RUN_TEST(failed_restore_keeps_a_backup);
    RUN_TEST(backup_folder_is_created_when_missing);
    RUN_TEST(unusable_backup_folder_falls_back_to_tmpdir);
    RUN_TEST(instances_saving_at_once_do_not_collide);
    RUN_TEST(opening_a_folder_says_so);
    RUN_TEST(size_is_exact);
    RUN_TEST(same_size_rewrite_changes_the_stamp);
    RUN_TEST(open_error_names_the_real_reason_without_stderr);
    RUN_TEST(delete_removes_files_not_folders);

    remove_tree(ROOT);
    npad_error_cleanup();
    TEST_SUMMARY();
    return 0;
}
