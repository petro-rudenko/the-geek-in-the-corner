#ifndef COMMON_H
#define COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#define CHKERR_EXIT(_cond, _msg)                    \
do {                                                \
    if (_cond) {                                    \
        perror("Failed to " _msg "\n");    \
        exit(EXIT_FAILURE);                         \
    }                                               \
} while (0)

#define TEST_NZ(x) do { if ( (x)) { perror("error: " #x " failed (returned non-zero)." ); exit(EXIT_FAILURE); }} while (0)
#define TEST_Z(x)  do { if (!(x)) { perror("error: " #x " failed (returned zero/null)."); exit(EXIT_FAILURE); }} while (0)

#define DEFAULT_PORT "12345"

#define BYTES_IN_GIGABIT  (1000u * 1000 * 1000 / 8)
#define BYTES_IN_GIGABYTE 1073741824U

struct file_mr_attr
{
    void *addr;
    size_t length;
    uint32_t rkey;
};

typedef enum benchmark_protocols
{
    VERBS_READ,
    VERBS_WRITE_AND_REGISTER,
    VERBS_WRITE
} BENCHMARK_PROTOCOLS;

struct context_params
{
    struct ibv_mr *file_mr;
    void *file_addr;
    struct ibv_mr *recv_mr;
    struct ibv_mr *send_mr;

    int iteration;
    uint64_t tx_per_iter;

    int fd;                            // File descriptor of file for mmap.
    int result_fd;                    // File descriptor to write result.
    struct timespec start_time;        // Start benchmark time;
    size_t bytes_received;
                                       // Command line arguments
    size_t total_size;                 // Total file size to do mmap;
    uint32_t block_size;               // Block size for MR;
    bool use_odp;                      // Whether to use ODP;
    int num_iters;                     // Number of loop iterations for benchmark;
    char *server_address;              // Server address for client to connect;
    char *bind_address;                // Bind address for server to bind.
    BENCHMARK_PROTOCOLS protocol;
};

struct context_params ctx;
void parse_cmd(int argc, char * const argv[]);

size_t total_bytes_recieved;
double total_time;
double peak_bw;

struct timespec endtime;

void start_time();
double get_elapsed_time_ms(struct timespec *start_time);
void print_iteration_time(const char *operation);
void print_average_bandwith();

void write_to_result_file(void *addr, size_t length);
#endif
