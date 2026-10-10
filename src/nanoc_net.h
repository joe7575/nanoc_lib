/*
 * NanoC non-blocking TCP sockets for the external client bridge.
 *
 * Small fd registry so Lua can poll without blocking the server thread.
 * Plain TCP only for now (TLS can be added via OpenSSL later).
 *
 * AGPL-3.0-or-later
 */

#pragma once

/* Returns a slot id >= 0, or -1 on error. */
int nanoc_net_listen(const char *host, int port);

/* Returns a new client slot id, or -1 if none pending. */
int nanoc_net_accept(int server_id);

/* > 0 bytes read, 0 nothing (would block), -1 closed/error. */
int nanoc_net_recv(int id, char *buf, int max);

/* Buffer data for sending (flushed by nanoc_net_flush). Returns len, -1 err. */
int nanoc_net_send(int id, const char *data, int len);

/* Try to send buffered data. 0 ok, -1 error. */
int nanoc_net_flush(int id);

void nanoc_net_close(int id);
