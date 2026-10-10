/*
 * NanoC non-blocking TCP sockets implementation.
 * AGPL-3.0-or-later
 */

#include "nanoc_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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
} net_slot_t;

static net_slot_t g_slots[NANOC_NET_MAX];

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

int nanoc_net_recv(int id, char *buf, int max)
{
	net_slot_t *s = slot(id);
	if (!s || s->is_server)
		return -1;

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
	close(s->fd);
	free(s->out);
	memset(s, 0, sizeof(*s));
}
