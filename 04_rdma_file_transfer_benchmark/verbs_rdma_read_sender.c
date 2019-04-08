#include "common.h"
#include "verbs_common.h"


static void send_message(struct rdma_cm_id *id)
{
  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)id;
  wr.opcode = IBV_WR_SEND;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  sge.addr = (uintptr_t)ctx.send_mr->addr;
  sge.length = ctx.send_mr->length;
  sge.lkey = ctx.send_mr->lkey;

  TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

static void on_pre_conn(struct rdma_cm_id *id)
{
    struct timespec tm1;
    clock_gettime(CLOCK_MONOTONIC, &tm1);
    if (ctx.fd != -1) {
      map_and_register_file();
    } else {
      alloc_and_register_mem();
    }
    uint64_t t = get_elapsed_time_ms(&tm1);

    printf("Mmap and register file/buf of size: %zu at addr: %p took: %ld ms \n", ctx.file_mr->length, ctx.file_mr->addr, t);

    void *send_buffer = memalign(getpagesize(), sizeof(struct file_mr_attr));
    TEST_Z(ctx.send_mr = ibv_reg_mr(rc_get_pd(), send_buffer, sizeof(struct file_mr_attr),
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    struct file_mr_attr mmaped_file = {
        .addr = ctx.file_mr->addr,
        .length = ctx.file_mr->length,
        .rkey = ctx.file_mr->lkey
    };
    memcpy(send_buffer, &mmaped_file, sizeof(struct file_mr_attr));
}

static void on_connection(struct rdma_cm_id *id)
{
  printf("Sender: got connection, sending back my local address and length\n");
  send_message(id);
}

static void on_completion(struct ibv_wc *wc)
{
  printf("Sender: Received event %d\n", wc->opcode); // Sender shouldn't receive any messages.
  fflush(stdout);
}

static void on_disconnect(struct rdma_cm_id *id)
{
    printf("Sender disconnecting and unmapping \n");
    ibv_dereg_mr(ctx.file_mr);
    munmap(ctx.file_mr->addr, ctx.file_mr->length);

    ibv_dereg_mr(ctx.send_mr);
    free(ctx.send_mr->addr);
}

void run_verbs_read_sender()
{
     rc_init(
      on_pre_conn,
      on_connection,
      on_completion,
      on_disconnect);
    printf("waiting for connections. interrupt (^C) to exit.\n");
    rc_server_loop(DEFAULT_PORT, &ctx);
}
