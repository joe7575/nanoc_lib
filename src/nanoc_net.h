/*
 * NanoC non-blocking TCP sockets for the external client bridge.
 *
 * Small fd registry so Lua can poll without blocking the server thread.
 * Plain TCP by default; optional TLS (OpenSSL) when built with NANOC_TLS.
 *
 * AGPL-3.0-or-later
 */

#pragma once

/* Returns a slot id >= 0, or -1 on error. */
int nanoc_net_listen(const char *host, int port);

/* Returns a new client slot id, or -1 if none pending. */
int nanoc_net_accept(int server_id);

/* 1 if the module was built with TLS support, 0 otherwise. */
int nanoc_net_tls_available(void);

/* Last TLS error message (also set for "not compiled in"). */
const char *nanoc_net_tls_error(void);

/* Wrap an accepted socket in a server-side TLS session. The handshake is
 * driven non-blocking from nanoc_net_recv()/nanoc_net_flush().
 * verify_peer requires ca_file (client certificates). 0 ok, -1 error. */
int nanoc_net_tls_accept(int id, const char *cert_file, const char *key_file,
		const char *ca_file, int verify_peer);

/* > 0 bytes read, 0 nothing (would block), -1 closed/error. */
int nanoc_net_recv(int id, char *buf, int max);

/* Buffer data for sending (flushed by nanoc_net_flush). Returns len, -1 err. */
int nanoc_net_send(int id, const char *data, int len);

/* Try to send buffered data. 0 ok, -1 error. */
int nanoc_net_flush(int id);

void nanoc_net_close(int id);
