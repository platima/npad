/*
 * npad - GTK3 UI: cross-instance messaging over D-Bus
 *
 * npad is one process per window, and a few things must reach every window
 * on the desktop: a preference saved in one applies live in all of them,
 * view sync mirrors zoom and font type, and Close All closes them all. On
 * Windows that is a registered window message posted to every window of
 * npad's class. Wayland gives a client no way to enumerate other clients'
 * windows, so here the same messages travel over the session bus instead.
 *
 * Each instance claims the well-known name <app id>.Instance.p<pid>. That
 * name is the instance registry: listing names with the prefix counts the
 * instances, and asking whether a pid's name has an owner says whether that
 * pid is a live npad - which also cannot be fooled by a recycled pid. The
 * bus drops a name the moment its process dies, crashes included, so nothing
 * goes stale.
 *
 * Messages are broadcast signals carrying the sender's pid; receivers ignore
 * their own, exactly as the Win32 receivers ignore a wParam equal to their
 * own process id. Signals are dispatched on the main loop, so handlers run
 * on the UI thread like everything else.
 *
 * GDBus is part of GLib, which GTK already requires: no extra dependency.
 * Without a session bus (a bare SSH session, a minimal window manager) every
 * call degrades to a no-op: no live sync, and a count of zero.
 *
 * The same connection also carries the desktop's light/dark preference: the
 * `system` colour scheme asks the xdg-desktop-portal Settings interface for
 * org.freedesktop.appearance color-scheme and follows its SettingChanged
 * signal (see "Desktop colour scheme" below).
 *
 * Author: Platima
 * https://npad.dev
 */

#define _DEFAULT_SOURCE // kill()

#include "ui_gtk.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define NPAD_DBUS_NAME_PREFIX NPAD_APP_ID ".Instance.p"
#define NPAD_DBUS_PATH "/io/github/platima/npad"
#define NPAD_DBUS_INTERFACE NPAD_APP_ID ".Instance"

#define FDO_NAME "org.freedesktop.DBus"
#define FDO_PATH "/org/freedesktop/DBus"
#define FDO_INTERFACE "org.freedesktop.DBus"

// RequestName flags and replies (D-Bus specification)
#define FDO_NAME_FLAG_DO_NOT_QUEUE 4u
#define FDO_REQUEST_NAME_REPLY_PRIMARY_OWNER 1u

#define DBUS_CALL_TIMEOUT_MS 1000

static GDBusConnection *g_bus = NULL;
static guint g_subscription = 0;
static guint g_portal_subscription = 0;
static char g_own_name[96] = "";

static void instance_name_for_pid(long pid, char *out, size_t size) {
    snprintf(out, size, "%s%ld", NPAD_DBUS_NAME_PREFIX, pid);
}

static void on_signal(GDBusConnection *connection, const gchar *sender_name,
                      const gchar *object_path, const gchar *interface_name,
                      const gchar *signal_name, GVariant *parameters, gpointer user_data) {
    (void) connection;
    (void) sender_name;
    (void) object_path;
    (void) interface_name;
    (void) user_data;

    // Every signal leads with the sender's pid
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE_TUPLE) ||
        g_variant_n_children(parameters) < 1)
        return;
    GVariant *first = g_variant_get_child_value(parameters, 0);
    if (!g_variant_is_of_type(first, G_VARIANT_TYPE_UINT32)) {
        g_variant_unref(first);
        return;
    }
    guint32 sender_pid = g_variant_get_uint32(first);
    g_variant_unref(first);
    if (sender_pid == (guint32) getpid())
        return; // Our own broadcast

    Window *window = g_gtk_main_window;
    if (!window)
        return;

    if (strcmp(signal_name, "SettingsChanged") == 0) {
        npad_gtk_reload_and_apply_settings(window);
    } else if (strcmp(signal_name, "ViewStateChanged") == 0) {
        if (g_variant_is_of_type(parameters, G_VARIANT_TYPE("(uib)"))) {
            guint32 pid = 0;
            gint32 zoom = 100;
            gboolean mono = FALSE;
            g_variant_get(parameters, "(uib)", &pid, &zoom, &mono);
            npad_gtk_apply_synced_view_state(window, zoom, mono != FALSE);
        }
    } else if (strcmp(signal_name, "CloseAll") == 0) {
        // Each window runs its own save-checked close, so a Cancel there
        // keeps that window open - the Win32 npadCloseAll behaviour
        npad_gtk_handle_command(window, NPAD_CMD_FILE_CLOSE);
    }
}

