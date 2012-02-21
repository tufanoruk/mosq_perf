/*
 * mq_util.h
 *
 *  Created on: Jan 16, 2012
 *      Author: tufan
 */

#ifndef MQ_UTIL_H_
#define MQ_UTIL_H_

#include <sys/time.h>

long mq_util_timeval_diff_usec (struct timeval x, struct timeval y);

int mq_util_sleep_usecs_for_next_request (int freq, const struct timeval* previous_time);

void mq_util_print_error (int result);

#endif /* MQ_UTIL_H_ */
