/*
 * NanoC techage command queues (mod-side API).
 *
 *  write_cmnd/write_signal: mod -> VM (incoming, program polls with ta_recv)
 *  read_signal:             VM -> mod (outgoing, program sent with ta_signal)
 *
 * AGPL-3.0-or-later
 */

#include "nanoc_techage.h"

#include <string.h>

int nanoc_techage_write_cmnd(nanoc_vm_t *h, uint16_t port, uint16_t topic,
		const uint16_t *payload, uint16_t num_words, uint16_t type)
{
	if (num_words > NANOC_MAX_CMND_WORDS - 1)
		return -1;
	if (h->in_cmnd_count >= NANOC_CMND_QUEUE_LEN)
		return -1;

	nanoc_cmnd_t *c = &h->in_cmnd_q[h->in_cmnd_tail];
	c->type = type ? type : 1;
	c->port = port;
	c->length = (uint16_t)(1 + num_words);
	c->words[0] = topic;
	if (num_words && payload)
		memcpy(&c->words[1], payload, num_words * sizeof(uint16_t));

	h->in_cmnd_tail = (h->in_cmnd_tail + 1) % NANOC_CMND_QUEUE_LEN;
	h->in_cmnd_count++;
	return 1;
}

int nanoc_techage_read_cmnd(nanoc_vm_t *h, uint16_t *port, uint16_t *topic,
		uint16_t *payload, uint16_t *num_words, uint16_t *type)
{
	if (h->cmnd_count == 0)
		return 0;

	nanoc_cmnd_t *c = &h->cmnd_q[h->cmnd_head];
	if (port)
		*port = c->port;
	if (topic)
		*topic = c->words[0];
	if (type)
		*type = c->type;
	uint16_t n = (uint16_t)(c->length > 0 ? c->length - 1 : 0);
	if (num_words)
		*num_words = n;
	if (payload && n)
		memcpy(payload, &c->words[1], n * sizeof(uint16_t));

	h->cmnd_head = (h->cmnd_head + 1) % NANOC_CMND_QUEUE_LEN;
	h->cmnd_count--;
	return 1;
}

int nanoc_techage_write_signal(nanoc_vm_t *h, uint16_t port, uint16_t signal)
{
	if (h->in_sig_count >= NANOC_SIGNAL_QUEUE_LEN)
		return -1;

	nanoc_signal_t *s = &h->in_sig_q[h->in_sig_tail];
	s->port = port;
	s->signal = signal;

	h->in_sig_tail = (h->in_sig_tail + 1) % NANOC_SIGNAL_QUEUE_LEN;
	h->in_sig_count++;
	return 1;
}

int nanoc_techage_read_signal(nanoc_vm_t *h, uint16_t *port, uint16_t *signal)
{
	if (h->sig_count == 0)
		return 0;

	nanoc_signal_t *s = &h->sig_q[h->sig_head];
	if (port)
		*port = s->port;
	if (signal)
		*signal = s->signal;

	h->sig_head = (h->sig_head + 1) % NANOC_SIGNAL_QUEUE_LEN;
	h->sig_count--;
	return 1;
}
