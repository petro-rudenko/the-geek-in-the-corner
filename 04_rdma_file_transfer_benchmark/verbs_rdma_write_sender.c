#include "common.h"
#include "verbs_common.h"
#include <sys/mman.h>

static struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
static struct ibv_sge recv_sge;
struct rpc_message *msg;
static struct ibv_send_wr wr, *bad_wr = NULL;
static struct ibv_sge sge;


static inline void post_receive(struct rdma_cm_id *id)
{
  TEST_NZ(ibv_post_recv(id->qp, &recv_wr, &bad_recv_wr));
}

static void rdma_write_chunk_registered(struct rdma_cm_id *id)
{
  wr.wr.rdma.remote_addr =(uintptr_t) msg->raddr;
  wr.wr.rdma.rkey = msg->rkey;
  sge.addr = (uintptr_t)ctx.file_mr->addr + msg->offset;
  if ((ctx.file_mr->addr + msg->offset + msg->length) > (ctx.file_mr->addr + ctx.file_mr->length))
  {
    sge.length = ctx.file_mr->length - msg->offset;
  } else
  {
    sge.length = msg->length;
  }
  sge.lkey = ctx.file_mr->lkey;
  wr.imm_data = sge.length;
  TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

static void rdma_write_chunk_unregistered(struct rdma_cm_id *id)
{
  uint32_t length;

  if ((ctx.file_addr + msg->offset + msg->length) > (ctx.file_addr + ctx.total_size))
  {
    length = ctx.total_size - msg->offset;
  } else
  {
    length = msg->length;
  }

  TEST_Z(ctx.file_mr = ibv_reg_mr(rc_get_pd(), ctx.file_addr, length, IBV_ACCESS_LOCAL_WRITE));

  wr.wr.rdma.remote_addr =(uintptr_t) msg->raddr;
  wr.wr.rdma.rkey = msg->rkey;

  sge.addr = (uintptr_t)ctx.file_mr->addr;

  sge.lkey = ctx.file_mr->lkey;
  sge.length = length;

  TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}


static void send_message(struct rdma_cm_id *id)
{
  struct ibv_send_wr send_wr, *bad_send_wr = NULL;
  struct ibv_sge send_sge;

  memset(&send_wr, 0, sizeof(send_wr));

  send_wr.wr_id = (uintptr_t)id;
  send_wr.opcode = IBV_WR_SEND;
  send_wr.sg_list = &send_sge;
  send_wr.num_sge = 1;

  send_sge.addr = (uintptr_t)ctx.send_mr->addr;
  send_sge.length = ctx.send_mr->length;
  send_sge.lkey = ctx.send_mr->lkey;

  TEST_NZ(ibv_post_send(id->qp, &send_wr, &bad_send_wr));
}

static void on_pre_conn(struct rdma_cm_id *id)
{
    printf("Sender: On pre connection.\n");
    char *operation;
    struct timespec tm1;
    clock_gettime(CLOCK_MONOTONIC, &tm1);
    if (ctx.fd != -1) {
      printf("Memory mapping file of size %lu \n", ctx.total_size);
      if (ctx.protocol == VERBS_WRITE) {
        map_and_register_file();
        operation = "Mmap and register file";
      } else {
        map_file();
        operation = "MMap file";
      }
    } else {
      printf("Allocating buffer of size %lu \n", ctx.total_size);
      if (ctx.protocol == VERBS_WRITE) {
        alloc_and_register_mem();
        operation = "Malloc and register";
      } else {
        ctx.file_addr = malloc(ctx.total_size);
        operation = "Malloc";
      }
    }

    uint64_t t = get_elapsed_time_ms(&tm1);
    printf("%s of size: %zu at addr: %p took: %ld ms \n", operation, ctx.total_size, ctx.file_addr, t);

    void *send_buffer = memalign(getpagesize(), sizeof(struct file_mr_attr));
    TEST_Z(ctx.send_mr = ibv_reg_mr(rc_get_pd(), send_buffer, sizeof(struct file_mr_attr),
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    struct file_mr_attr mmaped_file = {
        .addr = ctx.file_addr,
        .length = ctx.total_size,
        .rkey = 0
    };
    memcpy(send_buffer, &mmaped_file, sizeof(struct file_mr_attr));

    void *recv_buffer = memalign(getpagesize(), sizeof(struct rpc_message));
    TEST_Z(ctx.recv_mr = ibv_reg_mr(rc_get_pd(), recv_buffer, sizeof(struct rpc_message),
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    msg = (struct rpc_message *)ctx.recv_mr->addr;
}

static void on_connection(struct rdma_cm_id *id)
{
  printf("Sender: got connection, sending back my local address and length\n");
  send_message(id);
  memset(&recv_wr, 0, sizeof(recv_wr));

  recv_wr.wr_id = (uintptr_t)id;
  recv_wr.sg_list = &recv_sge;
  recv_wr.num_sge = 1;

  recv_sge.addr = (uintptr_t)ctx.recv_mr->addr;
  recv_sge.length = ctx.recv_mr->length;
  recv_sge.lkey = ctx.recv_mr->lkey;
  post_receive(id);

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)id;
  wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  wr.send_flags = IBV_SEND_SIGNALED;

  wr.sg_list = &sge;
  wr.num_sge = 1;
}

static void on_completion(struct ibv_wc *wc)
{
  struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc->wr_id);
  if (wc->opcode == IBV_WC_RECV) {
    if (ctx.protocol == VERBS_WRITE_AND_REGISTER) {
      rdma_write_chunk_unregistered(id);
    } else {
      rdma_write_chunk_registered(id);
    }
    post_receive(id);
  } else if (wc->opcode == IBV_WC_RDMA_WRITE) {
    if (ctx.protocol == VERBS_WRITE_AND_REGISTER) {
      ibv_dereg_mr(ctx.file_mr);
    }
  }
}

static void on_disconnect(struct rdma_cm_id *id)
{
    printf("Sender disconnecting and unmapping \n");
    ibv_dereg_mr(ctx.file_mr);
    munmap(ctx.file_mr->addr, ctx.file_mr->length);

    ibv_dereg_mr(ctx.send_mr);
    free(ctx.send_mr->addr);
}

void run_verbs_write_sender()
{
     rc_init(
      on_pre_conn,
      on_connection,
      on_completion,
      on_disconnect);
    printf("waiting for connections. interrupt (^C) to exit.\n");
    rc_server_loop(DEFAULT_PORT, &ctx);
}
