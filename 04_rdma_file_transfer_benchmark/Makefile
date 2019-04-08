.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := rdma_file_transfer

all: ${APPS}

rdma_file_transfer: common.o \
					verbs_common.o \
					verbs_rdma_read_sender.o \
					verbs_rdma_read_receiver.o \
					verbs_rdma_write_sender.o \
					verbs_rdma_write_receiver.o \
					run_benchmark.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
