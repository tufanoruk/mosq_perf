/*
 * mq_message.c
 *
 *  Created on: Jan 16, 2012
 *      Author: tufanoruk
 */

/**
 *
 *  ASSUMING ALL PARTIES HAS THE SAME ENDIANESS AND PADDING
 *
 */

#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#include "mq_message.h"
#include "mq_log.h"

static int mq_payload_size = 0;
static byte* mq_msg = 0;
static int mq_msg_id = 1; // start from 1

void mq_message_init (int payload_size) {
	mq_payload_size = payload_size;
	mq_msg = (byte*) malloc (mq_payload_size);
	memset(mq_msg, 0, mq_payload_size);
}

byte* mq_message_renew() {

	struct timeval tv = {0,0};
	byte* p = mq_msg;

	memcpy (p, &mq_msg_id, sizeof(int));
	p += sizeof(int);

	if (gettimeofday (&tv,0) == -1) {
		mq_log_error ("Error getting time");
		return 0; // error!
	}

	memcpy (p, &tv,sizeof(struct timeval));
	p += sizeof(struct timeval);

	memset (p, 't', mq_payload_size - (sizeof(int) + sizeof(struct timeval)));

	mq_msg_id++;
	return mq_msg;
}

void mq_message_destroy () {
	if (mq_msg) {
		free (mq_msg);
		mq_msg = 0;
	}
}

int mq_message_id (byte* msg) {
	int id = 0;
	byte* p = (byte*)msg;
	if (p) {
		memcpy (&id, p, sizeof(int));
	} else {
		mq_log_error ("message had not been set!");
	}
	return id;
}

struct timeval mq_message_txtime (byte* msg) {
	struct timeval tv = {0,0};
	byte* p = (byte*)msg;
	memcpy (&tv, p + sizeof(int), sizeof(struct timeval));

	return tv;
}

/* FILE* f must be opened w/ fopen */
void mq_message_dump (FILE* f, byte* msg) {
	int id = mq_message_id(msg);
	struct timeval tv = mq_message_txtime(msg);
	fprintf (f,"[%d] %lu.%d\n", id, tv.tv_sec, tv.tv_usec);
}
