/**
 * $Id: sqconsumer.c 169 2012-02-21 10:07:29Z tufan $
 *
 * sqconsumer -t <topicname> -q <qos> -d <debuglevel> -h <host> -p <port>
 *            -n <num-topic-types>
 *
 * inserts given topics in memory sqlite db
 * terminates when receives num-topic-types null messages
 *
 */

#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <limits.h>

#include <sqlite3.h>
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

#define MOSQ_LOOP_TIMEOUT 10 // miliseconds

#define MOSQ_DEFAULT_HOST "localhost"
#define MOSQ_DEFAULT_PORT 1883

#define MOSQ_KEEPALIVE_TIMEOUT 10 // seconds

#define MOSQ_SQLITE_DBNAME ":memory:" // in-memory database

typedef struct Args {
	char topic_name[MAX_TOPIC_NAME_LEN];
	char host_name[MAX_HOST_NAME_LEN];
	int port;
	int qos;
	int num_topic_types;
	int debug_level;
} Args;


typedef struct MessageStat {
	int mid;
	struct timeval tx_tv;
	struct timeval rx_tv;
} MessageStat;


// -- file scoped globals (starts w/ mq_)

static Args mq_args;

static sqlite3* mq_db = 0;

static char* mq_create_sql = "CREATE TABLE stats "
                            "   (topic text not null,"
		   	   	   	   	   	"    id integer not null,"
        					"    tx_time_sec  integer not null,"
		   	   	   	   	   	"    tx_time_usec integer not null,"
        					"    rx_time_sec  integer not null,"
		   	   	   	   	   	"    rx_time_usec integer not null,"
		   	   	   	   	   	"    CONSTRAINT pk PRIMARY KEY (topic, id))";

static char* mq_insert_sql = "INSERT INTO stats VALUES (?,?,?,?,?,?)";
static char* mq_select_sql = "SELECT * FROM stats WHERE topic=?";
static char* mq_select_topic_names_sql = "SELECT topic FROM stats GROUP BY topic";


static sqlite3_stmt* mq_insert_stmt = 0;
static sqlite3_stmt* mq_select_stmt = 0;
static sqlite3_stmt* mq_select_topic_names_stmt = 0;

/**
 * print_usage
 */
