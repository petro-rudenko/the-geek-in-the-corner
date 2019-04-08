#include "common.h"
#include "verbs_common.h"


static struct rpc_message *rpc;
static struct ibv_recv_wr wr_recv, *bad_wr_recv = NULL;
static struct ibv_sge sge_recv;
static struct ibv_send_wr wr, *bad_wr = NULL;
static struct ibv_sge sge;


static inline void post_receive(struct rdma_cm_id *id)
{
  TEST_NZ(ibv_post_recv(id->qp, &wr_recv, &bad_wr_recv));
}

static inline void stop_benchmark(struct rdma_cm_id *id)
{
    print_iteration_time("WRITE");
    print_average_bandwith();
    rc_disconnect(id);
}

static void read_next_chunk(struct rdma_cm_id *id)
{
  struct file_mr_attr *server_address = (struct file_mr_attr *)ctx.recv_mr->addr;
  if (ctx.iteration == ctx.num_iters && ctx.bytes_received >= server_address->length)
  {
    return stop_benchmark(id);
  }

  if (ctx.bytes_received >= server_address->length)
  {
    print_iteration_time("WRITE");
    ctx.iteration++;
    ctx.bytes_received = 0;
    ctx.tx_per_iter = 0;
  }

  rpc->offset = ctx.bytes_received;
  TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
  ctx.tx_per_iter++;
}

static inline void start_benchmark(struct rdma_cm_id *id)
{
    printf("Starting benhcmark!\n");
    ctx.iteration = 1;
    ctx.tx_per_iter = 0;

    rpc = (struct rpc_message*)ctx.send_mr->addr;
    rpc->raddr = ctx.file_mr->addr;
    rpc->length = ctx.file_mr->length;
    rpc->rkey = ctx.file_mr->rkey;

    wr.wr_id = (uintptr_t)id;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t)ctx.send_mr->addr;
    sge.length = ctx.send_mr->length;
    sge.lkey = ctx.send_mr->lkey;

    total_bytes_recieved = 0;
    total_time = 0.0;
    peak_bw = 0.0;
    start_time();
    read_next_chunk(id);
}

static void on_pre_conn(struct rdma_cm_id *id)
{
    printf("Receiver: On pre connection.\n");
    void *recv_buffer = memalign(getpagesize(), sizeof(struct file_mr_attr));
    TEST_Z(ctx.recv_mr = ibv_reg_mr(rc_get_pd(), recv_buffer, sizeof(struct file_mr_attr),
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    void *recv_file = memalign(getpagesize(), ctx.block_size);
    TEST_Z(ctx.file_mr = ibv_reg_mr(rc_get_pd(), recv_file, ctx.block_size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    void *send_mr = memalign(getpagesize(), sizeof(struct rpc_message));
    TEST_Z(ctx.send_mr = ibv_reg_mr(rc_get_pd(), send_mr, sizeof(struct rpc_message),
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    wr_recv.wr_id = (uintptr_t)id;
    wr_recv.sg_list = &sge_recv;
    wr_recv.num_sge = 1;

    sge_recv.addr = (uintptr_t)ctx.recv_mr->addr;
    sge_recv.length = ctx.recv_mr->length;
    sge_recv.lkey = ctx.recv_mr->lkey;

    post_receive(id);
}

static void on_completion(struct ibv_wc *wc)
{
  struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc->wr_id);
  if (wc->opcode == IBV_WC_RECV) {
      struct file_mr_attr *server_address = (struct file_mr_attr *)ctx.recv_mr->addr;
      printf("Receiver: Received message. Will read %zu bytes from server at address %p \n",
      server_address->length, server_address->addr);
      ctx.total_size = server_address->length;
      start_benchmark(id);
  } else if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
      ctx.bytes_received += ctx.block_size;
      if (ctx.result_fd != -1 && ctx.iteration == 1) {
        write_to_result_file(ctx.file_mr->addr, wc->imm_data);
      }
      read_next_chunk(id);
  } else if(wc->opcode == IBV_WC_SEND) {
      post_receive(id);
  }
}

static void on_disconnect(struct rdma_cm_id *id)
{
    ibv_dereg_mr(ctx.file_mr);
    free(ctx.file_mr->addr);

    ibv_dereg_mr(ctx.recv_mr);
    free(ctx.recv_mr->addr);
}

void run_verbs_write_receiver()
{
    rc_init(
      on_pre_conn,
      NULL,
      on_completion,
      on_disconnect);
    rc_client_loop(ctx.server_address, DEFAULT_PORT, &ctx);
}
