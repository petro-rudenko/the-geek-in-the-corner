#include "common.h"
#include "verbs_common.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <malloc.h>


const int TIMEOUT_IN_MS = 500;

struct context {
  struct ibv_context *ctx;
  struct ibv_pd *pd;
  struct ibv_cq *cq;
  struct ibv_comp_channel *comp_channel;

  pthread_t cq_poller_thread;
};

static struct context *s_ctx = NULL;
static pre_conn_cb_fn s_on_pre_conn_cb = NULL;
static connect_cb_fn s_on_connect_cb = NULL;
static completion_cb_fn s_on_completion_cb = NULL;
static disconnect_cb_fn s_on_disconnect_cb = NULL;

static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
static void event_loop(struct rdma_event_channel *ec, int exit_on_disconnect);
static void * poll_cq(void *);

void map_file()
{
    void *addr = mmap(NULL, ctx.total_size, PROT_READ, MAP_PRIVATE, ctx.fd, 0);
    CHKERR_EXIT(addr == MAP_FAILED, "mmap");
    ctx.file_addr = addr;
}

void map_and_register_file()
{
    printf("Sender: On pre connection.\n Memory maping file...\n");
    int access_flags = IBV_ACCESS_REMOTE_READ;
    if (ctx.use_odp) {
        access_flags |= IBV_ACCESS_ON_DEMAND;
    }

    map_file();
    TEST_Z(ctx.file_mr = ibv_reg_mr(rc_get_pd(), ctx.file_addr, ctx.total_size, access_flags));
}

void alloc_and_register_mem()
{
    printf("Sender: On pre connection.\n Allocating and register buffer ...\n");
    int access_flags = IBV_ACCESS_REMOTE_READ;
    if (ctx.use_odp) {
        access_flags |= IBV_ACCESS_ON_DEMAND;
    }

    void *addr = memalign(4096, ctx.total_size);
    ctx.file_addr = addr;
    TEST_Z(ctx.file_mr = ibv_reg_mr(rc_get_pd(), addr, ctx.total_size, access_flags));
}


void build_connection(struct rdma_cm_id *id)
{
  struct ibv_qp_init_attr qp_attr;

  build_context(id->verbs);
  build_qp_attr(&qp_attr);

  TEST_NZ(rdma_create_qp(id, s_ctx->pd, &qp_attr));
}

void build_context(struct ibv_context *verbs)
{
  if (s_ctx) {
    if (s_ctx->ctx != verbs)
      rc_die("cannot handle events in more than one context.");

    return;
  }

  s_ctx = (struct context *)malloc(sizeof(struct context));

  s_ctx->ctx = verbs;

  TEST_Z(s_ctx->pd = ibv_alloc_pd(s_ctx->ctx));
  TEST_Z(s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx));
  TEST_Z(s_ctx->cq = ibv_create_cq(s_ctx->ctx, 10, NULL, s_ctx->comp_channel, 0)); /* cqe=10 is arbitrary */
  TEST_NZ(ibv_req_notify_cq(s_ctx->cq, 0));

  TEST_NZ(pthread_create(&s_ctx->cq_poller_thread, NULL, poll_cq, NULL));
}

void build_params(struct rdma_conn_param *params)
{
  memset(params, 0, sizeof(*params));

  params->initiator_depth = params->responder_resources = 1;
  params->rnr_retry_count = 7; /* infinite retry */
  params->retry_count = 7;
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr)
{
  memset(qp_attr, 0, sizeof(*qp_attr));

  qp_attr->send_cq = s_ctx->cq;
  qp_attr->recv_cq = s_ctx->cq;
  qp_attr->qp_type = IBV_QPT_RC;

  qp_attr->cap.max_send_wr = 100;
  qp_attr->cap.max_recv_wr = 100;
  qp_attr->cap.max_send_sge = 10;
  qp_attr->cap.max_recv_sge = 1;
}

