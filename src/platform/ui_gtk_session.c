/*
 * npad - GTK3 UI: the session ending under npad
 *
 * The Linux counterpart of Windows' WM_QUERYENDSESSION / WM_ENDSESSION: when
 * something other than the user closes npad, unsaved work is parked as a
 * handoff snapshot (restored silently on the next launch) instead of being
 * lost or stalling the logout behind a save prompt nobody will answer. GTK 3
 * has no session-management hook of its own outside GtkApplication, so each
 * way a session can end is caught here:
 *
 *  - Logout: npad registers with the desktop's session manager over D-Bus
 *    (org.gnome.SessionManager, which GNOME, Cinnamon, MATE and Budgie
 *    provide; Xfce's org.xfce.SessionManager speaks the same client protocol).
 *    QueryEndSession parks the work and consents, CancelEndSession drops the
 *    handoff mark again, EndSession and Stop quit.
 *  - Shutdown or reboot without a logout first: a logind "delay" inhibitor
 *    lock, so PrepareForShutdown arrives while there is still time to park.
 *  - SIGTERM (systemd stopping the session, `kill`), SIGHUP (the terminal npad
 *    was started from closed) and SIGINT (Ctrl+C there): park and quit. A
 *    SIGHUP or SIGINT that npad inherited as ignored (nohup, a background
 *    job of a script) stays ignored.
 *  - Losing the X display with no warning at all (the X server ending, a bare
 *    window manager's logout): GDK would _exit at once, so an Xlib IO error
 *    handler parks the work first and then lets GDK's handler exit.
 *    Wayland has no such hook; there the session manager is the safeguard.
 *
 * All of it is optional and asynchronous: without a session bus, a session
 * manager or logind, npad runs exactly as before.
 *
 * Author: Platima
 * https://npad.dev
 */

#define _DEFAULT_SOURCE // dlopen() / dlsym() declarations under -std=c99

#include "ui_gtk.h"

#include <dlfcn.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

// D-Bus calls the session end waits on must not hang npad's exit
#define NPAD_SESSION_REPLY_TIMEOUT_MS 2000

// A logout or shutdown has been announced (and not called off)
static bool g_session_ending = false;

bool npad_gtk_session_ending(void) {
    return g_session_ending;
}

// Park the unsaved document as a handoff snapshot, once per session end: a
// snapshot is a full text copy plus a verified write, and the end may give
// npad little time
static void park_once(void) {
    if (g_session_ending)
        return;
    g_session_ending = true;
    if (g_gtk_main_window)
        ui_post_event(UI_EVENT_HANDOFF_SNAPSHOT, g_gtk_main_window, NULL);
}

// The session is going: park (if that has not happened) and quit. Never a
// prompt, and a failed park is not a reason to stay - refusing would only get
// npad killed with nothing parked at all.
static void end_now(void) {
    park_once();
    ui_quit();
}

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------

static gboolean on_terminate_signal(gpointer data) {
    (void) data;
    // QUIT_HANDOFF parks the latest text (anything typed since a session end
    // was announced included) and quits; if it cannot park it, quit anyway
    g_session_ending = true;
    Window *window = g_gtk_main_window;
    if (!window || !ui_post_event(UI_EVENT_QUIT_HANDOFF, window, NULL))
        ui_quit();
    return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------------------
// The desktop's session manager (logout)
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;          // Well-known bus name
    const char *path;          // Manager object
    const char *manager_iface; // Has RegisterClient(s app_id, s startup_id) -> o
    const char *client_iface;  // On the client object: the end-of-session signals and
                               // EndSessionResponse(b is_ok, s reason)
} SessionManagerApi;

static const SessionManagerApi k_session_managers[] = {
    { "org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager",
      "org.gnome.SessionManager.ClientPrivate" },
    { "org.xfce.SessionManager", "/org/xfce/SessionManager", "org.xfce.Session.Manager",
      "org.xfce.Session.Client" },
};

static GDBusConnection *g_session_bus = NULL;
static const SessionManagerApi *g_manager = NULL; // The one that took the registration
static char *g_client_path = NULL;
static char *g_startup_id = NULL;

