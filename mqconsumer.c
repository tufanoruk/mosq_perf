/**
 * $Id: mqconsumer.c 135 2012-01-19 10:06:31Z tufan $
 *
 * mqconsumer -t <topicname> -q <qos> -d <debuglevel> -h <host> -p <port>
 *
 */

#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <limits.h>

#include <mosquitto.h>

#include "mq_log.h"
#include "mq_util.h"
#include "mq_message.h"


#define MAX_TOPIC_NAME_LEN 1024  // seems big enough
#define MAX_HOST_NAME_LEN 256
#define MAX_NUM_OF_STATS 10000 // max number of samples

#ifdef MOSQ_DEBUG
  #define MOSQ_LOG_LEVEL  (MOSQ_LOG_DEBUG | MOSQ_LOG_ERR | MOSQ_LOG_WARNING | \
		MOSQ_LOG_NOTICE | MOSQ_LOG_INFO)
#else
  #define MOSQ_LOG_LEVEL  (MOSQ_LOG_ERR)
#endif

#define MOSQ_LOOP_TIMEOUT 10 // miliseconds

#define MOSQ_DEFAULT_HOST "localhost"
#define MOSQ_DEFAULT_PORT 1883

#define MOSQ_KEEPALIVE_TIMEOUT 10 // seconds

typedef struct Args {
	char topic_name[MAX_TOPIC_NAME_LEN];
	char host_name[MAX_HOST_NAME_LEN];
	int port;
	int qos;
	int debug_level;
} Args;

typedef struct JitterStat {
	int from;
	int to;
	long rx_usec_from_to;
	long tx_usec_from_to;
} JitterStat;

/**
 * this is not very reliable when producer and consumer resides on different machines.
 * even both machines are synchronized w/ NTP
 */
typedef struct DelayStat {
	int mid;
	long usec_tx_delay;
} DelayStat;

void dump_delay_stats();
void dump_jitter_stats();

static Args mq_args;

static JitterStat* mq_jitter_stats = 0;
static DelayStat*  mq_delay_stats = 0;

/**
 * print_usage
 */
static void print_usage () {
	fprintf (stderr, "Usage: mqconsumer -t <topicname> "
			                          "[-q <qos> (0-2)] "
			                          "[-d <debuglevel> (0-3)] "
			                          "[-h <broker-host> (localhost)] "
			                          "[-p <broker-port> (1883)]"
									  "-? (prints out this usage)");
}

/**
 * parse_args
 */
static int parse_args(int ac, char** av) {
	int c = 0;

	memset(mq_args.topic_name, 0, MAX_TOPIC_NAME_LEN);
	mq_args.qos = 0;
	mq_args.debug_level = MQ_LOG_ERROR; // error!
	strncpy (mq_args.host_name, MOSQ_DEFAULT_HOST, MAX_HOST_NAME_LEN);
	mq_args.port = MOSQ_DEFAULT_PORT;

	while ((c = getopt(ac, av, "?t:q:d:h:p:")) != -1) {
		switch (c) {
		case '?':
			print_usage();
			exit(EXIT_SUCCESS);
			break;
		case 't':
			strncpy (mq_args.topic_name, optarg, MAX_TOPIC_NAME_LEN);
			break;
		case 'q':
			mq_args.qos = atoi (optarg);
			break;
		case 'd':
			mq_args.debug_level = atoi (optarg);
			break;
		case 'h':
			strncpy (mq_args.host_name, optarg, MAX_HOST_NAME_LEN);
			break;
		case 'p':
			mq_args.port = atoi (optarg);
			break;

		default:
			mq_log_error ("'%c' %s",c, "unknown parameter!");
			return -1;
		};
	}

	if (strlen(mq_args.topic_name) == 0) {
		mq_log_error("%s", "Topic name must be supplied!");
		return -1;
	}

	if (mq_args.qos > 2) {
		mq_log_error ("%s", "QoS must be 0-2");
		return -1;
	}

	mq_log_debug ("'%s', %d, %d", mq_args.topic_name, mq_args.qos, mq_args.debug_level);
	return 0;
}


/**
 * mq_connect_callback
 */
static void mq_connect_callback(void *obj, int result) {

	void* p = 0;
	void* q = 0;

	if(!result){
		mq_log_info ("Connected!\n");
	}else{
		mq_util_print_error (result);
	}
	p = malloc (MAX_NUM_OF_STATS * sizeof(JitterStat));
	q = malloc (MAX_NUM_OF_STATS * sizeof(DelayStat));
	if (p && q) {
		memset (p, 0, MAX_NUM_OF_STATS * sizeof(JitterStat));
		mq_jitter_stats = (JitterStat*) p;

		memset (q, 0, MAX_NUM_OF_STATS * sizeof(DelayStat));
		mq_delay_stats = (DelayStat*) q;

	} else {
		mq_log_error ("Memory for statistical data cannot be allocated!");
	}
}


