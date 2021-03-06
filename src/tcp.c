// ===========================================================
// netstack -- A network stack implementation for BareMetal OS
//
// Copyright (C) 2017 Return Infinity -- see LICENSE
// ===========================================================

#include <netstack/tcp.h>

#include <netstack/buffer.h>
#include <netstack/error.h>
#include <netstack/mutator.h>

#include <limits.h>

static int parse_port(unsigned int *port_ptr,
                      const char *str, size_t str_size)
{
	// using an unsigned long int ensures
	// that if a number greater than 65535
	// is passed, we can detect that it is
	// greater than the 16-bit max (unsigned
	// int, on some platforms, may only be
	// 16-bits).
	unsigned long int port = 0;

	if (str_size > 5)
	{
		// 65535 is the max, so
		// if more than five digits
		// have been passed, only five
		// will be used.
		str_size = 5;
	}

	for (size_t i = 0; i < str_size; i++)
	{
		char c = str[i];
		if ((c >= '0') && (c <= '9'))
		{
			port *= 10;
			port += c - '0';
		}
		else
			return NETSTACK_ERROR_BAD_FIELD;
	}

	// port exceeds maximum
	if (port > 65535)
		return NETSTACK_ERROR_BAD_FIELD;

	*port_ptr = (unsigned int) port;

	return NETSTACK_ERROR_NONE;
}

void netstack_tcp_init(struct netstack_tcp *tcp)
{
	tcp->source = 0;
	tcp->destination = 0;
	tcp->sequence = 0;
	tcp->acknowledgment = 0;
	tcp->data_offset = 5;
	tcp->control_bits = 0;
	tcp->window_size = 1;
	tcp->checksum = 0;
	tcp->urgent_pointer = 0;
}

int netstack_tcp_set_source(struct netstack_tcp *tcp,
                            const char *str,
                            size_t str_size)
{
	return parse_port(&tcp->source, str, str_size);
}

int netstack_tcp_set_destination(struct netstack_tcp *tcp,
                                 const char *str,
                                 size_t str_size)
{
	return parse_port(&tcp->destination, str, str_size);
}

int netstack_tcp_mutate(struct netstack_tcp *tcp,
                        const struct netstack_mutator *mutator)
{
	if (mutator->mutate_tcp == NULL)
		return NETSTACK_ERROR_NONE;

	return mutator->mutate_tcp(mutator->data, tcp);
}

int netstack_tcp_pack(struct netstack_tcp *tcp,
                      struct netstack_buffer *buffer)
{
	int err = netstack_buffer_shift(buffer, 20);
	if (err != 0)
		return err;

	unsigned char *header = buffer->data;

	// source port
	header[0] = (0xff00 & tcp->source) >> 8;
	header[1] = (0x00ff & tcp->source) >> 0;
	// destination port
	header[2] = (0xff00 & tcp->destination) >> 8;
	header[3] = (0x00ff & tcp->destination) >> 0;
	// sequence number
	header[4] = (0xff000000 & tcp->sequence) >> 24;
	header[5] = (0x00ff0000 & tcp->sequence) >> 16;
	header[6] = (0x0000ff00 & tcp->sequence) >> 8;
	header[7] = (0x000000ff & tcp->sequence) >> 0;
	// acknowledgment
	header[8]  = (0xff000000 & tcp->acknowledgment) >> 24;
	header[9]  = (0x00ff0000 & tcp->acknowledgment) >> 16;
	header[10] = (0x0000ff00 & tcp->acknowledgment) >> 8;
	header[11] = (0x000000ff & tcp->acknowledgment) >> 0;
	// zero fields that require bit shifting
	header[12] = 0;
	header[13] = 0;
	// data offset (5 32-bit words)
	header[12] = (5 << 4);
	// control bits
	if (tcp->control_bits & NETSTACK_TCP_NS)
		header[12] |= 0x01;
	if (tcp->control_bits & NETSTACK_TCP_CWR)
		header[13] |= 0x80;
	if (tcp->control_bits & NETSTACK_TCP_ECE)
		header[13] |= 0x40;
	if (tcp->control_bits & NETSTACK_TCP_URG)
		header[13] |= 0x20;
	if (tcp->control_bits & NETSTACK_TCP_ACK)
		header[13] |= 0x10;
	if (tcp->control_bits & NETSTACK_TCP_PSH)
		header[13] |= 0x08;
	if (tcp->control_bits & NETSTACK_TCP_RST)
		header[13] |= 0x04;
	if (tcp->control_bits & NETSTACK_TCP_SYN)
		header[13] |= 0x02;
	if (tcp->control_bits & NETSTACK_TCP_FIN)
		header[13] |= 0x01;
	// window size
	header[14] = (tcp->window_size & 0xff00) >> 8;
	header[15] = (tcp->window_size & 0x00ff) >> 0;
	// checksum placeholder
	header[16] = 0;
	header[17] = 0;
	// urgent pointer
	header[18] = (tcp->urgent_pointer & 0xff00) >> 8;
	header[19] = (tcp->urgent_pointer & 0x00ff) >> 0;
	// TODO : checksum
	return NETSTACK_ERROR_NONE;
}