static void respond_to_manager(bool wait) {
    if (!g_session_bus || !g_manager || !g_client_path)
        return;
    GVariant *args = g_variant_new("(bs)", TRUE, "");
    if (!wait) {
        g_dbus_connection_call(g_session_bus, g_manager->name, g_client_path,
                               g_manager->client_iface, "EndSessionResponse", args, NULL,
                               G_DBUS_CALL_FLAGS_NONE, NPAD_SESSION_REPLY_TIMEOUT_MS, NULL, NULL,
                               NULL);
        return;
    }
    // Waited for: npad quits right after, and an answer still queued at exit
    // would never leave
    GVariant *reply = g_dbus_connection_call_sync(g_session_bus, g_manager->name, g_client_path,
                                                  g_manager->client_iface, "EndSessionResponse",
                                                  args, NULL, G_DBUS_CALL_FLAGS_NONE,
                                                  NPAD_SESSION_REPLY_TIMEOUT_MS, NULL, NULL);
    if (reply)
        g_variant_unref(reply);
}

static void on_client_signal(GDBusConnection *connection, const gchar *sender, const gchar *path,
                             const gchar *iface, const gchar *signal, GVariant *params,
                             gpointer data) {
    (void) connection;
    (void) sender;
    (void) path;
    (void) iface;
    (void) params;
    (void) data;
    if (strcmp(signal, "QueryEndSession") == 0) {
        // Park while there is still time, then consent. Never veto and never
        // prompt (Win32's WM_QUERYENDSESSION reasoning): a refusal would only
        // stall the logout, and the parked copy comes back on the next launch.
        park_once();
        respond_to_manager(false);
    } else if (strcmp(signal, "CancelEndSession") == 0) {
        // Called off: prompt on close again, and drop the handoff mark so a
        // genuine crash later is reported as one
        if (g_session_ending) {
            g_session_ending = false;
            if (g_gtk_main_window)
                ui_post_event(UI_EVENT_HANDOFF_CANCELLED, g_gtk_main_window, NULL);
        }
    } else if (strcmp(signal, "EndSession") == 0) {
        // Committed. A forced logout can skip QueryEndSession, so park here
        // if that never ran, answer, and go.
        park_once();
        respond_to_manager(true);
        ui_quit();
    } else if (strcmp(signal, "Stop") == 0) {
        end_now();
    }
}

static void on_registered(GObject *source, GAsyncResult *result, gpointer data);

static void register_with(size_t index) {
    if (index >= G_N_ELEMENTS(k_session_managers))
        return; // No session manager that speaks the protocol: signals still work
    const SessionManagerApi *api = &k_session_managers[index];
    g_dbus_connection_call(
        g_session_bus, api->name, api->path, api->manager_iface, "RegisterClient",
        g_variant_new("(ss)", NPAD_APP_ID, g_startup_id ? g_startup_id : ""), G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, on_registered, GSIZE_TO_POINTER(index));
}

static void on_registered(GObject *source, GAsyncResult *result, gpointer data) {
    size_t index = GPOINTER_TO_SIZE(data);
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (!reply) {
        g_clear_error(&error);
        register_with(index + 1); // Not this desktop's manager: try the next
        return;
    }
    g_manager = &k_session_managers[index];
    g_variant_get(reply, "(o)", &g_client_path);
    g_variant_unref(reply);
    g_dbus_connection_signal_subscribe(g_session_bus, g_manager->name, g_manager->client_iface,
                                       NULL, g_client_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                       on_client_signal, NULL, NULL);
}

static void on_session_bus(GObject *source, GAsyncResult *result, gpointer data) {
    (void) source;
    (void) data;
    g_session_bus = g_bus_get_finish(result, NULL);
    if (g_session_bus)
        register_with(0);
}

// ---------------------------------------------------------------------------
// logind (shutdown and reboot)
// ---------------------------------------------------------------------------

static GDBusConnection *g_system_bus = NULL;
static int g_inhibit_fd = -1; // Held until npad is ready for the shutdown

static void release_inhibitor(void) {
    if (g_inhibit_fd >= 0) {
        close(g_inhibit_fd);
        g_inhibit_fd = -1;
    }
}

static void on_prepare_for_shutdown(GDBusConnection *connection, const gchar *sender,
                                    const gchar *path, const gchar *iface, const gchar *signal,
                                    GVariant *params, gpointer data) {
    (void) connection;
    (void) sender;
    (void) path;
    (void) iface;
    (void) signal;
    (void) data;
    gboolean starting = FALSE;
    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(b)")))
        return;
    g_variant_get(params, "(b)", &starting);
    if (!starting)
        return;
    park_once();
    release_inhibitor(); // Parked: logind may go on
    ui_quit();
}