/**
 *
 */
static void mq_disconnect_callback(void *obj) {

	mq_log_debug ("mq_disconnect_callback");

	dump_jitter_stats();
	printf ("\n\n");
	dump_delay_stats();
}


/**
 *
 */
static void mq_subscribe_callback(void *obj, 
		uint16_t mid,
		int qos_count,
		const uint8_t *granted_qos) {
	// @@@ TODO
	mq_log_debug ("mq_subscribe_callback for %d", mid);
}

/**
 *
 */
static void mq_unsubscribe_callback(void *obj, uint16_t mid) {
	// @@@ TODO
	mq_log_debug ("mq_unsubscribe_callback for %d", mid);
}

static void mq_on_message_callback(void *obj, const struct mosquitto_message* msg) {

	static int message_count = 0;
	static struct mosquitto_message previous_message;
	static struct timeval previous_msg_rx_tv = {0,0};

	struct timeval now = {0,0};
	double rx_dt_usec = 0.0;
	double tx_dt_usec = 0.0;
	JitterStat* jit_s = mq_jitter_stats;
	DelayStat* dly_s = mq_delay_stats;

	struct mosquitto* mosq = (struct mosquitto*) obj;

	mq_log_debug ("mq_on_message_callback");

	gettimeofday (&now, 0); // current message rx time

	if (msg->payloadlen == 0) {
		// this is  a disconnect message!
		mq_log_info ("Got ZERO payload message! Disconnecting!");
		mosquitto_disconnect(mosq);
	} else {
		dly_s += message_count;
		dly_s->mid = mq_message_id ((byte*)msg->payload);
		dly_s->usec_tx_delay = mq_util_timeval_diff_usec(now, mq_message_txtime((byte*)msg->payload));

		/* determine jitter */
		if (message_count > 0) {
			/**
			 * Take the difference of two packet tx/rx timestamps.
			 * Since packet production rate is constant during a test session,
			 * the difference of two consecutive packet timestamps gives us a relative delay,
			 * including packet production rate.
			 * Later we will use these relative delays to calculate the delay variations
			 * by simply calculating their difference.
			 * If everything were perfect, then (relative) delay variation (jitter) would be 0.
			 */

			rx_dt_usec = mq_util_timeval_diff_usec (now, previous_msg_rx_tv);
			tx_dt_usec = mq_util_timeval_diff_usec (mq_message_txtime((byte*)msg->payload),
													mq_message_txtime((byte*)previous_message.payload));
			jit_s += (message_count - 1);
			jit_s->from = mq_message_id ((byte*)previous_message.payload);
			jit_s->to = mq_message_id ((byte*)msg->payload);
			jit_s->rx_usec_from_to = rx_dt_usec;
			jit_s->tx_usec_from_to = tx_dt_usec;
		}
		mosquitto_message_copy(&previous_message, msg);
		previous_msg_rx_tv = mq_message_txtime((byte*)msg->payload);
		message_count++;
	}
}


void dump_jitter_stats() {

	JitterStat* jit_s = 0;
	int index = 0;

	long tx_jitter = 0L;
	long rx_jitter = 0L;

	long rx_min = LONG_MAX;
	long rx_max = LONG_MIN;
	double rx_avg = 0.0;

	long tx_min = LONG_MAX;
	long tx_max = LONG_MIN;
	double tx_avg = 0.0;

	// dump stats
	if (mq_jitter_stats) {
		while ( (jit_s = (mq_jitter_stats + index)) != 0) {

			if (jit_s->from == 0) break;

			printf ("%4d %4d %6ld %6ld",
					jit_s->from, jit_s->to,
					jit_s->tx_usec_from_to, jit_s->rx_usec_from_to);

			if (index) {
				tx_jitter = (jit_s - 1)->tx_usec_from_to - jit_s->tx_usec_from_to;
				rx_jitter = (jit_s - 1)->rx_usec_from_to - jit_s->rx_usec_from_to;

				printf (" %6ld",      tx_jitter);
				printf (" %6ld usec", rx_jitter);

				if (rx_jitter < rx_min) rx_min = rx_jitter;
				if (rx_jitter > rx_max) rx_max = rx_jitter;

				if (tx_jitter < tx_min) tx_min = tx_jitter;
				if (tx_jitter > tx_max) tx_max = tx_jitter;

				tx_avg += labs(tx_jitter);
				rx_avg += labs(rx_jitter);

			}
      printf("\n");
			index++;
		}
		rx_avg = rx_avg / index;
		tx_avg = tx_avg / index;

		printf ("Jitter ------------------------------------------------\n");
		printf("TX: %d pairs, %4ld / %4ld / %6.2f usec\n", index, tx_min, tx_max, tx_avg);
		printf("RX: %d pairs, %4ld / %4ld / %6.2f usec\n", index, rx_min, rx_max, rx_avg);
	}
}