int netstack_tcp_unpack(struct netstack_tcp *tcp,
                        struct netstack_buffer *buffer)
{
	if (buffer->size < 20)
		return NETSTACK_ERROR_MISSING_DATA;

	unsigned char *header = (unsigned char *) buffer->data;

	tcp->source = 0;
	tcp->source |= ((unsigned int) header[0]) << 8;
	tcp->source |= ((unsigned int) header[1]) << 0;

	tcp->destination = 0;
	tcp->destination |= ((unsigned int) header[2]) << 8;
	tcp->destination |= ((unsigned int) header[3]) << 0;

	tcp->sequence = 0;
	tcp->sequence |= ((unsigned long int) header[4]) << 24;
	tcp->sequence |= ((unsigned long int) header[5]) << 16;
	tcp->sequence |= ((unsigned long int) header[6]) << 8;
	tcp->sequence |= ((unsigned long int) header[7]) << 0;

	tcp->acknowledgment = 0;
	tcp->acknowledgment |= ((unsigned long int) header[8]) << 24;
	tcp->acknowledgment |= ((unsigned long int) header[9]) << 16;
	tcp->acknowledgment |= ((unsigned long int) header[10]) << 8;
	tcp->acknowledgment |= ((unsigned long int) header[11]) << 0;

	tcp->data_offset = ((unsigned int) (header[12] >> 4));

	tcp->control_bits = 0;

	if (header[12] & 0x01)
		tcp->control_bits |= NETSTACK_TCP_NS;
	if (header[13] & 0x80)
		tcp->control_bits |= NETSTACK_TCP_CWR;
	if (header[13] & 0x40)
		tcp->control_bits |= NETSTACK_TCP_ECE;
	if (header[13] & 0x20)
		tcp->control_bits |= NETSTACK_TCP_URG;
	if (header[13] & 0x10)
		tcp->control_bits |= NETSTACK_TCP_ACK;
	if (header[13] & 0x08)
		tcp->control_bits |= NETSTACK_TCP_PSH;
	if (header[13] & 0x04)
		tcp->control_bits |= NETSTACK_TCP_RST;
	if (header[13] & 0x02)
		tcp->control_bits |= NETSTACK_TCP_SYN;
	if (header[13] & 0x01)
		tcp->control_bits |= NETSTACK_TCP_FIN;

	tcp->window_size = 0;
	tcp->window_size |= ((unsigned int) header[14]) << 8;
	tcp->window_size |= ((unsigned int) header[15]) << 0;

	tcp->checksum = 0;
	tcp->checksum |= ((unsigned int) header[16]) << 8;
	tcp->checksum |= ((unsigned int) header[17]) << 0;

	tcp->urgent_pointer = 0;
	tcp->urgent_pointer |= ((unsigned int) header[18]) << 8;
	tcp->urgent_pointer |= ((unsigned int) header[19]) << 0;

	return NETSTACK_ERROR_NONE;
}