static void on_inhibited(GObject *source, GAsyncResult *result, gpointer data) {
    (void) data;
    GUnixFDList *fds = NULL;
    GVariant *reply = g_dbus_connection_call_with_unix_fd_list_finish(G_DBUS_CONNECTION(source),
                                                                      &fds, result, NULL);
    if (reply) {
        gint32 handle = -1;
        g_variant_get(reply, "(h)", &handle);
        if (fds && handle >= 0 && handle < g_unix_fd_list_get_length(fds))
            g_inhibit_fd = g_unix_fd_list_get(fds, handle, NULL); // Close-on-exec
        g_variant_unref(reply);
    }
    if (fds)
        g_object_unref(fds);
}

static void on_system_bus(GObject *source, GAsyncResult *result, gpointer data) {
    (void) source;
    (void) data;
    g_system_bus = g_bus_get_finish(result, NULL);
    if (!g_system_bus)
        return;
    g_dbus_connection_signal_subscribe(g_system_bus, "org.freedesktop.login1",
                                       "org.freedesktop.login1.Manager", "PrepareForShutdown",
                                       "/org/freedesktop/login1", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                       on_prepare_for_shutdown, NULL, NULL);
    // A delay lock, never a block: the shutdown waits only while npad parks
    // (and at most logind's InhibitDelayMaxSec)
    g_dbus_connection_call_with_unix_fd_list(
        g_system_bus, "org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", "Inhibit",
        g_variant_new("(ssss)", "shutdown", "npad", "Keeping unsaved documents for the next start",
                      "delay"),
        G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, NULL, on_inhibited, NULL);
}

// ---------------------------------------------------------------------------
// Losing the X display
// ---------------------------------------------------------------------------

// Xlib's handler types, declared here so npad does not link libX11 itself: the
// X11 backend has already loaded it, and dlsym finds it there
typedef int (*NpadXIOErrorHandler)(void *display);
typedef NpadXIOErrorHandler (*NpadXSetIOErrorHandler)(NpadXIOErrorHandler handler);

static NpadXIOErrorHandler g_gdk_io_error_handler = NULL;

// The X connection is gone. Xlib must not be called again, and nothing parking
// needs does call it: the text comes from the GtkTextBuffer in memory and the
// snapshot is plain file I/O. Then GDK's own handler reports it and exits.
static int on_x_io_error(void *display) {
    park_once();
    if (g_gdk_io_error_handler)
        return g_gdk_io_error_handler(display);
    _exit(1);
}

static void hook_x_io_errors(void) {
    GdkDisplay *display = gdk_display_get_default();
    if (!display || strcmp(G_OBJECT_TYPE_NAME(display), "GdkX11Display") != 0)
        return;
    void *self = dlopen(NULL, RTLD_LAZY);
    if (!self)
        return;
    NpadXSetIOErrorHandler set_handler = (NpadXSetIOErrorHandler) dlsym(self, "XSetIOErrorHandler");
    if (set_handler)
        g_gdk_io_error_handler = (NpadXIOErrorHandler) set_handler(on_x_io_error);
    // The handle stays open: closing it would not unload anything anyway
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

// SIGHUP and SIGINT come from the terminal npad was started from. A launch
// that set them to be ignored (`nohup npad &`, or `npad &` from a script,
// where POSIX shells ignore SIGINT for background commands) asked npad to
// outlive that terminal, so the inherited SIG_IGN stays in place.
static void add_signal_unless_ignored(int sig) {
    struct sigaction old;
    if (sigaction(sig, NULL, &old) == 0 && old.sa_handler == SIG_IGN)
        return;
    g_unix_signal_add(sig, on_terminate_signal, NULL);
}

void npad_gtk_session_init(void) {
    g_unix_signal_add(SIGTERM, on_terminate_signal, NULL);
    add_signal_unless_ignored(SIGHUP);
    add_signal_unless_ignored(SIGINT);

    hook_x_io_errors();

    // The session manager hands an autostarted client its id this way; it is
    // ours alone, so the windows npad starts must not inherit it
    const char *startup_id = g_getenv("DESKTOP_AUTOSTART_ID");
    if (startup_id && startup_id[0])
        g_startup_id = g_strdup(startup_id);
    g_unsetenv("DESKTOP_AUTOSTART_ID");

    g_bus_get(G_BUS_TYPE_SESSION, NULL, on_session_bus, NULL);
    g_bus_get(G_BUS_TYPE_SYSTEM, NULL, on_system_bus, NULL);
}
