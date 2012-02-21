/*
 * mq_util.c
 *
 *  Created on: Jan 16, 2012
 *      Author: tufan
 */

#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include <mosquitto.h>

#include "mq_util.h"
#include "mq_log.h"

/* strongly inspired from glibc docs */
long mq_util_timeval_diff_usec (struct timeval x, struct timeval y) {

	struct timeval dt = {0,0};

	/* Perform the carry for the later subtraction by updating y. */
	if (x.tv_usec < y.tv_usec) {
		int nsec = (y.tv_usec - x.tv_usec) / 1000000L + 1;
		y.tv_usec -= 1000000L * nsec;
		y.tv_sec += nsec;
	}
	if (x.tv_usec - y.tv_usec > 1000000L) {
		int nsec = (x.tv_usec - y.tv_usec) / 1000000L;
		y.tv_usec += 1000000L * nsec;
		y.tv_sec -= nsec;
	}
	/* tv_usec is certainly positive. */
	dt.tv_sec = x.tv_sec - y.tv_sec;
	dt.tv_usec = x.tv_usec - y.tv_usec;

	return labs (dt.tv_sec * 1000000 + dt.tv_usec);
}


int mq_util_sleep_usecs_for_next_request (int freq, const struct timeval *previous_time) {

  unsigned long waitTime_usec    = 0;
  unsigned long prevReqTime_usec = 0;
  unsigned long nextReqTime_usec = 0;
  long sleepTime_usec            = 0;
  struct timeval currentTime;

  if(freq) {
    waitTime_usec = 1000000.0 * (1.0/freq);

    prevReqTime_usec = (previous_time->tv_sec)*1000000 +
      previous_time->tv_usec;

    nextReqTime_usec = prevReqTime_usec + waitTime_usec;

    gettimeofday(&currentTime,0);

    sleepTime_usec = nextReqTime_usec -
      (currentTime.tv_sec*1000000 + currentTime.tv_usec);
    /*
     * printf("nextReqTime: %lu + %lu = %lu\n",
     * prevReqTime_usec, waitTime_usec, nextReqTime_usec);
     */
    if(sleepTime_usec < 0) {
      mq_log_error ("Schedule error %lu usec", labs(sleepTime_usec));
      /*
       * printf("nextReqTime: %lu\n",
       * (unsigned long)(nextReqTime.tv_sec * 1000 +
       *                 nextReqTime.tv_usec / 1000));
       * printf("currentTime: %lu\n",
       * (unsigned long)(currentTime.tv_sec * 1000 +
       *                 currentTime.tv_usec / 1000));
       */
      return 0;
    }
  }
  mq_log_debug ("Shall sleep %d usec", sleepTime_usec);
  return sleepTime_usec;
}







/**
 * print_error
 */
void mq_util_print_error (int result) {
	switch(result){
	case MOSQ_ERR_NOMEM:
		mq_log_error ("No memory!");
		break;
	case MOSQ_ERR_PROTOCOL:
		mq_log_error ("Unacceptable protocol version!");
		break;
	case MOSQ_ERR_INVAL:
		mq_log_error ("Invalid value!");
		break;
	case MOSQ_ERR_NO_CONN:
		mq_log_error ("No connection!");
		break;
	case MOSQ_ERR_CONN_REFUSED:
		mq_log_error ("Connection refused!");
		break;
	case MOSQ_ERR_NOT_FOUND:
		mq_log_error ("Not found!");
		break;
	case MOSQ_ERR_CONN_LOST:
		mq_log_error ("Connection lost!");
		break;
	case MOSQ_ERR_SSL:
		mq_log_error ("SSL!");
		break;
	case MOSQ_ERR_PAYLOAD_SIZE:
		mq_log_error ("Payload size!");
		break;
	case MOSQ_ERR_NOT_SUPPORTED:
		mq_log_error ("Not supported!");
		break;
	case MOSQ_ERR_AUTH:
		mq_log_error ("Authentication!");
		break;
	case MOSQ_ERR_ACL_DENIED:
		mq_log_error ("Authorization!");
		break;
	default:
		mq_log_error ("Unknown reason!\n");
		break;
	}
}
