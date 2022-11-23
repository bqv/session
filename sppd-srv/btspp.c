/*
 * Copyright (C) 2017 Canonical Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "profile1-iface.h"

#define SERIAL_PORT_PROFILE_UUID "00001101-0000-1000-8000-00805f9b34fb"

typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

struct sockaddr_rc {
	sa_family_t rc_family;
	bdaddr_t    rc_bdaddr;
	uint8_t     rc_channel;
};

struct spp_data {
	GMainLoop *loop;	
	int sock_fd;
	gboolean server;
	struct sockaddr_rc local;
	struct sockaddr_rc remote;	
};

int register_profile(struct spp_data *spp, GDBusProxy *proxy)
{
	GVariant *profile;
	GVariantBuilder profile_builder;
	GError *error = NULL;

	fprintf(stderr, "register_profile called!\n");
	
	g_variant_builder_init(&profile_builder, G_VARIANT_TYPE("(osa{sv})"));

	if (g_variant_is_object_path("/bluetooth/profile/serial_port")) {
		fprintf(stderr, "object path is good!\n");
	}
	
	g_variant_builder_add (&profile_builder, "o",
			"/bluetooth/profile/serial_port");

	g_variant_builder_add (&profile_builder, "s", SERIAL_PORT_PROFILE_UUID);

	g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("a{sv}"));

        // not specifying AutoConnect...
	
	g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));	
	g_variant_builder_add (&profile_builder, "s", "Channel");
	g_variant_builder_add (&profile_builder, "v", g_variant_new_uint16(22));
	g_variant_builder_close(&profile_builder);

	g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
	g_variant_builder_add (&profile_builder, "s", "Service");
	g_variant_builder_add (&profile_builder, "v",
			g_variant_new_string(SERIAL_PORT_PROFILE_UUID));
	g_variant_builder_close(&profile_builder);

	g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
	g_variant_builder_add (&profile_builder, "s", "Name");
	g_variant_builder_add (&profile_builder, "v",
			g_variant_new_string("Serial Port"));
	g_variant_builder_close(&profile_builder);

	g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
	g_variant_builder_add (&profile_builder, "s", "Role");

	if (spp->server) {
		g_variant_builder_add (&profile_builder, "v",
				g_variant_new_string("server"));
	} else {
		g_variant_builder_add (&profile_builder, "v",
				g_variant_new_string("client"));
	}
		
	g_variant_builder_close(&profile_builder);


	g_variant_builder_close(&profile_builder);
	profile = g_variant_builder_end(&profile_builder);

	g_dbus_proxy_call_sync (proxy,
				"RegisterProfile",
				profile,
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				&error);
	g_assert_no_error(error);

	return 0;
}

gboolean
finish (struct spp_data *spp) {
	// close connection
	close(spp->sock_fd);

	// all done!
	g_main_loop_quit(spp->loop);	

	// make this a one-shot
	return false;
}

char *
eval_loop (const char *text) {
	char console[4096] = { 0 };
	FILE *fp;

	if (!text)
		return NULL;

	struct passwd *pw = getpwuid(getuid());
	chdir(pw->pw_dir);

	fp = popen(text, "r");
	if (fp == NULL) {
		perror("eval: popen failed\n");
		return strdup("ERROR");
	}

	while (fgets(console + strlen(console), sizeof(console) - strlen(console), fp) != NULL);

	pclose(fp);

	printf("%s\n", console);

	return strndup(console, 4096);
}

int
write_loop (struct spp_data *spp, char *text) {
	int status;

	if (!text) {
		text = (char*)"";
	}

	// write data to the client
	status = write(spp->sock_fd, text, strlen(text));
	if (status < 0) {
		perror("client: write to socket failed!\n");
	}

	return status;
}

gboolean
client_write_data (gpointer user_data) {
	struct spp_data *spp = user_data;

	char *text = "Hello World!\n";

	fprintf(stderr, "client_write_data called\n");	
	
	write_loop(spp, text);

	fprintf(stderr, "client_write_data status OK!\n");		

	return finish(spp);
}

int
read_loop (struct spp_data *spp, char *buf, size_t bufsz) {
	int bytes_read;

	bytes_read = read(spp->sock_fd, buf, bufsz);

	if ( bytes_read > 0 ) {
		buf[bytes_read] = '\0';
		printf("$ %s\n", buf);
	} else if ( errno == 11 ) {
		// retry
	} else {
		fprintf(stderr, "error reading from client [%d] %s\n", errno, strerror(errno));
	}

	return true;
}

gboolean
server_read_data (gpointer user_data) {
	char buf[1024] = { 0 };
	int opts = 0;
	struct spp_data *spp = user_data;
	
	fprintf(stderr, "server_read_data called\n");

	// set socket for blocking IO
	fcntl(spp->sock_fd, F_SETFL, opts);
	opts = fcntl(spp->sock_fd, F_GETFL);
	if (opts < 0) {
		perror("fcntl(F_GETFL)");
		exit(EXIT_FAILURE);
	}

	//opts &= ~O_NONBLOCK;
	opts |= O_NONBLOCK;
	
	if (fcntl(spp->sock_fd, F_SETFL,opts) < 0) {
		perror("fcntl(F_SETFL)");
		exit(EXIT_FAILURE);
	}

	// read data from the client
	while (read_loop(spp, buf, sizeof(buf))) {
		char *result = NULL;

		for (char *line = strtok(buf, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
			result = eval_loop(line);
			if (result && *result) {
				write_loop(spp, result);
				free(result);
			}
		}

		*buf = '\0';
	};

	return finish(spp);
}

void
print_bdaddr (gchar *prefix, const bdaddr_t *bdaddr)
{
	fprintf(stderr, "%s", prefix);

	// print BTADDR in reverse
	for (int i = 5; i > -1; i--) {
		fprintf(stderr, "%02X", bdaddr->b[i]);
		if (i > 0) fprintf(stderr, ":");
	}

	fprintf(stderr, "\n");
}

static gboolean
on_handle_new_connection (OrgBluezProfile1 *interface,
			GDBusMethodInvocation *invocation,
			const gchar           *device,
			const GVariant        *fd,
			const GVariant        *fd_props,
			gpointer              user_data)
{
	GDBusMessage *message;
	GError *error = NULL;	
	GUnixFDList *fd_list;
	socklen_t optlen;
	struct sockaddr_rc saddr;
	struct spp_data *spp = user_data;

	message = g_dbus_method_invocation_get_message (invocation);
	fd_list = g_dbus_message_get_unix_fd_list (message);
	spp->sock_fd = g_unix_fd_list_get (fd_list, 0, &error);
	g_assert_no_error (error);

	fprintf(stderr, "handle_new_conn called for device: %s fd: %d!\n", device, spp->sock_fd);

	memset(&saddr, 0, sizeof(saddr));
	optlen = sizeof(saddr);

	if (getsockname (spp->sock_fd, (struct sockaddr *) &(spp->local), &optlen) < 0) {
		fprintf(stderr, "handle_new_conn: local getsockname failed: %s\n", strerror(errno));
		return FALSE;
	}

	print_bdaddr("handle_new_conn local: ", &(spp->local.rc_bdaddr));

	memset(&saddr, 0, sizeof(saddr));
	if (getpeername (spp->sock_fd, (struct sockaddr *) &(spp->remote), &optlen) < 0) {
		fprintf(stderr, "handle_new_conn: remote getsockname failed: %s\n", strerror(errno));
		return FALSE;
	}

	print_bdaddr("handle_new_conn remote: ", &(spp->remote.rc_bdaddr));
	
	// finished with method call; no reply sent
	g_dbus_method_invocation_return_value(invocation, NULL);

	if (spp->server) {
		g_idle_add(server_read_data, spp);
	} else {	
		g_idle_add(client_write_data, spp);
	}
		
	return TRUE;
}

int main(int argc, char *argv[])
{
	GDBusProxy *proxy;
	GDBusConnection *conn;
	GError *error = NULL;
	OrgBluezProfile1 *interface;
	struct spp_data *spp;	

	spp = g_new0 (struct spp_data, 1);

	/* TODO: add real command-line handling.  Currently any string
	 * specified on the command-line triggers client mode.  This
	 * is leftover behavior from the legacy rfcomm sample application
	 * which required the client to know the remote server's MAC addr.
	 */
	if (argc == 1) {
		spp->server = true;
	}

	spp->loop = g_main_loop_new (NULL, FALSE);
	
	conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	g_assert_no_error (error);

	proxy = g_dbus_proxy_new_sync (conn,
				G_DBUS_PROXY_FLAGS_NONE,
				NULL,/* GDBusInterfaceInfo */
				"org.bluez",/* name */
				"/org/bluez",/* object path */
				"org.bluez.ProfileManager1",/* interface */
				NULL,/* GCancellable */
				&error);
	g_assert_no_error (error);

	if (register_profile (spp, proxy)) {
		return 0;
	}

	interface = org_bluez_profile1_skeleton_new ();

	g_signal_connect (interface,
			"handle_new_connection",
			G_CALLBACK (on_handle_new_connection),
			spp);

	error = NULL;
	if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (interface),
						conn,
						"/bluetooth/profile/serial_port",
						&error))
	{
		fprintf(stderr, "dbus_interface_skeleton_export failed for SPP!\n");
		return 1;
	}

	g_main_loop_run (spp->loop);

	g_object_unref (proxy);
	g_object_unref (conn);

	return 0;
}