static void print_usage () {
	fprintf (stderr, "Usage: sqconsumer -t <topicname>\n"
				     "                 [-n <num-topic-types> (1)]\n"
			         "                 [-q <qos> (0-2)]\n"
			         "                 [-d <debuglevel> (0-3)]\n"
			         "                 [-h <broker-host> (localhost)]\n"
			         "                 [-p <broker-port> (1883)]\n"
				     "                 -? (prints out this usage)\n");
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
	mq_args.num_topic_types = 1;

	while ((c = getopt(ac, av, "?t:q:d:h:p:n:")) != -1) {
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
			mq_log_set_debug_level (mq_args.debug_level);
			break;
		case 'h':
			strncpy (mq_args.host_name, optarg, MAX_HOST_NAME_LEN);
			break;
		case 'p':
			mq_args.port = atoi (optarg);
			break;
		case 'n':
			mq_args.num_topic_types = atoi (optarg);
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

	if (mq_args.num_topic_types <= 0) {
		mq_log_warning ("Wrong number of topics (%d). "
						"There must be at east one topic type. Assuming '1'!",
						mq_args.num_topic_types);
		mq_args.num_topic_types = 1;
		return 0;
	}

	mq_log_debug ("'%s', %d, %d", mq_args.topic_name, mq_args.qos, mq_args.debug_level);
	return 0;
}


static int db_init () {

	char *errmsg = 0;

	// create database to collect data
	if( sqlite3_open(MOSQ_SQLITE_DBNAME, &mq_db) != SQLITE_OK ){
		mq_log_error ("Can't open database: %s\n", sqlite3_errmsg(mq_db));
		return -1;
	}
	// create table where topic statistics will be inserted
	if( sqlite3_exec (mq_db, mq_create_sql, 0, 0, &errmsg) != SQLITE_OK ){
		mq_log_error ("Can't create table: %s\n", sqlite3_errmsg(mq_db));
		mq_log_error ("                    %s\n", errmsg);
		sqlite3_free(errmsg);
		return -1;
	}
	if (sqlite3_prepare_v2 (mq_db, mq_insert_sql, -1, &mq_insert_stmt, 0) != SQLITE_OK) {
		mq_log_error ("Can't prepare insert statement: %s\n", sqlite3_errmsg(mq_db));
		return -1;
	}
	if (sqlite3_prepare_v2 (mq_db, mq_select_sql, -1, &mq_select_stmt, 0) != SQLITE_OK) {
		mq_log_error ("Can't prepare select statement: %s\n", sqlite3_errmsg(mq_db));
		return -1;
	}
	return 0;
}


static int db_insert_msg (const struct mosquitto_message* msg) {

	struct timeval rx_time = {0,0};
	struct timeval tx_time = {0,0};
	int mid = 0;
	int rc = 0;

	gettimeofday (&rx_time, 0); // current message rx time

	// insert into db
	mid = mq_message_id ((byte*)msg->payload);
	tx_time =  mq_message_txtime ((byte*)msg->payload);

	rc = sqlite3_bind_text (mq_insert_stmt, 1, msg->topic, -1, 0) == SQLITE_OK &&
		 sqlite3_bind_int (mq_insert_stmt, 2, mid) == SQLITE_OK &&
		 sqlite3_bind_int (mq_insert_stmt, 3, tx_time.tv_sec) == SQLITE_OK &&
		 sqlite3_bind_int (mq_insert_stmt, 4, tx_time.tv_usec) == SQLITE_OK &&
		 sqlite3_bind_int (mq_insert_stmt, 5, rx_time.tv_sec) == SQLITE_OK &&
		 sqlite3_bind_int (mq_insert_stmt, 6, rx_time.tv_usec) == SQLITE_OK;
	if ( ! rc ) {
		mq_log_error ("Can't bind params: %s\n", sqlite3_errmsg(mq_db));
		return -1;
	}
	rc = sqlite3_step (mq_insert_stmt);
	if (rc != SQLITE_DONE) {
		mq_log_error ("Can't insert (%d): %s\n", rc, sqlite3_errmsg(mq_db));
	}
	sqlite3_reset (mq_insert_stmt);

	return 0;
}


/**
 * mq_connect_callback
 */
static void mq_connect_callback(void *obj, int result) {

	if(!result){
		mq_log_info ("Connected!\n");
	}else{
		mq_util_print_error (result);
	}
}


/**
 *
 */
static void mq_disconnect_callback(void *obj) {
	// @@@ TODO
	mq_log_debug ("mq_disconnect_callback");
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

	static int zero_message_count = 0;
	static int message_count = 0;

	struct mosquitto* mosq = (struct mosquitto*) obj;

	mq_log_debug ("mq_on_message_callback");


	if (msg->payloadlen == 0) {
		// this is  a disconnect message!
		zero_message_count++;

		mq_log_info ("Got ZERO payload message (%d)!", zero_message_count);

		if (zero_message_count >= mq_args.num_topic_types) {
			mosquitto_disconnect(mosq);
		}
	} else {

		if ( db_insert_msg (msg) == -1 ) {
			mq_log_error ("Message cannot be inserted into stats db!");
		} else {
			message_count++;
		}
	}
}


int dump_topic_stats (const char* topic_name) {

	int message_count = 0;
	int jitter_count = 0;

	MessageStat current_msg = {0,{0,0},{0,0}};
	MessageStat previous_msg ={0,{0,0},{0,0}};

	long current_rx_dt_usec = 0;
	long current_tx_dt_usec = 0;
	long previous_rx_dt_usec = 0;
	long previous_tx_dt_usec = 0;

	long tx_jitter = 0;
	long rx_jitter = 0;
	long rx_min = LONG_MAX;
	long tx_min = LONG_MAX;
	long rx_max = LONG_MIN;
	long tx_max = LONG_MIN;
	double tx_avg = 0.0;
	double rx_avg = 0.0;

	if (sqlite3_bind_text (mq_select_stmt, 1, topic_name, -1, 0) != SQLITE_OK) {
		mq_log_error ("Cannot bind topic name!");
		return -1;
	}

	while (sqlite3_step(mq_select_stmt) == SQLITE_ROW) {

		current_msg.mid = sqlite3_column_int(mq_select_stmt, 1);
		current_msg.tx_tv.tv_sec =  sqlite3_column_int(mq_select_stmt, 2);
		current_msg.tx_tv.tv_usec =  sqlite3_column_int(mq_select_stmt, 3);
		current_msg.rx_tv.tv_sec =  sqlite3_column_int(mq_select_stmt, 4);
		current_msg.rx_tv.tv_usec =  sqlite3_column_int(mq_select_stmt, 5);

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

			current_rx_dt_usec = mq_util_timeval_diff_usec (current_msg.rx_tv, previous_msg.rx_tv);
			current_tx_dt_usec = mq_util_timeval_diff_usec (current_msg.tx_tv, previous_msg.tx_tv);

			if (message_count > 1) {
				tx_jitter = previous_tx_dt_usec - current_tx_dt_usec;
				rx_jitter = previous_rx_dt_usec - current_rx_dt_usec;

				printf (" %6ld",        tx_jitter);
				printf (" %6ld usec\n", rx_jitter);

				if (rx_jitter < rx_min) rx_min = rx_jitter;
				if (rx_jitter > rx_max) rx_max = rx_jitter;

				if (tx_jitter < tx_min) tx_min = tx_jitter;
				if (tx_jitter > tx_max) tx_max = tx_jitter;

				tx_avg += labs(tx_jitter);
				rx_avg += labs(rx_jitter);

				jitter_count++;
			}

			previous_rx_dt_usec = current_rx_dt_usec;
			previous_tx_dt_usec = current_tx_dt_usec;
		}

		previous_msg = current_msg;

		message_count++;
	}
	tx_avg = tx_avg / jitter_count;
	rx_avg = rx_avg / jitter_count;

	sqlite3_reset(mq_select_stmt);

	printf ("Jitter ------------------------------------------------\n");
	printf ("TX: %d messages, %4ld / %4ld / %6.2f usec\n", message_count, tx_min, tx_max, tx_avg);
	printf ("RX: %d messages, %4ld / %4ld / %6.2f usec\n", message_count, rx_min, rx_max, rx_avg);

	return 0;
}

void dump_stats() {

	const char* topic_name = 0;

	if (sqlite3_prepare_v2 (mq_db, mq_select_topic_names_sql, -1,
			&mq_select_topic_names_stmt, 0) != SQLITE_OK) {
		mq_log_error ("Can't prepare insert statement: %s\n", sqlite3_errmsg(mq_db));
		return;
	}
	while (sqlite3_step(mq_select_topic_names_stmt) == SQLITE_ROW) {
		topic_name = (const char*) sqlite3_column_text(mq_select_topic_names_stmt, 0);
		printf("'%s'\n", topic_name);

		dump_topic_stats (topic_name);
	}
	sqlite3_reset(mq_select_topic_names_stmt);
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

	if ( db_init () == -1 ) goto cleanup;

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

	dump_stats();
	printf ("\n");

	/* CLEANUP LABEL*/
	cleanup:

	if (mq_db) {
		sqlite3_close(mq_db);
		mq_db = 0;
	}
	if (mosq) {
		mosquitto_destroy (mosq);
		mosquitto_lib_cleanup();
	}
	mq_log_destroy();

	free (client_id);
	free (bname);
	printf ("Done! (%d)\n", getpid());

	return 0;
}