// Claim our instance name synchronously, so an instance started right after
// this one already counts it
static bool request_own_name(void) {
    instance_name_for_pid((long) getpid(), g_own_name, sizeof(g_own_name));
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        g_bus, FDO_NAME, FDO_PATH, FDO_INTERFACE, "RequestName",
        g_variant_new("(su)", g_own_name, FDO_NAME_FLAG_DO_NOT_QUEUE), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, DBUS_CALL_TIMEOUT_MS, NULL, &error);
    if (!reply) {
        g_warning("npad: could not claim %s: %s", g_own_name, error ? error->message : "unknown");
        g_clear_error(&error);
        g_own_name[0] = '\0';
        return false;
    }
    guint32 result = 0;
    g_variant_get(reply, "(u)", &result);
    g_variant_unref(reply);
    if (result != FDO_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        g_own_name[0] = '\0';
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Desktop colour scheme (xdg-desktop-portal)
// ---------------------------------------------------------------------------
//
// org.freedesktop.appearance color-scheme: 0 = no preference, 1 = prefer dark,
// 2 = prefer light. The value is read once, lazily - only a window whose scheme
// is `system` ever asks, so the default light scheme never touches the portal
// (which may need starting) - and then kept current by SettingChanged. The
// signal fires for every portal setting, so a change is only reported when the
// number differs (re-theming an idle window reflows its whole document:
// LESSONS.md, "Idle windows are not idle").

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_SETTINGS_INTERFACE "org.freedesktop.portal.Settings"
#define PORTAL_APPEARANCE_NS "org.freedesktop.appearance"
#define PORTAL_COLOR_SCHEME_KEY "color-scheme"

#define PORTAL_UNKNOWN (-1)
static int g_portal_scheme = PORTAL_UNKNOWN;

// The value arrives in a variant; the older Read() method wraps it in a second
// one. Unwrap to the uint32 and fold anything unexpected to "no preference".
static int portal_scheme_from_variant(GVariant *value) {
    GVariant *inner = value ? g_variant_ref(value) : NULL;
    while (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_VARIANT)) {
        GVariant *next = g_variant_get_variant(inner);
        g_variant_unref(inner);
        inner = next;
    }
    int scheme = 0;
    if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_UINT32)) {
        guint32 raw = g_variant_get_uint32(inner);
        scheme = raw == 1 ? 1 : raw == 2 ? 2 : 0;
    }
    if (inner)
        g_variant_unref(inner);
    return scheme;
}

static void on_portal_setting_changed(GDBusConnection *connection, const gchar *sender_name,
                                      const gchar *object_path, const gchar *interface_name,
                                      const gchar *signal_name, GVariant *parameters,
                                      gpointer user_data) {
    (void) connection;
    (void) sender_name;
    (void) object_path;
    (void) interface_name;
    (void) signal_name;
    (void) user_data;

    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(ssv)")))
        return;
    const gchar *ns = NULL;
    const gchar *key = NULL;
    GVariant *value = NULL;
    g_variant_get(parameters, "(&s&sv)", &ns, &key, &value);
    if (g_strcmp0(ns, PORTAL_APPEARANCE_NS) == 0 && g_strcmp0(key, PORTAL_COLOR_SCHEME_KEY) == 0) {
        int scheme = portal_scheme_from_variant(value);
        if (scheme != g_portal_scheme) {
            g_debug("npad: desktop colour scheme is now %d", scheme);
            g_portal_scheme = scheme;
            npad_gtk_system_scheme_changed();
        }
    }
    if (value)
        g_variant_unref(value);
}

static void portal_subscribe(void) {
    g_portal_subscription = g_dbus_connection_signal_subscribe(
        g_bus, PORTAL_NAME, PORTAL_SETTINGS_INTERFACE, "SettingChanged", PORTAL_PATH,
        PORTAL_APPEARANCE_NS, G_DBUS_SIGNAL_FLAGS_NONE, on_portal_setting_changed, NULL, NULL);
}

int npad_gtk_portal_color_scheme(void) {
    if (g_portal_scheme != PORTAL_UNKNOWN)
        return g_portal_scheme;

    g_portal_scheme = 0; // No portal, no bus or no answer: no preference
    if (!g_bus)
        return g_portal_scheme;

    // Synchronous so the window opens in the right colours instead of
    // flashing the wrong ones; a missing portal fails at once, a slow one is
    // cut off by the timeout
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        g_bus, PORTAL_NAME, PORTAL_PATH, PORTAL_SETTINGS_INTERFACE, "Read",
        g_variant_new("(ss)", PORTAL_APPEARANCE_NS, PORTAL_COLOR_SCHEME_KEY), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, DBUS_CALL_TIMEOUT_MS, NULL, &error);
    if (!reply) {
        g_clear_error(&error);
        return g_portal_scheme;
    }
    GVariant *value = g_variant_get_child_value(reply, 0);
    g_portal_scheme = portal_scheme_from_variant(value);
    g_variant_unref(value);
    g_variant_unref(reply);
    return g_portal_scheme;
}

