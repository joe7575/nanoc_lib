/*
 * NanoC non-blocking TCP sockets implementation.
 * AGPL-3.0-or-later
 */

#include "nanoc_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef NANOC_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define NANOC_NET_MAX 64

typedef struct {
	int used;
	int fd;
	int is_server;
	char *out;
	size_t out_len;
	size_t out_cap;
#ifdef NANOC_TLS
	SSL *ssl;
	SSL_CTX *ctx;
	int handshaking;
#endif
} net_slot_t;

static net_slot_t g_slots[NANOC_NET_MAX];

#ifdef NANOC_TLS
static char g_tls_error[256] = "unknown TLS error";
#endif

int nanoc_net_tls_available(void)
{
#ifdef NANOC_TLS
	return 1;
#else
	return 0;
#endif
}

const char *nanoc_net_tls_error(void)
{
#ifdef NANOC_TLS
	if (!g_tls_error[0])
		return "unknown TLS error";
	return g_tls_error;
#else
	return "TLS support not compiled in";
#endif
}

#ifdef NANOC_TLS
static void set_tls_error(const char *fallback)
{
	unsigned long e = ERR_get_error();
	if (e == 0) {
		snprintf(g_tls_error, sizeof(g_tls_error), "%s", fallback);
	} else {
		ERR_error_string_n(e, g_tls_error, sizeof(g_tls_error));
	}
}
#endif


static net_slot_t *slot(int id)
{
	if (id < 0 || id >= NANOC_NET_MAX || !g_slots[id].used)
		return NULL;
	return &g_slots[id];
}

static void set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int alloc_slot(int fd, int is_server)
{
	for (int i = 0; i < NANOC_NET_MAX; i++) {
		if (!g_slots[i].used) {
			memset(&g_slots[i], 0, sizeof(g_slots[i]));
			g_slots[i].used = 1;
			g_slots[i].fd = fd;
			g_slots[i].is_server = is_server;
			return i;
		}
	}
	return -1;
}

int nanoc_net_listen(const char *host, int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((uint16_t)port);
	if (!host || !strcmp(host, "localhost"))
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	else if (!strcmp(host, "0.0.0.0"))
		a.sin_addr.s_addr = htonl(INADDR_ANY);
	else
		a.sin_addr.s_addr = inet_addr(host);

	if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0 ||
			listen(fd, 4) < 0) {
		close(fd);
		return -1;
	}
	set_nonblock(fd);

	int id = alloc_slot(fd, 1);
	if (id < 0)
		close(fd);
	return id;
}

int nanoc_net_accept(int server_id)
{
	net_slot_t *s = slot(server_id);
	if (!s || !s->is_server)
		return -1;

	int c = accept(s->fd, NULL, NULL);
	if (c < 0)
		return -1;
	set_nonblock(c);
	int id = alloc_slot(c, 0);
	if (id < 0)
		close(c);
	return id;
}

int nanoc_net_tls_accept(int id, const char *cert_file, const char *key_file,
		const char *ca_file, int verify_peer)
{
#ifdef NANOC_TLS
	net_slot_t *s = slot(id);
	if (!s || s->is_server || s->ssl || !cert_file || !key_file)
		return -1;

	SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
	if (!ctx) {
		set_tls_error("SSL_CTX_new failed");
		return -1;
	}
	if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
		set_tls_error("cannot set minimum TLS version");
		SSL_CTX_free(ctx);
		return -1;
	}
	if (SSL_CTX_use_certificate_chain_file(ctx, cert_file) != 1) {
		set_tls_error("cannot load certificate");
		SSL_CTX_free(ctx);
		return -1;
	}
	if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1 ||
			SSL_CTX_check_private_key(ctx) != 1) {
		set_tls_error("cannot load private key");
		SSL_CTX_free(ctx);
		return -1;
	}
	if (ca_file && ca_file[0]) {
		if (SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1) {
			set_tls_error("cannot load CA file");
			SSL_CTX_free(ctx);
			return -1;
		}
	}
	if (verify_peer && (!ca_file || !ca_file[0])) {
		set_tls_error("client verification needs nanoc_bridge_tls_ca");
		SSL_CTX_free(ctx);
		return -1;
	}
	SSL_CTX_set_verify(ctx, verify_peer ?
		(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT) : SSL_VERIFY_NONE, NULL);

	SSL *ssl = SSL_new(ctx);
	if (!ssl) {
		set_tls_error("SSL_new failed");
		SSL_CTX_free(ctx);
		return -1;
	}
	if (SSL_set_fd(ssl, s->fd) != 1) {
		set_tls_error("SSL_set_fd failed");
		SSL_free(ssl);
		SSL_CTX_free(ctx);
		return -1;
	}
	SSL_set_accept_state(ssl);
	SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE |
		SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER |
		SSL_MODE_RELEASE_BUFFERS);
	s->ssl = ssl;
	s->ctx = ctx;
	s->handshaking = 1;
	return 0;
