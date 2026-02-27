# MOTION Setup & Usage Guide

## Docker Setup

We run a container and mount the MOTION directory. If the image `mpc-frameworks` already exist we directly launch a container, else we build the image and then launch the container.

```
./setup_docker.sh
```

This script will use the modified `Dockerfile` to build the image with the required packages.

## Setup Instructions

1. Inside the Docker container, navigate to the MOTION directory,
    ```
    cd /workspace/MOTION
    ```

2. Update all the submodules 
    ```
    git submodule update --init --recursive
    ```

## Overview

This guide helps you integrate a **custom benchmark executable** (`benchmark_count`) into the [MOTION MPC framework](https://github.com/sartori-labs/MOTION.git), supporting the following operations:

- **Sum**
- **Count**
- **ReLU**
- **Billionaire**

## What Each Operation Does

| Operation     | Description                                 |
| ------------- | ------------------------------------------- |
| `sum`         | Bitwise XOR sum over Boolean GMW shares     |
| `count`       | Counts 1-bits via parity-based accumulation |
| `relu`        | Computes `max(0,x)` using secure masking    |
| `billionaire` | Secure bitwise comparison between parties   |

---

## Build Instructions

#### Step 1: File Setup (Optional)
1. Create New Benchmark Source File

```bash
mkdir -p src/examples/benchmarks
```

Add the program similar to this `src/examples/my_benchmark/benchmarks_main.cpp`

2. Update `CMakeLists.txt`

Add the following to `src/examples/CMakeLists.txt`

```cmake
add_executable(benchmarks benchmarks/benchmarks_main.cpp)
target_link_libraries(benchmarks PRIVATE MOTION::motion)
```

#### Step 3: Build the Project

```bash
cd MOTION
mkdir build && cd build
cmake -DMOTION_BUILD_EXAMPLES=ON -DMOTION_BUILD_EXE=ON ..
cmake --build . --target benchmarks -j `nproc`
```

---

## Usage Examples

Run from two different terminals, or run the first party in the background, and then run the second party

```bash
./bin/benchmarks 0 0,127.0.0.1,23000 1,127.0.0.1,23001 count 32 1 & 
./bin/benchmarks 1 0,127.0.0.1,23000 1,127.0.0.1,23001 count 32 1
```

---

## Troubleshooting

- If `bind address () is no IP` error: ensure both parties are started within seconds.
- If `core dumped`: check port conflicts or retry with a slight delay between terminals.
- Enable logging with:

```cpp
party->GetConfiguration()->SetLoggingEnabled(true);
```

---

## Credits

Built using the MOTION MPC Framework by ENCRYPTO Group.  
Modified for benchmarking by Akshat Ghoshal, Nishanth Murthy