bool npad_gtk_instance_init(void) {
    GError *error = NULL;
    g_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!g_bus) {
        // Not fatal: a desktop session always has a bus, and outside one npad
        // still edits files - it just cannot talk to other instances
        g_message("npad: no session bus (%s); cross-window sync is off",
                  error ? error->message : "unknown");
        g_clear_error(&error);
        return false;
    }

    request_own_name();

    g_subscription =
        g_dbus_connection_signal_subscribe(g_bus, NULL, NPAD_DBUS_INTERFACE, NULL, NPAD_DBUS_PATH,
                                           NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_signal, NULL, NULL);
    portal_subscribe();
    return true;
}

void npad_gtk_instance_cleanup(void) {
    if (!g_bus)
        return;
    if (g_subscription) {
        g_dbus_connection_signal_unsubscribe(g_bus, g_subscription);
        g_subscription = 0;
    }
    if (g_portal_subscription) {
        g_dbus_connection_signal_unsubscribe(g_bus, g_portal_subscription);
        g_portal_subscription = 0;
    }
    // Releasing the bus connection releases the name with it
    g_object_unref(g_bus);
    g_bus = NULL;
    g_own_name[0] = '\0';
}

static void broadcast(const char *member, GVariant *parameters) {
    if (!g_bus) {
        if (parameters)
            g_variant_unref(g_variant_ref_sink(parameters));
        return;
    }
    GError *error = NULL;
    if (!g_dbus_connection_emit_signal(g_bus, NULL, NPAD_DBUS_PATH, NPAD_DBUS_INTERFACE, member,
                                       parameters, &error)) {
        g_warning("npad: broadcast %s failed: %s", member, error ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }
    // A settings change is often followed straight away by this instance
    // exiting; flush so the message is not lost with the connection
    g_dbus_connection_flush_sync(g_bus, NULL, NULL);
}

void npad_gtk_instance_broadcast_settings_changed(void) {
    broadcast("SettingsChanged", g_variant_new("(u)", (guint32) getpid()));
}

void npad_gtk_instance_broadcast_view_state(int zoom, bool monospace) {
    broadcast("ViewStateChanged",
              g_variant_new("(uib)", (guint32) getpid(), (gint32) zoom, monospace ? TRUE : FALSE));
}

void npad_gtk_instance_broadcast_close_all(void) {
    broadcast("CloseAll", g_variant_new("(u)", (guint32) getpid()));
}

int npad_gtk_instance_count_others(void) {
    if (!g_bus)
        return 0;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        g_bus, FDO_NAME, FDO_PATH, FDO_INTERFACE, "ListNames", NULL, G_VARIANT_TYPE("(as)"),
        G_DBUS_CALL_FLAGS_NONE, DBUS_CALL_TIMEOUT_MS, NULL, &error);
    if (!reply) {
        g_clear_error(&error);
        return 0;
    }

    int count = 0;
    size_t prefix_len = strlen(NPAD_DBUS_NAME_PREFIX);
    GVariantIter *names = NULL;
    const gchar *name = NULL;
    g_variant_get(reply, "(as)", &names);
    while (g_variant_iter_loop(names, "&s", &name)) {
        if (strncmp(name, NPAD_DBUS_NAME_PREFIX, prefix_len) == 0 && strcmp(name, g_own_name) != 0)
            count++;
    }
    g_variant_iter_free(names);
    g_variant_unref(reply);
    return count;
}

// Fallback when the bus cannot answer: the pid exists and its command name
// is npad's. comm is the executable's basename, truncated to 15 characters,
// which keeps "npad-linux" intact.
static bool proc_is_npad(long pid) {
    if (kill((pid_t) pid, 0) != 0 && errno != EPERM)
        return false;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return true; // Alive, but unidentifiable: assume it is, the safe side
    char comm[32] = "";
    bool npad = fgets(comm, sizeof(comm), f) != NULL && strncmp(comm, "npad", 4) == 0;
    fclose(f);
    return npad;
}

bool npad_gtk_instance_pid_is_npad(long pid) {
    if (pid <= 0)
        return false;
    if (pid == (long) getpid())
        return true;
    if (g_bus) {
        char name[96];
        instance_name_for_pid(pid, name, sizeof(name));
        GError *error = NULL;
        GVariant *reply = g_dbus_connection_call_sync(
            g_bus, FDO_NAME, FDO_PATH, FDO_INTERFACE, "NameHasOwner", g_variant_new("(s)", name),
            G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, DBUS_CALL_TIMEOUT_MS, NULL, &error);
        if (reply) {
            gboolean owned = FALSE;
            g_variant_get(reply, "(b)", &owned);
            g_variant_unref(reply);
            if (owned)
                return true;
            // Not on this bus. It may still be an npad in another login
            // session (a different bus), so fall through to /proc rather than
            // declare its recovery slot abandoned.
        } else {
            g_clear_error(&error);
        }
    }
    return proc_is_npad(pid);
}
