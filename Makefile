# $Id: Makefile 183 2012-02-26 23:20:52Z tufan $
#
# Makefile
#

# where is mosquitto header and library installed?
MOSQUITTO ?= /usr/local

CC=cc 
CFLAGS=-I${MOSQUITTO}/include  -Wall 
LDFLAGS=-L${MOSQUITTO}/lib -lmosquitto 


ifeq ($(shell uname -s), Darwin)
	CPPFLAGS += -DMOSQ_DARWIN
	CFLAGS	+= -Wpointer-arith
	LDFLAGS += -flat_namespace -lsqlite3
	LOG_OBJ = mq_log_syslog.o
endif

ifeq ($(shell uname -s), Linux)
	SQLITE3 = /usr/local
	CPPFLAGS += -DMOSQ_LINUX
	CFLAGS += -I${SQLITE3}/include
	LDFLAGS += -L${SQLITE3}/lib -Bstatic -lsqlite3
	LOG_OBJ = mq_log_syslog.o	
endif

ifeq ($(target),1)
	CFLAGS+= -O3
	LDFLAGS+= -O3
else
	CPPFLAGS += -DMOSQ_DEBUG
	CFLAGS+= -ggdb 
	LDFLAGS+= 
endif

.PHONY: all  clean

all : mqproducer mqconsumer sqconsumer 

mqproducer : mqproducer.o mq_util.o mq_message.o ${LOG_OBJ} 
	${CC} $^ -o $@ ${LDFLAGS}

mqconsumer : mqconsumer.o mq_util.o mq_message.o ${LOG_OBJ} 
	${CC} $^ -o $@ ${LDFLAGS}

sqconsumer : sqconsumer.o mq_util.o mq_message.o ${LOG_OBJ} 
	${CC} $^ -o $@ ${LDFLAGS}


clean : 
	-rm -f *.o	mqproducer mqconsumer sqconsumer

