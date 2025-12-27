# MOTION - A Framework for Mixed-Protocol Multi-Party Computation [![Build Status](https://travis-ci.org/encryptogroup/MOTION.svg?branch=master)](https://travis-ci.org/encryptogroup/MOTION)

Check out the [paper](https://ia.cr/2020/1137) (published at ACM TOPS'22) for details.

This code is provided as an experimental implementation for testing purposes and should not be used in a productive environment. We cannot guarantee security and correctness.


## Docker Setup

We run a container and mount the MOTION directory. If the image `mpc-frameworks` already exist we directly launch a container, else we build the image and then launch the container.

```
./setup_docker.sh
```

This script will use the modified `Dockerfile` to build the image with the required packages.

## Setup & Build Instructions

1. Inside the Docker container, navigate to the MOTION directory,
    ```
    cd /workspace/MOTION
    ```

2. Although the submodules are already updated, it is a good practice to check for updates on all the submodules 
    ```
    git submodule update --init --recursive
    ```
3. Create and enter the build directory:

    ```bash
    mkdir build && cd build
    ```
## Building MOTION

By default, MOTION does **not** build executables or test cases. These components must be explicitly enabled using CMake options.

##### Build Options

- `MOTION_BUILD_EXE` — Build MOTION executables  
- `MOTION_BUILD_TESTS` — Build MOTION test cases  
- `MOTION_BUILD_EXAMPLES` — Build example applications  

##### Basic Configuration

To configure MOTION with executables and examples enabled:

```bash
cmake .. -DMOTION_BUILD_EXAMPLES=ON -DMOTION_BUILD_EXE=ON
```

##### Selecting the Build Type

You can choose the build type, e.g. Release or Debug using [CMAKE_BUILD_TYPE](https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html):

```bash
cmake .. -DCMAKE_BUILD_TYPE=<Release|Debug>
```

* **Release (default)**: Enables compiler optimizations for maximum performance.

* **Debug**: Includes debug symbols and disables optimizations, useful for development and debugging.

##### Choosing a Compiler
To use a non-default C++ compiler, set the CXX environment variable before running CMake.
For example, to build with clang++:

```bash
CXX=/usr/bin/clang++ cmake ..
```

###### Cleaning the Build Directory

Executing `make clean` in the build directory removes all build artifacts.
This includes built dependencies and examples.
To clean only parts of the build, either invoke `make clean` in the specific
subdirectory or use `make -C`:

* `make clean` - clean everything
* `make -C src/motioncore clean` - clean only the MOTION library
* `make -C src/examples clean` - clean only the examples
* `make -C src/test clean` - clean only the test application
* `make -C extern clean` - clean only the built dependencies

#### Developer Guide and Documentation

This guide provides step-by-step instructions on how to integrate and build a **custom benchmarks executable** (`benchmarks`) into the [MOTION MPC framework](https://github.com/sartori-labs/MOTION.git), supporting the following operations:

- **Sum**
- **Count**
- **ReLU**
- **Billionaire**

#### What Each Operation Does

| Operation     | Description                                 |
| ------------- | ------------------------------------------- |
| `sum`         | Bitwise XOR sum over Boolean GMW shares     |
| `count`       | Counts 1-bits via parity-based accumulation |
| `relu`        | Computes `max(0,x)` using secure masking    |
| `billionaire` | Secure bitwise comparison between parties   |

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

## Build the Application/Benchmark
```bash
cmake --build . --target benchmarks -j `nproc`
```
---

## Usage

To run the application on the MOTION framework, we either use two different terminals, or run the first party in the background, and then run the second party

```bash
./bin/benchmark <praty_id> 0,<ip_assigned_party_0> 1,<ip_assigned_party_1> <sum|count|relu|billionaire> <vector_size> <iteration> &
./bin/benchmark <praty_id> 0,<ip_assigned_party_0> 1,<ip_assigned_party_1> <sum|count|relu|billionaire> <vector_size> <iteration>
```
#### Example

To run the `count` benchmark with an input size of `32` we use the following command.
```bash
./bin/benchmarks 0 0,127.0.0.1,23000 1,127.0.0.1,23001 count 32 1 & 
./bin/benchmarks 1 0,127.0.0.1,23000 1,127.0.0.1,23001 count 32 1
```

**Note**: the IP adressess have to match on both the invocations, and must not be same for both the parties.

---



##### Detailed Guide

###### External Dependencies

MOTION depends on the following libraries:
* [boost](https://www.boost.org/)
* [flatbuffers](https://github.com/google/flatbuffers)
* [fmt](https://github.com/fmtlib/fmt)
* [googletest](https://github.com/google/googletest) (optional)

These are referenced using the Git submodules in the `extern/`
directory.
During configure phase of the build (calling `cmake ..`) CMake searches your
system for these libraries.

* If they are already installed at a standard location, e.g., at `/usr` or
  `/usr/local`, CMake should find these automatically.
* In case they are installed at a nonstandard location, e.g., at `~/some/path/`,
  you can point CMake to their location via the
  [`CMAKE_PREFIX_PATH`](https://cmake.org/cmake/help/latest/variable/CMAKE_PREFIX_PATH.html)
  option:
    ```
    cmake .. -DCMAKE_PREFIX_PATH=~/some/path/
    ```
* Otherwise, CMake updates and initializes the Git submodules in `extern/` (if
  not already done), and the missing dependencies are built together with MOTION.
  If you want to do this without a network connection, consider to clone the
  repository recursively.




#### Running Applications
  Adding `-DMOTION_BUILD_EXE=On` to the `cmake` command enables the compilation of the applications implemented in 
  MOTION. Currently, the following applications are implemented and can be found in `src/examples/`:
  * AES-128 encryption
  * SHA-256 hashing
  * millionaires' problem: each party has an integer input (amount of money), the protocol yields the index of the party 
  with the largest input (i.e., the richest party)
  * **TODO (in work, cleanup):** All the applications implemented in [HyCC](https://gitlab.com/securityengineering/HyCC) 
  via our HyCC adapter
    
Three other examples with a detailed `README` can be found in `src/examples/tutorial/` :
  * Crosstabs
  * Inner Product
  * Multiply 3: multiply three real inputs from three parties or three shared inputs from two parties.

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
