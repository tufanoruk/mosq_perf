/**
 * $Id: mq_log.h 117 2012-01-16 07:02:30Z tufan $
 *
 * logging related functionality
 *
 */

#ifndef _MQ_LOG_H_
#define _MQ_LOG_H_


enum MQ_LOG_DEBUG_LEVEL {
	MQ_LOG_ERROR = 0,
	MQ_LOG_WARNING = 1,
	MQ_LOG_INFO = 2,
	MQ_LOG_DEBUG = 3
};

/**
 * user is the name of the program
 * level is one of {error:0 | warning:1 | info:2 | debug:3 }
 */
void mq_log_init(const char* user, int level);

void mq_log_set_debug_level (int level);

void mq_log_destroy ();

void mq_log_error (const char* fmt, ...);

void mq_log_warning (const char* fmt, ...);

void mq_log_info (const char* fmt, ...);

void mq_log_debug (const char* fmt, ...);

#endif