void event_loop(struct rdma_event_channel *ec, int exit_on_disconnect)
{
  struct rdma_cm_event *event = NULL;
  struct rdma_conn_param cm_params;

  build_params(&cm_params);

  while (rdma_get_cm_event(ec, &event) == 0) {
    struct rdma_cm_event event_copy;

    memcpy(&event_copy, event, sizeof(*event));
    rdma_ack_cm_event(event);

    if (event_copy.event == RDMA_CM_EVENT_ADDR_RESOLVED) {
      build_connection(event_copy.id);

      if (s_on_pre_conn_cb)
        s_on_pre_conn_cb(event_copy.id);

      TEST_NZ(rdma_resolve_route(event_copy.id, TIMEOUT_IN_MS));

    } else if (event_copy.event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
      TEST_NZ(rdma_connect(event_copy.id, &cm_params));

    } else if (event_copy.event == RDMA_CM_EVENT_CONNECT_REQUEST) {
      build_connection(event_copy.id);

      if (s_on_pre_conn_cb)
        s_on_pre_conn_cb(event_copy.id);

      TEST_NZ(rdma_accept(event_copy.id, &cm_params));

    } else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED) {
      if (s_on_connect_cb)
        s_on_connect_cb(event_copy.id);

    } else if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED) {
      rdma_destroy_qp(event_copy.id);

      if (s_on_disconnect_cb)
        s_on_disconnect_cb(event_copy.id);

      rdma_destroy_id(event_copy.id);

      if (exit_on_disconnect)
        break;

    } else {
      fprintf(stderr, "Unknown event %s \n", rdma_event_str(event_copy.event));
      rc_die("unknown event\n");
    }
  }
}

void * poll_cq(void *ct)
{
  struct ibv_wc wcs[1];
  int num_completions, i;
  struct ibv_cq *cq = s_ctx->cq;

  while (1) {
    while ((num_completions = ibv_poll_cq(cq, 1, wcs))) {
      for (i = 0; i < num_completions; i++) {
        if (wcs[i].status == IBV_WC_SUCCESS) {
          s_on_completion_cb(&wcs[i]);
        }
        else {
          fprintf(stderr, "poll_cq: operation: %d - status is not IBV_WC_SUCCESS, but %s\n",
                  wcs[i].opcode, ibv_wc_status_str(wcs[i].status));
          rc_die("poll_cq: status is not IBV_WC_SUCCESS\n");
        }
      }
    }
  }

  return NULL;
}

void rc_init(pre_conn_cb_fn pc, connect_cb_fn conn, completion_cb_fn comp, disconnect_cb_fn disc)
{
  s_on_pre_conn_cb = pc;
  s_on_connect_cb = conn;
  s_on_completion_cb = comp;
  s_on_disconnect_cb = disc;
}

void rc_client_loop(const char *host, const char *port, void *context)
{
  struct addrinfo *addr;
  struct rdma_cm_id *conn = NULL;
  struct rdma_event_channel *ec = NULL;
  struct rdma_conn_param cm_params;

  TEST_NZ(getaddrinfo(host, port, NULL, &addr));

  TEST_Z(ec = rdma_create_event_channel());
  TEST_NZ(rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP));
  TEST_NZ(rdma_resolve_addr(conn, NULL, addr->ai_addr, TIMEOUT_IN_MS));

  freeaddrinfo(addr);

  conn->context = context;

  build_params(&cm_params);

  event_loop(ec, 1); // exit on disconnect

  rdma_destroy_event_channel(ec);
}

void rc_server_loop(const char *port, void *context)
{
  struct sockaddr_in addr;
  struct rdma_cm_id *listener = NULL;
  struct rdma_event_channel *ec = NULL;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(port));

  if (ctx.bind_address)
  {
    addr.sin_addr.s_addr = inet_addr(ctx.bind_address);
  }

  TEST_Z(ec = rdma_create_event_channel());
  TEST_NZ(rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP));
  TEST_NZ(rdma_bind_addr(listener, (struct sockaddr *)&addr));
  TEST_NZ(rdma_listen(listener, 10)); /* backlog=10 is arbitrary */

  listener->context = context;

  event_loop(ec, 1);

  rdma_destroy_id(listener);
  rdma_destroy_event_channel(ec);
}

void rc_disconnect(struct rdma_cm_id *id)
{
  rdma_disconnect(id);
}

void rc_die(const char *reason)
{
  perror(reason);
  exit(EXIT_FAILURE);
}

struct ibv_pd * rc_get_pd()
{
  return s_ctx->pd;
}
