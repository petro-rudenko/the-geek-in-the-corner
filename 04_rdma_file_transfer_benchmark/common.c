#include "common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void display_usage(char* program)
{
    fprintf(stderr, "Usage: %s [parameters]\n", program);
    fprintf(stderr, "Sender - receiver benchmark utility. Receiver got file by chunks from sender.");
    fprintf(stderr, "\nParameters are:\n");
    fprintf(stderr, "\n-f --file - File to do trasfer from sender.\n");
    fprintf(stderr, "\n-r --result_file - File to write result on receiver.\n");
    fprintf(stderr, "\n-t --total_size - Total size of sender buffer to transfer. default: 10g\n");
    fprintf(stderr, "\n-b --block_size - Receiver block size. default: 1m\n");
    fprintf(stderr, "\n-o --use_odp - Whether to use ODP. default: false\n");
    fprintf(stderr, "\n-s --server_address - Address of sender endpoint to connect.\n");
    fprintf(stderr, "\n-l --bind_address - Sender address to listen.\n");
    fprintf(stderr, "\n-n --num_iters - How many iterations to do.\n");
    fprintf(stderr, "\n-p --protocol - Protocol to use (verbs_read, verbs_write_and_register, verbs_write). default: verbs_read.\n");
    exit(0);
}

void parse_cmd(int argc, char *const argv[])
{
    ctx.total_size = 1024U * 1024U * 1024U * 10U; //10G
    ctx.block_size = 1024U * 1024U;               //1M
    ctx.use_odp = false;
    ctx.server_address = NULL;
    ctx.fd = -1;
    ctx.result_fd = -1;
    ctx.protocol = VERBS_READ;
    ctx.num_iters = 1;
    int c = 0;
    const struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"file", required_argument, NULL, 'f'},
        {"total_size", required_argument, NULL, 't'},
        {"block_size", required_argument, NULL, 'b'},
        {"result_file", required_argument, NULL, 'r'},
        {"use_odp", optional_argument, NULL, 'o'},
        {"server_address", required_argument, NULL, 's'},
        {"bind_address", required_argument, NULL, 'l'},
        {"num_iters", required_argument, NULL, 'n'},
        {"protocol", required_argument, NULL, 'p'},
        {NULL, 0, NULL, 0}};
    int option_index;
    while ((c = getopt_long(argc, argv, "hf:t:b:r:oms:l:n:p:", long_options, &option_index)) != -1)
    {
        switch (c)
        {
            case 'f':
                ctx.fd = open(optarg, O_RDONLY);
                if (ctx.fd == -1)
                {
                    fprintf(stderr, "unable to open input file %s\n", optarg);
                    exit(1);
                }
                break;
            case 'r':
                ctx.result_fd = open(optarg, O_RDWR|O_CREAT|O_TRUNC, (mode_t)0755);
                if (ctx.result_fd == -1)
                {
                    fprintf(stderr, "unable to open result file %s\n", optarg);
                    exit(1);
                }
                break;
            case 't':
                ctx.total_size = atol(optarg);
                break;
            case 'b':
                ;
                unsigned long bs = strtoul(optarg, NULL, 10);
                if (bs > BYTES_IN_GIGABYTE) {
                    fprintf(stderr, "Block size must be < %u bytes \n", BYTES_IN_GIGABYTE);
                    exit(1);
                }
                ctx.block_size = bs;
                break;
            case 'o':
                ctx.use_odp = true;
                break;
            case 'n':
                ctx.num_iters = atoi(optarg);
                break;
            case 's':
                ctx.server_address = optarg;
                break;
            case 'l':
                ctx.bind_address = optarg;
                break;
            case 'p':
                if (strcmp(optarg, "verbs_write_and_register") == 0)
                {
                    ctx.protocol = VERBS_WRITE_AND_REGISTER;
                } else if(strcmp(optarg, "verbs_write") == 0)
                {
                    ctx.protocol = VERBS_WRITE;
                }
                break;
            case 'h':
            case '?':
                    display_usage(argv[0]);
                    break;
        }
    }
}

inline void start_time()
{
    clock_gettime(CLOCK_MONOTONIC, &ctx.start_time);
}

double get_elapsed_time_ms(struct timespec *start_time)
{
    clock_gettime(CLOCK_MONOTONIC, &endtime);
    return (double) 1000UL * (endtime.tv_sec - start_time->tv_sec) + (double) (endtime.tv_nsec - start_time->tv_nsec) / 1000000UL;
}

void print_iteration_time(const char *operation)
{
    double t = get_elapsed_time_ms(&ctx.start_time);
    total_bytes_recieved += ctx.total_size;
    total_time += t;
    double gigabits = (double)ctx.total_size / BYTES_IN_GIGABIT;
    double gigabytes = (double)ctx.total_size / BYTES_IN_GIGABYTE;
    double seconds = t / (double)1000L;
    double gigabits_per_second = gigabits / seconds;
    if (gigabits_per_second > peak_bw) {
        peak_bw = gigabits_per_second;
    }
    printf("Iteration: %d/%d. RDMA %s %.6f GiB (%.6f GB) took: %.4f ms, bandwith: %.4f Gb/s, number of transactions: %lu\n",
           ctx.iteration, ctx.num_iters, operation, gigabits, gigabytes, t, gigabits_per_second, ctx.tx_per_iter);
    start_time();
}

void print_average_bandwith()
{
    double gigabits =  (double) total_bytes_recieved / BYTES_IN_GIGABIT;
    double seconds = total_time / (double)1000L;
    double gigabits_per_second = gigabits / seconds;
    printf("Peak bandwith: %.4f Gb/s, average bandwith: %.4f Gb/s \n", peak_bw, gigabits_per_second);
}


void write_to_result_file(void *addr, size_t length)
{
    FILE* result = fdopen(ctx.result_fd, "w");
    fwrite(addr, length, 1, result);
    fsync(ctx.result_fd);
}