void dump_delay_stats() {

	DelayStat* dly_s = 0;

	int index = 0;
	long dly_min = 0L;
	long dly_max = 0L;
	double dly_avg = 0.0;

	if (mq_delay_stats) {
		while ( (dly_s = (mq_delay_stats + index)) != 0) {

			if (dly_s->mid == 0) break;

			printf ("%4d %6ld usec\n", dly_s->mid, dly_s->usec_tx_delay);
			if (index) {
				if (dly_s->usec_tx_delay < dly_min) dly_min = dly_s->usec_tx_delay;
				if (dly_s->usec_tx_delay > dly_max) dly_max = dly_s->usec_tx_delay;
			} else {
				dly_min = dly_s->usec_tx_delay;
				dly_max = dly_s->usec_tx_delay;
			}
			dly_avg += dly_s->usec_tx_delay;
			index++;
		}
		dly_avg = dly_avg / index;
		printf ("Delay ------------------------------------------------\n");
		printf("%d samples, %ld / %ld / %6.2f usec\n", index, dly_min, dly_max, dly_avg);
	}
}


/**
 * main
 */
int main (int ac, char** av) {

	char* bname = 0;
	char* client_id = 0;
	struct mosquitto* mosq = 0;
	int result = MOSQ_ERR_SUCCESS;
	uint16_t  smid = 0; // subscribe message id!

	bname = strdup (basename(av[0]));
	client_id = malloc (strlen(bname) + 16); // space for pid
	sprintf (client_id, "%s_%d", bname, getpid());

	// log needs to be initialized for parse_args and print_usage!
	mq_log_init (client_id, MQ_LOG_ERROR);

	if (parse_args (ac, av) == -1) {
		print_usage ();
		exit(EXIT_FAILURE);
		// does not reach here!
	}
	// re-set log level
	mq_log_set_debug_level (mq_args.debug_level);

	mq_log_info ("This subscriber id is '%s'", client_id);

	// now we can start mqtt staff
	mosquitto_lib_init ();
	mosq = mosquitto_new (client_id, 0);
	if (!mosq) {
		mq_log_error ("Error creating mosquito instance!");
		exit (EXIT_FAILURE);
	}
	mosquitto_log_init (mosq, MOSQ_LOG_LEVEL, MOSQ_LOG_STDERR);

	mosquitto_connect_callback_set(mosq, mq_connect_callback);
	mosquitto_disconnect_callback_set(mosq, mq_disconnect_callback);
	mosquitto_subscribe_callback_set(mosq, mq_subscribe_callback);
	mosquitto_unsubscribe_callback_set(mosq, mq_unsubscribe_callback);
	mosquitto_message_callback_set (mosq, mq_on_message_callback);

	result = mosquitto_connect(mosq, mq_args.host_name, mq_args.port, MOSQ_KEEPALIVE_TIMEOUT, true);
	if (result != MOSQ_ERR_SUCCESS) {
		mq_util_print_error (result);
		goto cleanup;
	}

	result = mosquitto_subscribe (mosq, &smid, &mq_args.topic_name[0], mq_args.qos);
	if ( result != MOSQ_ERR_SUCCESS) {
		mq_util_print_error (result);
		goto cleanup;
	}
	mq_log_info ("Subscribed with message id %d",smid);


	do {

		result = mosquitto_loop(mosq,MOSQ_LOOP_TIMEOUT);

	} while (result == MOSQ_ERR_SUCCESS);

	/* CLEANUP LABEL*/
	cleanup:


	if (mq_jitter_stats) {
		free (mq_jitter_stats);
		mq_jitter_stats = 0;
	}
	if (mq_delay_stats) {
		free (mq_delay_stats);
		mq_delay_stats = 0;
	}

	mosquitto_destroy (mosq);
	mosquitto_lib_cleanup();

	mq_log_destroy();

	free (client_id);
	free (bname);
	printf ("Done! (%d)\n", getpid());

	return 0;
}
