/*
 * dbus-daemon-proxy.c - Source for dbus-daemon-proxy
 * Copyright (C) 2010 Collabora Ltd.
 *   @author: Alban Crequy <alban.crequy@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

static GMainLoop *mainloop = NULL;

/* the address that we are listening for D-Bus connections on */
gchar *dbus_srv_addr = "tcp:host=localhost,bind=*,port=8082,family=ipv4";

/* the server that's listening on dbus_srv_addr */
static DBusServer *dbus_srv;

/* the connection to dbus_srv from a local client, or NULL */
static DBusConnection *dbus_conn;

/* our unique D-Bus name on the real bus */
const gchar *dbus_local_name;

DBusGConnection *master_conn;

static DBusHandlerResult
filter_cb (DBusConnection *conn,
           DBusMessage *msg,
           void *user_data)
{
  gchar *marshalled = NULL;
  gint len;
  guint32 serial;

  g_debug ("New message from client: type='%d' path='%s' iface='%s' member='%s'",
      dbus_message_get_type (msg),
      dbus_message_get_path (msg),
      dbus_message_get_interface (msg),
      dbus_message_get_member (msg));

  if (dbus_message_get_type (msg) == DBUS_MESSAGE_TYPE_METHOD_CALL &&
      strcmp (dbus_message_get_path (msg),
        "/org/freedesktop/DBus") == 0 &&
      strcmp (dbus_message_get_interface (msg),
        "org.freedesktop.DBus") == 0 &&
      strcmp (dbus_message_get_destination (msg),
        "org.freedesktop.DBus") == 0 &&
      strcmp (dbus_message_get_member (msg), "Hello") == 0)
  {
    DBusMessage *welcome;

    g_debug ("Hello received");

    welcome = dbus_message_new_method_return (msg);
    if (!dbus_message_append_args (welcome,
          DBUS_TYPE_STRING, &dbus_local_name,
          DBUS_TYPE_INVALID))
    {
      g_error ("Cannot reply to Hello message");
      exit (1);
    }
    dbus_connection_send (conn, welcome, &serial);

    goto out;
  }

  if (dbus_message_get_type (msg) == DBUS_MESSAGE_TYPE_SIGNAL &&
      strcmp (dbus_message_get_interface (msg),
        "org.freedesktop.DBus.Local") == 0 &&
      strcmp (dbus_message_get_member (msg), "Disconnected") == 0)
  {
    /* connection was disconnected */
    g_debug ("connection was disconnected");
    dbus_connection_close (dbus_conn);
    dbus_connection_unref (dbus_conn);
    dbus_conn = NULL;
    goto out;
  }

  if (!dbus_message_marshal (msg, &marshalled, &len))
    goto out;

  dbus_connection_send (dbus_g_connection_get_connection (master_conn), msg,
      &serial);

out:
  if (marshalled != NULL)
    g_free (marshalled);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
master_filter_cb (DBusConnection *conn,
                  DBusMessage *msg,
                  void *user_data)
{
  gchar *marshalled = NULL;
  gint len;
  guint32 serial;

  if (!dbus_conn)
    return;

  g_debug ("New message from server: type='%d' path='%s' iface='%s' member='%s'",
      dbus_message_get_type (msg),
      dbus_message_get_path (msg),
      dbus_message_get_interface (msg),
      dbus_message_get_member (msg));

  if (!dbus_message_marshal (msg, &marshalled, &len))
    goto out;

  dbus_connection_send (dbus_conn, msg, &serial);

out:
  if (marshalled != NULL)
    g_free (marshalled);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static dbus_bool_t
allow_all_connections (DBusConnection *conn,
                       unsigned long uid,
                       void *data)
{
  g_debug ("allow_all_connections: Called");
  return TRUE;
}


static void
new_connection_cb (DBusServer *server,
                   DBusConnection *conn,
                   void *data)
{
  if (dbus_conn != NULL)
  {
    g_error ("Already connect, reject new connection");
    return;
  }

  g_debug ("New connection");

  dbus_connection_ref (conn);
  dbus_connection_setup_with_g_main (conn, NULL);
  dbus_connection_add_filter (conn, filter_cb, NULL, NULL);
  dbus_connection_set_unix_user_function (conn, allow_all_connections, NULL,
      NULL);
  dbus_connection_set_allow_anonymous (conn, TRUE);
  dbus_conn = conn;
}

void
start_bus ()
{
  DBusError error;

  dbus_error_init (&error);

  dbus_srv = dbus_server_listen (dbus_srv_addr, &error);
  if (dbus_srv == NULL)
  {
    g_error ("Cannot listen on '%s'", dbus_srv_addr);
    exit (1);
  }

  dbus_server_set_new_connection_function (dbus_srv,
      new_connection_cb,
      NULL, NULL);
  dbus_server_setup_with_g_main (dbus_srv, NULL);
}

int
main ()
{
  GError *error = NULL;

  g_type_init ();

  master_conn = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (!master_conn)
  {
    g_error ("Failed to open connection to session bus: %s\n",
        error->message);
    g_clear_error(&error);
    return 1;
  }

  dbus_local_name = dbus_bus_get_unique_name
    (dbus_g_connection_get_connection (master_conn));

  g_debug ("Unique name: '%s'", dbus_local_name);

  dbus_connection_add_filter (dbus_g_connection_get_connection (master_conn),
      master_filter_cb, NULL, NULL);

  start_bus ();

  mainloop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (mainloop);

  return 0;
}

