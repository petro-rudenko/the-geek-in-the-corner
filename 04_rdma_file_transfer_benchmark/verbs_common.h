#ifndef VERBS_COMMON_H
#define VERBS_COMMON_H

#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <sys/mman.h>

#define DEFAULT_PORT "12345"

typedef void (*pre_conn_cb_fn)(struct rdma_cm_id *id);
typedef void (*connect_cb_fn)(struct rdma_cm_id *id);
typedef void (*completion_cb_fn)(struct ibv_wc *wc);
typedef void (*disconnect_cb_fn)(struct rdma_cm_id *id);

void rc_init(pre_conn_cb_fn, connect_cb_fn, completion_cb_fn, disconnect_cb_fn);
void rc_client_loop(const char *host, const char *port, void *context);
void rc_disconnect(struct rdma_cm_id *id);
void rc_die(const char *message);
struct ibv_pd * rc_get_pd();
void rc_server_loop(const char *port, void *context);

void map_file();
void map_and_register_file();
void alloc_mem();
void alloc_and_register_mem();

void run_verbs_read_sender();
void run_verbs_read_receiver();

void run_verbs_write_sender();
void run_verbs_write_receiver();

/**
 * @brief RPC message for RDMA write protocol.
 * Receiver send to sender to do RDMA write block of size @length at @offset to sender's @raddr.
 *
 */
struct rpc_message
{
    size_t offset;
    size_t length;
    void *raddr;
    uint32_t rkey;
};

#endif
