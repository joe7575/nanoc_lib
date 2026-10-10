/*
 * NanoC techage command queue.
 *
 * In-process "Beduino API" bridge: [type, length, topic, payload...]
 * Model: vm2 io_cmnd.c. The Lua mod maps ports to techage node numbers
 * and forwards via techage.beduino_send_cmnd / beduino_request_data.
 *
 * AGPL-3.0-or-later
 */

#pragma once

#include "nanoc_host.h"

/* status: 1 = success/free, 0 = no data, -1 = error (vm2 semantics) */

/* mod -> VM (incoming): program reads via ta_recv()/ta_recv_sig() */
int nanoc_techage_write_cmnd(nanoc_vm_t *h, uint16_t port, uint16_t topic,
		const uint16_t *payload, uint16_t num_words, uint16_t type);
int nanoc_techage_write_signal(nanoc_vm_t *h, uint16_t port, uint16_t signal);

/* VM -> mod (outgoing): program sent via ta_send()/ta_signal() */
int nanoc_techage_read_cmnd(nanoc_vm_t *h, uint16_t *port, uint16_t *topic,
		uint16_t *payload, uint16_t *num_words, uint16_t *type);
int nanoc_techage_read_signal(nanoc_vm_t *h, uint16_t *port, uint16_t *signal);
