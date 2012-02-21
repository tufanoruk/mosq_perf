/**
 * $Id: mqproducer.c 169 2012-02-21 10:07:29Z tufan $
 *
 * mqproducer -s <size> -n <iterations> -f <frequency>
 *            -t <topicname> -q <qos> -d <debuglevel> -h <broker-host> -p <broker-port>
 *            -?
 *
 */

#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>

#include <mosquitto.h>

#include "mq_log.h"
#include "mq_util.h"
#include "mq_message.h"


#define MAX_TOPIC_NAME_LEN 1024  // seems big enough
#define MAX_HOST_NAME_LEN 256

#ifdef MOSQ_DEBUG
  #define MOSQ_LOG_LEVEL  (MOSQ_LOG_DEBUG | MOSQ_LOG_ERR | MOSQ_LOG_WARNING | \
		MOSQ_LOG_NOTICE | MOSQ_LOG_INFO)
#else
  #define MOSQ_LOG_LEVEL  (MOSQ_LOG_ERR)
#endif

#define MOSQ_LOOP_TIMEOUT 100 // miliseconds

#define MOSQ_DEFAULT_HOST "localhost"
#define MOSQ_DEFAULT_PORT 1883
#define MOSQ_DEFAULT_PAYLOAD_SIZE 256 // Bytes
#define MOSQ_DEFAULT_PUB_FREQ 1 // Hz
#define MOSQ_DEFAULT_NUM_MESSAGES 1000

#define MOSQ_KEEPALIVE_TIMEOUT 10 // seconds

#define MOSQ_DEFAULT_LOOP_MSEC 0 // msec, almost quantum!

typedef struct Args {
	char topic_name[MAX_TOPIC_NAME_LEN];
	char host_name[MAX_HOST_NAME_LEN];
	int port;
	int qos;
	int debug_level;
	int payload_size;
	int pub_freq;
	int num_messages;
} Args;

static Args mq_args;

/**
 * print_usage
 */
static void print_usage () {
	fprintf (stderr, "Usage: mqproducer -t <topicname>\n"
			         "                  [-q <qos> (0-2)]\n"
			         "                  [-d <debuglevel> (0-3)]\n"
				     "                  [-s <payloadsize> (256B)]\n"
				     "                  [-f <publish-frequency> (1Hz)]\n"
				     "                  [-n <number-of-messages> (1000)]\n"
			         "                  [-h <broker-host> (localhost)]\n"
			         "                  [-p <broker-port> (1883)]\n"
				     "                  -? (prints out this usage)\n");
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
	mq_args.payload_size = MOSQ_DEFAULT_PAYLOAD_SIZE;
	mq_args.pub_freq = MOSQ_DEFAULT_PUB_FREQ;
	mq_args.num_messages = MOSQ_DEFAULT_NUM_MESSAGES;

	while ((c = getopt(ac, av, "?t:q:d:h:p:s:f:n:")) != -1) {
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
		case 's':
			mq_args.payload_size = atoi (optarg);
			break;
		case 'f':
			mq_args.pub_freq = atoi (optarg);
			break;
		case 'n':
			mq_args.num_messages = atoi(optarg);
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
static void mq_connect_callback(void* obj, int result) {

	if(!result){
		mq_log_info ("Connected!\n");
	}else{
		mq_util_print_error (result);
	}
}


/**
 *
 */
static void mq_disconnect_callback(void* obj) {
	// @@@ TODO
	mq_log_debug ("mq_disconnect_callback");
}


//static void mq_publish_callback(void* obj, uint16_t mid) {
//	// @@ TODO
//	mq_log_debug("mq_publish_callback");
//}

int main (int ac, char** av) {
	char* bname = 0;
	char* client_id = 0;
	struct mosquitto* mosq = 0;
	int result = MOSQ_ERR_SUCCESS;
	int pub_message_count = 0;
	uint16_t pmid = 0; // published message id!
	byte* msg = 0;
	struct timeval t1;

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
	// mosquitto_publish_callback_set(mosq, mq_publish_callback);

	result = mosquitto_connect(mosq, mq_args.host_name, mq_args.port, MOSQ_KEEPALIVE_TIMEOUT, true);
	if (result != MOSQ_ERR_SUCCESS) {
		mq_util_print_error (result);
		goto cleanup;
	}

	mq_message_init (mq_args.payload_size);

	do {
		gettimeofday(&t1, 0);
		msg = mq_message_renew();

		//mq_message_dump (stdout, msg);

		result = mosquitto_publish (mosq,
									&pmid,
									mq_args.topic_name,
									mq_args.payload_size,
									(uint8_t*) msg,
									mq_args.qos,
									0 /*don't retain*/);
		if (result != MOSQ_ERR_SUCCESS) {
			break;
		}

		result = mosquitto_loop(mosq, MOSQ_DEFAULT_LOOP_MSEC);

		usleep (mq_util_sleep_usecs_for_next_request (mq_args.pub_freq, &t1));

		if (result != MOSQ_ERR_SUCCESS) {
			break;
		}
	} while (result == MOSQ_ERR_SUCCESS && ++pub_message_count < mq_args.num_messages);

	if (result != MOSQ_ERR_SUCCESS) {
		mq_util_print_error (result);
	}
	result = mosquitto_publish (mosq,
								&pmid,
								mq_args.topic_name,
								0, // ZERO sized message denotes disconnect to the consumer
								0,
								1, // make sure disconnect of the consumer
								0 /*don't retain*/);
	if (result != MOSQ_ERR_SUCCESS) {
		mq_util_print_error (result);
	}
	/* CLEANUP LABEL*/
	cleanup:

	mq_message_destroy();

	mosquitto_destroy (mosq);
	mosquitto_lib_cleanup();

	mq_log_destroy();

	free (client_id);
	free (bname);
	printf ("Done! (%d)\n", getpid());

	return 0;
}
