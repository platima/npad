/*
 * npad - GTK3 UI: Check for Updates (notify only)
 *
 * The Linux counterpart of the Win32 updater, minus the installing: it asks
 * GitHub for the latest release and, when there is a newer one, says how to
 * update the way this copy was installed - the system's package manager for a
 * repository install, the repository or a download for a package installed by
 * hand, the release page for a portable copy. It never downloads or runs
 * anything. The settings keys, defaults and surfacing modes are the Windows
 * ones (all off by default); "auto" has no meaning here and behaves as
 * "prompt".
 *
 * Nothing blocks the UI. The release lookup and the install-source detection
 * (reading the repo files, asking dpkg / rpm / pacman who owns the executable)
 * run in a GTask worker; only the main thread touches settings or widgets.
 * There is no HTTP library: HTTP/1.1 over GIO's TLS, which needs
 * glib-networking at run time - without it the check fails with a message
 * naming that package rather than any new link-time dependency.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "ui_gtk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "../core/settings.h"
#include "../core/update_check.h"
#include "../main.h"

#define UPDATE_HOST "api.github.com"
#define UPDATE_PATH "/repos/platima/npad/releases/latest"
// A pre-release build reads the newest few releases instead: /releases/latest
// never names a pre-release, so an rc tester was never told of the next rc
#define UPDATE_LIST_PATH "/repos/platima/npad/releases?per_page=10"
#define RELEASE_TAG_URL "https://github.com/platima/npad/releases/tag/"
#define UPDATE_MAX_BYTES (1024 * 1024)
#define UPDATE_LIST_MAX_BYTES (4 * 1024 * 1024)
#define UPDATE_TIMEOUT_SECONDS 20

// One job for the worker: a release lookup, a source detection, or both
typedef struct {
    bool want_check;
    bool want_source;

    bool ok;
    char tag[32];
    char error[320];

    UpdateInstallSource source;
} UpdateJob;

// Main-thread state. One document window per process, so one of these.
static struct {
    bool busy;      // A release lookup is running
    bool manual;    // ... and the user asked for it (report loudly)
    bool detecting; // A source detection is running
    bool source_known;
    UpdateInstallSource source;
    UpdateJob *held_check; // A finished check waiting for the detection (g_free)
} U;

void npad_gtk_update_current_version(char *out, size_t cap) {
    update_current_version(NPAD_VERSION, NPAD_VERSION_MAJOR, NPAD_VERSION_MINOR, NPAD_VERSION_PATCH,
                           out, cap);
}

bool npad_gtk_update_busy(void) {
    return U.busy;
}

// ---------------------------------------------------------------------------
// Worker: the release lookup
// ---------------------------------------------------------------------------

static void fetch_latest(UpdateJob *job) {
    GTlsBackend *tls = g_tls_backend_get_default();
    if (!tls || !g_tls_backend_supports_tls(tls)) {
        snprintf(job->error, sizeof(job->error),
                 "npad could not check for updates: this system's GLib has no TLS support. "
                 "Install the glib-networking package and try again.");
        return;
    }

    // Compile-time constants only, so safe on this worker thread
    char cur[32];
    npad_gtk_update_current_version(cur, sizeof(cur));
    bool prerelease = update_is_prerelease(cur);
    guint max_bytes = prerelease ? UPDATE_LIST_MAX_BYTES : UPDATE_MAX_BYTES;

    GError *error = NULL;
    GSocketClient *client = g_socket_client_new();
    g_socket_client_set_tls(client, TRUE); // Validates the certificate against the host
    g_socket_client_set_timeout(client, UPDATE_TIMEOUT_SECONDS);
    GSocketConnection *conn =
        g_socket_client_connect_to_host(client, UPDATE_HOST, 443, NULL, &error);
    GByteArray *data = NULL;
    if (conn) {
        char request[512];
        int request_len = snprintf(request, sizeof(request),
                                   "GET %s HTTP/1.1\r\n"
                                   "Host: " UPDATE_HOST "\r\n"
                                   "User-Agent: npad-updater\r\n"
                                   "Accept: application/vnd.github+json\r\n"
                                   "Connection: close\r\n"
                                   "\r\n",
                                   prerelease ? UPDATE_LIST_PATH : UPDATE_PATH);
        GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
        GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
        if (request_len > 0 && (size_t) request_len < sizeof(request) &&
            g_output_stream_write_all(out, request, (gsize) request_len, NULL, NULL, &error)) {
            data = g_byte_array_new();
            guint8 buf[8192];
            for (;;) {
                gssize n = g_input_stream_read(in, buf, sizeof(buf), NULL, &error);
                if (n <= 0)
                    break; // 0 = the server closed the connection: the response is complete
                if (data->len + (guint) n > max_bytes) {
                    g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE,
                                        "the response was too large");
                    break;
                }
                g_byte_array_append(data, buf, (guint) n);
            }
        }
        g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
        g_object_unref(conn);
    }
    g_object_unref(client);

    if (error) {
        snprintf(job->error, sizeof(job->error),
                 "Could not reach " UPDATE_HOST " to check for updates.\n\n%.200s", error->message);
        g_error_free(error);
        if (data)
            g_byte_array_free(data, TRUE);
        return;
    }

    int status = 0;
    char *body = NULL;
    if (!data) {
        // The request could not be formed or sent, without an error to show
        snprintf(job->error, sizeof(job->error), "Could not send the update check request.");
        return;
    }
    if (!update_http_parse_response((const char *) data->data, data->len, &status, &body, NULL)) {
        snprintf(job->error, sizeof(job->error), "Unexpected response from the update server.");
    } else if (status != 200) {
        update_http_status_message(status, job->error, sizeof(job->error));
    } else if (!update_pick_release_tag(body, prerelease, job->tag, sizeof(job->tag))) {
        snprintf(job->error, sizeof(job->error), "Unexpected response from the update server.");
    } else {
        job->ok = true;
    }
    free(body);
    g_byte_array_free(data, TRUE);
}

// ---------------------------------------------------------------------------
// Worker: how this copy was installed
// ---------------------------------------------------------------------------

static char *read_small_file(const char *path) {
    gchar *text = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &text, &len, NULL))
        return NULL;
    if (len > 256 * 1024) { // No repo file is this big; do not scan it
        g_free(text);
        return NULL;
    }
    return text;
}

static bool file_names_npad(const char *path) {
    char *text = read_small_file(path);
    bool found = update_repo_text_names_npad(text);
    g_free(text);
    return found;
}

// Whether a file in dir with this suffix points at pkg.npad.dev
static bool dir_names_npad(const char *dir, const char *suffix_a, const char *suffix_b) {
    GDir *d = g_dir_open(dir, 0, NULL);
    if (!d)
        return false;
    bool found = false;
    const char *name;
    while (!found && (name = g_dir_read_name(d)) != NULL) {
        if (!g_str_has_suffix(name, suffix_a) && !(suffix_b && g_str_has_suffix(name, suffix_b)))
            continue;
        char *path = g_build_filename(dir, name, NULL);
        found = file_names_npad(path);
        g_free(path);
    }
    g_dir_close(d);
    return found;
}

// g_spawn_sync's status is a wait status on Unix (see ui_gtk_prefs.c)
static bool exited_cleanly(gint status) {
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Ask one package manager whether it owns exe. Unprivileged, read-only
// queries; a tool that is not installed simply does not own it.
static bool query_owner(UpdatePackageOwner owner, const char *exe, char *pkg, size_t pkg_cap) {
    const char *tool = owner == UPDATE_OWNER_DPKG  ? "dpkg"
                       : owner == UPDATE_OWNER_RPM ? "rpm"
                                                   : "pacman";
    char *prog = g_find_program_in_path(tool);
    if (!prog)
        return false;
    char *argv[6] = { prog, NULL };
    if (owner == UPDATE_OWNER_DPKG) {
        argv[1] = "-S";
        argv[2] = (char *) exe;
    } else if (owner == UPDATE_OWNER_RPM) {
        argv[1] = "-qf";
        argv[2] = "--queryformat";
        argv[3] = "%{NAME}\\n";
        argv[4] = (char *) exe;
    } else {
        argv[1] = "-Qqo";
        argv[2] = (char *) exe;
    }
    // Untranslated output, whatever the desktop's language
    char **envp = g_environ_setenv(g_get_environ(), "LC_ALL", "C", TRUE);
    char *out = NULL;
    gint status = 0;
    bool owned = g_spawn_sync(NULL, argv, envp, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL,
                              &status, NULL) &&
                 exited_cleanly(status) && update_parse_owner_output(owner, out, pkg, pkg_cap);
    g_free(out);
    g_strfreev(envp);
    g_free(prog);
    return owned;
}

static UpdateInstallSource detect_source(void) {
    // Without the " (deleted)" a package upgrade under a running npad leaves
    char *exe = npad_gtk_self_exe_path();

    UpdateInstallFacts facts = { UPDATE_OWNER_NONE, NULL, false, false, false };
    char pkg[128] = "";
    if (update_exe_may_be_packaged(exe)) {
        static const UpdatePackageOwner order[] = { UPDATE_OWNER_DPKG, UPDATE_OWNER_RPM,
                                                    UPDATE_OWNER_PACMAN };
        for (size_t i = 0; i < G_N_ELEMENTS(order) && facts.owner == UPDATE_OWNER_NONE; i++) {
            if (query_owner(order[i], exe, pkg, sizeof(pkg))) {
                facts.owner = order[i];
                facts.package = pkg;
            }
        }
    }
    g_free(exe);

    // Only the repo files for the manager that owns it matter
    if (facts.owner == UPDATE_OWNER_DPKG) {
        facts.apt_repo = g_file_test("/etc/apt/sources.list.d/npad.sources", G_FILE_TEST_EXISTS) ||
                         file_names_npad("/etc/apt/sources.list") ||
                         dir_names_npad("/etc/apt/sources.list.d", ".list", ".sources");
    } else if (facts.owner == UPDATE_OWNER_RPM) {
        facts.dnf_repo = g_file_test("/etc/yum.repos.d/npad.repo", G_FILE_TEST_EXISTS) ||
                         dir_names_npad("/etc/yum.repos.d", ".repo", NULL);
    } else if (facts.owner == UPDATE_OWNER_PACMAN) {
        char *conf = read_small_file("/etc/pacman.conf");
        facts.pacman_repo = update_pacman_conf_has_npad(conf);
        g_free(conf);
    }
    return update_install_source(&facts);
}

static void worker(GTask *task, gpointer source_object, gpointer task_data,
                   GCancellable *cancellable) {
    (void) source_object;
    (void) cancellable;
    UpdateJob *job = task_data;
    if (job->want_source)
        job->source = detect_source();
    if (job->want_check)
        fetch_latest(job);
    g_task_return_boolean(task, TRUE);
}

static void job_done(GObject *source_object, GAsyncResult *result, gpointer data);

// Start the worker. A detection rides along with the first check, so the
// result dialog can already say how to update.
static void run_job(bool check) {
    UpdateJob *job = g_new0(UpdateJob, 1);
    job->want_check = check;
    job->want_source = !U.source_known && !U.detecting;
    if (job->want_source)
        U.detecting = true;
    if (check)
        U.busy = true;
    GTask *task = g_task_new(NULL, NULL, job_done, NULL);
    g_task_set_task_data(task, job, g_free);
    g_task_run_in_thread(task, worker);
    g_object_unref(task);
}

// ---------------------------------------------------------------------------
// Main thread: results
// ---------------------------------------------------------------------------

const char *npad_gtk_update_advice(void) {
    if (U.source_known)
        return update_install_advice(U.source);
    if (!U.detecting)
        run_job(false);
    return NULL;
}

static void stamp_checked(void) {
    GDateTime *now = g_date_time_new_now_local();
    char *stamp = g_date_time_format(now, "%Y-%m-%d %H:%M");
    settings_set_string("update_last_checked", stamp ? stamp : "");
    g_free(stamp);
    g_date_time_unref(now);
}

static GtkWindow *dialog_parent(Window *window) {
    // Over Preferences when the check was started from there
    GtkWindow *modal = NULL;
    GList *toplevels = gtk_window_list_toplevels();
    for (GList *l = toplevels; l; l = l->next) {
        GtkWindow *w = l->data;
        if (gtk_window_get_modal(w) && gtk_widget_get_visible(GTK_WIDGET(w)) && window &&
            gtk_window_get_transient_for(w) == GTK_WINDOW(window->window))
            modal = w;
    }
    g_list_free(toplevels);
    if (modal)
        return modal;
    return window ? GTK_WINDOW(window->window) : NULL;
}

static void show_simple(Window *window, GtkMessageType type, const char *primary,
                        const char *secondary) {
    GtkWidget *dialog = gtk_message_dialog_new(dialog_parent(window),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               type, GTK_BUTTONS_OK, "%s", primary);
    if (secondary)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
    gtk_window_set_title(GTK_WINDOW(dialog), "npad");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

enum { RESPONSE_NOTES = 1, RESPONSE_SKIP = 2 };

// "A newer version is available": how to update, the release notes, Skip.
// There is deliberately no install button.
static void show_available(Window *window, const char *tag) {
    char cur[32];
    npad_gtk_update_current_version(cur, sizeof(cur));
    const char *advice = update_install_advice(U.source_known ? U.source : UPDATE_SOURCE_PORTABLE);
    const char *nl = strrchr(advice, '\n');
    char *prose = nl ? g_strndup(advice, (gsize) (nl - advice)) : g_strdup(advice);
    const char *command = nl ? nl + 1 : "";

    GtkWidget *dialog = gtk_message_dialog_new(dialog_parent(window),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_INFO, GTK_BUTTONS_NONE, NULL);
    // GTK only makes the heading bold when there is secondary text
    gtk_message_dialog_set_markup(
        GTK_MESSAGE_DIALOG(dialog),
        "<span weight=\"bold\" size=\"larger\">A newer version of npad is available</span>");
    gtk_window_set_title(GTK_WINDOW(dialog), "npad");

    // The text in labels of our own rather than the secondary text, which GTK
    // centres as a block, so the command (or link) lines up under the prose.
    // The command is selectable so it can be copied.
    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    char *text = g_strdup_printf("npad %s is available (you have %s).\n\n%s", tag, cur, prose);
    GtkWidget *body = gtk_label_new(text);
    g_free(text);
    gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(body), 56);
    gtk_label_set_xalign(GTK_LABEL(body), 0.0f);
    gtk_box_pack_start(GTK_BOX(area), body, FALSE, FALSE, 0);

    GtkWidget *label = gtk_label_new(NULL);
    char *markup = g_str_has_prefix(command, "https://")
                       ? g_markup_printf_escaped("<a href=\"%s\">%s</a>", command, command)
                       : g_markup_printf_escaped("<tt>%s</tt>", command);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 0);
    gtk_widget_show(body);
    gtk_widget_show(label);

    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_View the Release Notes", RESPONSE_NOTES,
                           "_Skip This Version", RESPONSE_SKIP, "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(prose);

    if (response == RESPONSE_NOTES) {
        // This release's own notes: /releases/latest is never a pre-release
        char *notes = g_strconcat(RELEASE_TAG_URL, tag, NULL);
        GError *error = NULL;
        if (!g_app_info_launch_default_for_uri(notes, NULL, &error)) {
            show_simple(window, GTK_MESSAGE_WARNING, "Could not open the release notes", notes);
            g_clear_error(&error);
        }
        g_free(notes);
    } else if (response == RESPONSE_SKIP) {
        settings_set_string("update_skipped_version", tag);
        settings_save();
        ui_notify_settings_changed();
    }
    npad_gtk_update_apply_indicator(window);
    npad_gtk_prefs_updates_refresh();
}

static void handle_check(Window *window, const UpdateJob *job) {
    bool manual = U.manual;
    U.busy = false;
    U.manual = false;

    if (!job->ok) {
        npad_gtk_set_status_message(window, "Update check failed");
        npad_gtk_prefs_updates_refresh();
        // Silent for the launch check; loud only when the user asked
        if (manual)
            show_simple(window, GTK_MESSAGE_WARNING, "Update check failed", job->error);
        return;
    }

    // Record the result for the Updates page and the Help indicator; the other
    // windows pick it up from the broadcast
    stamp_checked();
    settings_set_string("update_latest_version", job->tag);
    settings_save();
    ui_notify_settings_changed();

    char cur[32];
    npad_gtk_update_current_version(cur, sizeof(cur));
    bool newer = update_version_compare(job->tag, cur) > 0;
    char *skipped = settings_get_string("update_skipped_version", "");
    char *mode = settings_get_string("update_mode", "off");
    bool surface = update_is_newer_unskipped(cur, job->tag, skipped);
    // "auto" (download and install) does not exist here: it prompts instead
    bool prompt_mode = strcmp(mode, "prompt") == 0 || strcmp(mode, "auto") == 0;
    free(skipped);
    free(mode);

    npad_gtk_update_apply_indicator(window);
    npad_gtk_prefs_updates_refresh();

    // A manual check re-surfaces even a skipped version (the user asked); the
    // launch check obeys the mode and the skip
    if (manual ? newer : (surface && prompt_mode)) {
        npad_gtk_set_status_message(window, NULL);
        show_available(window, job->tag);
    } else if (manual) {
        npad_gtk_set_status_message(window, "npad is up to date");
        char detail[160];
        snprintf(detail, sizeof(detail), "You have %s; the latest release is %s.", cur, job->tag);
        show_simple(window, GTK_MESSAGE_INFO, "npad is up to date", detail);
    } else if (surface) {
        npad_gtk_set_status_message(window, "An update is available (see the Help menu)");
    } else {
        npad_gtk_set_status_message(window, NULL);
    }
}

static void job_done(GObject *source_object, GAsyncResult *result, gpointer data) {
    (void) source_object;
    (void) data;
    const UpdateJob *job = g_task_get_task_data(G_TASK(result));
    if (job->want_source) {
        U.detecting = false;
        U.source_known = true;
        U.source = job->source;
        npad_gtk_prefs_updates_refresh();
        if (U.held_check) { // A check that finished first, waiting for this
            UpdateJob *held = U.held_check;
            U.held_check = NULL;
            handle_check(g_gtk_main_window, held);
            g_free(held);
        }
    }
    if (!job->want_check)
        return;
    if (!U.source_known) {
        // The detection runs in a job of its own (the Updates page started
        // it) and has not finished: the dialog would give the wrong advice
        U.held_check = g_new(UpdateJob, 1);
        *U.held_check = *job;
        return;
    }
    handle_check(g_gtk_main_window, job);
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

// A manual request while the launch check is still on its way joins it: that
// check's result is then reported as if just asked for. It used to be refused
// ("already running") and the launch check then finished silently.
bool npad_gtk_update_check(Window *window, bool manual) {
    if (!window)
        return false;
    if (U.busy) {
        if (manual) {
            U.manual = true;
            npad_gtk_set_status_message(window, "Checking for updates...");
        }
        return true;
    }
    U.manual = manual;
    npad_gtk_set_status_message(window, "Checking for updates...");
    run_job(true);
    npad_gtk_prefs_updates_refresh();
    return true;
}

void npad_gtk_update_command(Window *window) {
    if (!window)
        return;
    if (window->update_item_available)
        npad_gtk_show_preferences_updates(window);
    else if (!npad_gtk_update_check(window, true))
        npad_gtk_set_status_message(window, "An update check is already running");
}

void npad_gtk_update_launch_check(Window *window) {
    if (!window || !settings_get_bool("update_check_on_launch", false))
        return;
    char *mode = settings_get_string("update_mode", "off");
    bool on = strcmp(mode, "off") != 0;
    free(mode);
    // Windows checks from its primary window only. Each Linux window is its
    // own process, so the first one open stands in for it: opening a second
    // window does not check again.
    if (on && npad_gtk_instance_count_others() == 0)
        npad_gtk_update_check(window, false);
}

void npad_gtk_update_apply_indicator(Window *window) {
    if (!window || !window->help_item || !window->command_items[NPAD_CMD_HELP_CHECK_UPDATES])
        return;
    char cur[32];
    npad_gtk_update_current_version(cur, sizeof(cur));
    char *mode = settings_get_string("update_mode", "off");
    char *latest = settings_get_string("update_latest_version", "");
    char *skipped = settings_get_string("update_skipped_version", "");
    bool avail = strcmp(mode, "off") != 0 && update_is_newer_unskipped(cur, latest, skipped);

    // The Help title gains a dot while an update is waiting, and the one Help
    // item turns into a shortcut to the Updates page (as on Windows)
    gtk_menu_item_set_label(GTK_MENU_ITEM(window->help_item),
                            avail ? "_Help  \xE2\x97\x8F" : "_Help");
    char label[96];
    if (avail)
        snprintf(label, sizeof(label), "_Update Available (%s)...", latest);
    else
        snprintf(label, sizeof(label), "Check for _Updates...");
    gtk_menu_item_set_label(GTK_MENU_ITEM(window->command_items[NPAD_CMD_HELP_CHECK_UPDATES]),
                            label);
    window->update_item_available = avail;

    free(mode);
    free(latest);
    free(skipped);
}