#else
	(void)id;
	(void)cert_file;
	(void)key_file;
	(void)ca_file;
	(void)verify_peer;
	return -1;
#endif
}

#ifdef NANOC_TLS
/* Drive a non-blocking handshake. 0 = ok/still running, -1 = fatal. */
static int tls_handshake(net_slot_t *s)
{
	if (!s->ssl || !s->handshaking)
		return 0;
	int r = SSL_do_handshake(s->ssl);
	if (r == 1) {
		s->handshaking = 0;
		return 0;
	}
	int e = SSL_get_error(s->ssl, r);
	if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
		return 0;
	set_tls_error("TLS handshake failed");
	return -1;
}
#endif

int nanoc_net_recv(int id, char *buf, int max)
{
	net_slot_t *s = slot(id);
	if (!s || s->is_server)
		return -1;

#ifdef NANOC_TLS
	if (s->ssl) {
		if (s->handshaking) {
			if (tls_handshake(s) < 0)
				return -1;
			if (s->handshaking)
				return 0;
		}
		int n = SSL_read(s->ssl, buf, max);
		if (n > 0)
			return n;
		int e = SSL_get_error(s->ssl, n);
		if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
			return 0;
		return -1;               /* zero return or fatal error */
	}
#endif

	int n = (int)recv(s->fd, buf, (size_t)max, 0);
	if (n > 0)
		return n;
	if (n == 0)
		return -1;                 /* peer closed */
	if (errno == EAGAIN || errno == EWOULDBLOCK)
		return 0;
	return -1;
}

int nanoc_net_send(int id, const char *data, int len)
{
	net_slot_t *s = slot(id);
	if (!s || len < 0)
		return -1;

	if (s->out_len + (size_t)len + 1 > s->out_cap) {
		size_t cap = s->out_cap ? s->out_cap : 1024;
		while (cap < s->out_len + (size_t)len + 1)
			cap *= 2;
		char *p = realloc(s->out, cap);
		if (!p)
			return -1;
		s->out = p;
		s->out_cap = cap;
	}
	memcpy(s->out + s->out_len, data, (size_t)len);
	s->out_len += (size_t)len;
	return len;
}

int nanoc_net_flush(int id)
{
	net_slot_t *s = slot(id);
	if (!s)
		return -1;

#ifdef NANOC_TLS
	if (s->ssl) {
		if (s->handshaking) {
			if (tls_handshake(s) < 0)
				return -1;
			if (s->handshaking)
				return 0;
		}
		size_t off = 0;
		while (off < s->out_len) {
			int n = SSL_write(s->ssl, s->out + off,
					(int)(s->out_len - off));
			if (n > 0) {
				off += (size_t)n;
				continue;
			}
			int e = SSL_get_error(s->ssl, n);
			if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
				break;
			set_tls_error("TLS write failed");
			return -1;
		}
		if (off > 0) {
			memmove(s->out, s->out + off, s->out_len - off);
			s->out_len -= off;
		}
		return 0;
	}
#endif

	size_t off = 0;
	while (off < s->out_len) {
		ssize_t n = send(s->fd, s->out + off, s->out_len - off, MSG_NOSIGNAL);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			break;
		return -1;
	}
	if (off > 0) {
		memmove(s->out, s->out + off, s->out_len - off);
		s->out_len -= off;
	}
	return 0;
}

void nanoc_net_close(int id)
{
	net_slot_t *s = slot(id);
	if (!s)
		return;
#ifdef NANOC_TLS
	if (s->ssl) {
		SSL_shutdown(s->ssl);
		SSL_free(s->ssl);
		s->ssl = NULL;
	}
	if (s->ctx) {
		SSL_CTX_free(s->ctx);
		s->ctx = NULL;
	}
#endif
	close(s->fd);
	free(s->out);
	memset(s, 0, sizeof(*s));
}
