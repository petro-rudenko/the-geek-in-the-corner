#include "common.h"
#include "verbs_common.h"

struct ibv_send_wr wr, *bad_wr = NULL;
struct ibv_sge sge;

static inline void init_iteration(struct rdma_cm_id *id)
{
    ctx.bytes_received = 0;
    ctx.tx_per_iter = 0;
    struct file_mr_attr *server_address = (struct file_mr_attr *)ctx.recv_mr->addr;
    wr.wr.rdma.rkey = server_address->rkey;
    wr.wr.rdma.remote_addr = (uintptr_t)server_address->addr;
    start_time();
}

static inline void stop_benchmark(struct rdma_cm_id *id)
{
    print_iteration_time("READ");
    print_average_bandwith();
    rc_disconnect(id);
}

static inline void read_next_chunk(struct rdma_cm_id *id)
{
  if (ctx.iteration == ctx.num_iters && ctx.bytes_received >= ctx.total_size)
  {
    return stop_benchmark(id);
  }

  if (ctx.bytes_received >= ctx.total_size)
  {
    print_iteration_time("READ");
    ctx.iteration++;
    init_iteration(id);
  }

  sge.length = (ctx.bytes_received + ctx.block_size > ctx.total_size)?(ctx.total_size - ctx.bytes_received):ctx.block_size;

  TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));

  wr.wr.rdma.remote_addr += (uintptr_t)sge.length;
  ctx.bytes_received += sge.length;
  ctx.tx_per_iter++;
}

static inline void start_benchmark(struct rdma_cm_id *id)
{
    printf("Starting benhcmark!\n");
    ctx.iteration = 1;

    sge.lkey = ctx.file_mr->lkey;
    sge.addr = (uintptr_t)ctx.file_mr->addr;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.wr_id = (uintptr_t)id;

    total_bytes_recieved = 0;
    total_time = 0.0;
    peak_bw = 0.0;
    init_iteration(id);
    read_next_chunk(id);
}

static void post_receive(struct rdma_cm_id *id)
{
  struct ibv_recv_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)id;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  sge.addr = (uintptr_t)ctx.recv_mr->addr;
  sge.length = ctx.recv_mr->length;
  sge.lkey = ctx.recv_mr->lkey;

  TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
}


static void on_pre_conn(struct rdma_cm_id *id)
{
    printf("Receiver: On pre connection. Pre register all buffers...\n");
    void *recv_buffer = memalign(getpagesize(), sizeof(struct file_mr_attr));
    TEST_Z(ctx.recv_mr = ibv_reg_mr(rc_get_pd(), recv_buffer, sizeof(struct file_mr_attr),
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    void *recv_file = memalign(getpagesize(), ctx.block_size);
    TEST_Z(ctx.file_mr = ibv_reg_mr(rc_get_pd(), recv_file, ctx.block_size, IBV_ACCESS_LOCAL_WRITE));

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
  } else if (wc->opcode == IBV_WC_RDMA_READ) {
      if (ctx.result_fd != -1 && ctx.iteration == 1) {
        size_t received_length = (ctx.bytes_received % ctx.block_size == 0) ? ctx.block_size:(ctx.bytes_received % ctx.block_size);
        write_to_result_file(ctx.file_mr->addr, received_length);
      }
      read_next_chunk(id);
  } else {
      printf("Receiver: Received event %d", wc->opcode);
  }
}

static void on_disconnect(struct rdma_cm_id *id)
{
    ibv_dereg_mr(ctx.file_mr);
    free(ctx.file_mr->addr);

    ibv_dereg_mr(ctx.recv_mr);
    free(ctx.recv_mr->addr);
}

void run_verbs_read_receiver()
{
    rc_init(
      on_pre_conn,
      NULL,
      on_completion,
      on_disconnect);
    rc_client_loop(ctx.server_address, DEFAULT_PORT, &ctx);
}
