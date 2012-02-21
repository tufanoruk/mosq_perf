/**
 * $Id: mq_log_syslog.c 117 2012-01-16 07:02:30Z tufan $
 * 
 * log implementation that utilize syslog
 *
 */

#include "mq_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>

#define SYSLOG_OPTIONS  (LOG_CONS | LOG_NDELAY | LOG_NOWAIT | LOG_PERROR | LOG_PID)
#define SYSLOG_FACILITY (LOG_USER)

#define MAX_LOG_MSG_EXPAND 1024

static char* make_log_message (const char* fmt, va_list va) {
  
  char* msg = 0;
  int max_len = strlen (fmt) + MAX_LOG_MSG_EXPAND; 

  msg = malloc (max_len);

  memset (msg, 0, max_len);

  vsnprintf(msg, max_len, fmt, va);

  msg[max_len-1] = 0;

  return msg;
}

static void mq_log (int level, const char* fmt, va_list va) {

  static char* level_to_string[] = {
		  "ERMERGENCY",
		  "ALERT",
		  "CRITICAL",
		  "ERROR",
		  "WARNING",
		  "NOTICE",
		  "INFO",
		  "DEBUG"};

  char* msg = make_log_message (fmt, va);
  syslog (level, "%s : %s", level_to_string[level], msg);
  free (msg);
}

// --- interface implementation

void mq_log_init(const char* user, int level) {

  openlog (user, SYSLOG_OPTIONS, SYSLOG_FACILITY);

  if (level < 0 || level > 3) {
	  mq_log_error("Wrong debug level '%d'. Using debug level '%d'", level , MQ_LOG_ERROR);
	  level = MQ_LOG_ERROR;
  }
  mq_log_set_debug_level (level);
}

void mq_log_set_debug_level (int level) {

	static int syslog_lookup [4] = {LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG};

	level = syslog_lookup[level];
	setlogmask (LOG_UPTO(level));
}

void mq_log_destroy () {
  closelog();
}

void mq_log_error (const char* fmt, ...) {
  va_list va;
  va_start (va, fmt);
  mq_log (LOG_ERR, fmt, va);
  va_end(va);
}

void mq_log_warning (const char* fmt, ...) {
  va_list va;
  va_start (va, fmt);
  mq_log (LOG_WARNING, fmt, va);
  va_end (va);
}

void mq_log_info (const char* fmt, ...) {
  va_list va;
  va_start (va, fmt);
  mq_log (LOG_INFO, fmt, va);
  va_end (va);
}

void mq_log_debug (const char* fmt, ...) {
  va_list va;
  va_start (va, fmt);
  mq_log (LOG_DEBUG, fmt, va);
  va_end(va);
}

