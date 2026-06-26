# DEIChain

DEIChain is a concurrency-focused blockchain simulation developed for the Operating Systems course. The goal of the project is to model, in a simplified way, the essential mechanisms of a distributed blockchain with special attention to processes, threads, inter-process communication, synchronization, and controlled shutdown.

## Purpose

The application simulates the basic blockchain workflow:

1. Transaction generation.
2. Grouping transactions into blocks by multiple miners.
3. Validation of blocks by a validator process.
4. Recording validated blocks in a shared ledger.
5. Collecting and printing statistics during execution and at shutdown.

The project is designed to highlight concepts such as shared memory, semaphores, mutexes, named pipes, message queues, and signal handling.

## Architecture

The system is organized into several cooperating components:

- **Controller**: reads the configuration, initializes the IPCs, and creates the main processes.
- **Miner**: process that creates a pool of mining threads, reads transactions from the pool, performs a simplified PoW, and sends blocks to the validator.
- **Validator**: validates received blocks, updates the blockchain in shared memory, and sends results to the statistics process.
- **Statistics**: receives messages from the validator and displays execution metrics.
- **TxGen**: user-launched external process that produces transactions at regular intervals.

The main communication and synchronization mechanisms are:

- **Shared Memory** for the Transaction Pool.
- **Shared Memory** for the Blockchain Ledger.
- **Named Pipe** `VALIDATOR_INPUT` for sending blocks from Miner to Validator.
- **Message Queue** for communication between Validator and Statistics.
- **Semaphores and mutexes** to protect concurrent access to shared structures.

## Project Structure

- `src/Controller.c` - system startup and process coordination.
- `src/TxGen.c` - transaction generator.
- `src/handler.c` - helper functions, IPCs, logging, hashing, statistics, and shared structures.
- `include/handler.h` - definitions, structures, and prototypes.
- `config.cfg` - simulation parameters.
- `DEIChain_log.txt` - event log file.
- `Makefile` - build configuration for the executables.

## Requirements

To build and run the project, you need:

- GCC or a compatible C compiler.
- POSIX threads (`pthread`).
- Unix IPC support: shared memory, semaphores, message queues, and FIFOs.
- OpenSSL for SHA-256.

## Build

The project is built with `make`.

```bash
make
```

If you are building from a different directory than the one configured in the `Makefile`, adjust the paths defined in `SRC_DIR`, `OBJ_DIR`, and `BIN_DIR` first.

To remove build artifacts:

```bash
make clean
```

## Run

1. Build the project.
2. Start the main system process:

```bash
./bin/controller
```

3. In parallel, start one or more transaction generators:

```bash
./bin/TxGen <reward> <sleep_time_ms>
```

### `TxGen` parameters

- `reward`: value from `1` to `3`.
- `sleep_time_ms`: delay between transactions, from `200` to `3000` milliseconds.

Example:

```bash
./bin/TxGen 2 500
```

## Configuration

The `config.cfg` file contains the simulation parameters, one per line:

```text
NUM_MINERS
POOL_SIZE
TRANSACTIONS_PER_BLOCK
BLOCKCHAIN_BLOCKS
TRANSACTION_POOL_SIZE (default value of 10000)
```

Current example:

```text
5
50
10
50000
```

## Logging

All relevant messages are written both to the screen and to `DEIChain_log.txt`. The log includes, among other events:

- system start and shutdown;
- process and thread creation;
- runtime errors;
- validated or rejected blocks;
- received signals;
- resource cleanup.

## Supported Signals

- `SIGINT`: starts the controlled shutdown of the system.
- `SIGUSR1`: requests Statistics to print the current statistics.

## Cleanup and Shutdown

The simulation is shut down in a controlled way, releasing the resources in use:

- removal of shared memory segments;
- closing and unlinking of the named FIFO;
- removal of the message queue;
- release of auxiliary structures and closing of the log file.

## Notes

- The project is intended to demonstrate concurrency and synchronization, so correct use of IPCs is central to its operation.
- The configuration file and the paths defined in the `Makefile` must match the environment where the project is built.

## Authors

- José Miguel Luís Antunes - 2023211288
- André Jorge Balula Leão - 2023210870
