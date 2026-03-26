Ultra-Low Latency Limit Order Book (LOB)
**Author:** Anish Basety



## Architecture Overview
This project implements a deterministic, ultra-low latency matching engine in C++20. Designed for high-frequency trading (HFT) environments, the architecture completely avoids dynamic memory allocation on the critical path, ensuring consistent microsecond-level execution times and zero garbage collection/OS-level context switching latency.

## Key Optimizations
* **Zero Critical-Path Allocation:** All `Order` objects are instantiated at startup using a custom contiguous memory pool (free-list).
* **$\mathcal{O}(1)$ Time Complexity:** * Order insertions and cancellations execute in $O(1)$ time via an intrusive doubly-linked list.
  * Price level lookups execute in $O(1)$ time using a statically allocated array mapped to tick sizes.
* **Cache-Aligned Layouts:** Data structures are padded to 64 bytes (`alignas(64)`) to align with standard L1/L2 cache lines, completely eliminating false sharing across multi-threaded operations.

## Build & Test Instructions
Built with CMake and tested rigorously with Catch2 to validate Price/Time priority and partial fills.
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./tests/lob_tests
Benchmarks
Tested on an Intel Core i9 (isolated cores).

Order Insertion: ~45 ns

Order Cancellation: ~30 ns

Matching (Limit vs Market): ~85 ns

