# RDMA file transfer benchmark to simulate [SparkRDMA](https://github.com/Mellanox/SparkRDMA) workload

The goal is to transfer buffer of size `-t --total_size` from sender to receiver. The receiver accepts to it's receiver buffer of size `-b --block_size`, that usually less then total size.

Other parameters:
```
-f --file - File to do mmap.

-t --total_size - Total size of sender buffer to transfer. default: 10g

-b --block_size - Receiver block size. default: 1m

-o --use_odp - Whether to use ODP. default: false

-s --server_address - Address of sender endpoint to connect.

-l --bind_address - Sender address to listen.

-n --num_iters - How many iterations to do.

-p --protocol - Protocol to use (verbs_read, verbs_write_and_register, verbs_write). default: verbs_read.
```