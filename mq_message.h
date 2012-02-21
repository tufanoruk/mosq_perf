/*
 * mq_message.h
 *
 *  Created on: Jan 16, 2012
 *      Author: tufanoruk
 */

#ifndef MQ_MESSAGE_H_
#define MQ_MESSAGE_H_

#include <stdio.h>

/**
 * struct message {int id; timeval txtime; byte* load};
 *
 * first sizeof(int) bytes holds the message.id
 * sizeof(timeval) bytes after the first sizeof(int) bytes of the message holds mesage.txtime
 * load is payload_size - (sizeof(int) + sizeof(timeval))
 *
 */

typedef char byte;

/**
 * creates a reusable message
 */
void mq_message_init (int payload_size);

/**
 * updates id and txtime fields of the message and returns underlying message buffer
 */
byte* mq_message_renew ();

/**
 * cleans message buffer created in mq_message_init
 */
void mq_message_destroy ();

int mq_message_id (byte* msg);

struct timeval mq_message_txtime (byte* msg);

/**
 * helper function that dumps id and txtime to the given file
 *
 * FILE*f must be opened w/ fopen
 * */
void mq_message_dump (FILE* f, byte* msg);


#endif /* MQ_MESSAGE_H_ */
