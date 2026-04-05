# Distributed Task Execution System

A C-based "Publisher-Subscriber" or broker model where a central Server routes compiled C executable tasks from Clients to Workers for remote execution. The system uses standard TCP sockets for network communication and C POSIX threads (pthreads) on the server to handle multiple connections concurrently.

## Architecture & Lifecycle

The lifecycle of a task follows a straightforward pull-based model for execution:

```mermaid
graph LR
    subgraph Clients
        C1[Client]
        C2[Client]
        C3[Client]
    end

    subgraph Server["Server.c"]
        subgraph MQ["Message Queue"]
            T1[task]
            T2[task]
            T3[task]
        end
        LB{Load balancer}
    end

    subgraph Workers
        W1((Worker))
        W2((worker))
        W3((worker))
        W4((worker))
    end

    subgraph FQ["Free Queue"]
        B1[ ]
        B2[ ]
        B3[ ]
    end

    %% Connections
    C1 & C2 & C3 --> MQ
    MQ --> LB
    LB --> W1 & W2 & W3 & W4

    %% Styling
    style Server fill:#333,stroke:#ccc,color:#fff
    style MQ fill:#432,stroke:#d62,color:#f88
    style LB fill:#242,stroke:#8c8,color:#8f8
    style FQ fill:#333,stroke:#ccc,color:#fff
    style B1 fill:#144,stroke:#48c
    style B2 fill:#144,stroke:#48c
    style B3 fill:#144,stroke:#48c
    style W1 fill:#144,stroke:#48c,color:#fff
    style W2 fill:#144,stroke:#48c,color:#fff
    style W3 fill:#144,stroke:#48c,color:#fff
    style W4 fill:#144,stroke:#48c,color:#fff
    style C1 fill:#144,stroke:#48c,color:#fff
    style C2 fill:#144,stroke:#48c,color:#fff
    style C3 fill:#144,stroke:#48c,color:#fff
```

## System Components

1. **Server (`server.c`)**: Acts as a broker. It receives incoming code executables, places them in a thread-safe Queue (using `pthread_mutex` and `pthread_cond`), and dispatches them to available workers.
2. **Worker (`worker.c`)**: Runs on edge nodes. Workers maintain connections to the server, pull tasks, execute them safely on disk, and pipe the output strictly back to the Server.
3. **Client (`client.c`)**: Compiles `task.c` behind the scenes, submits its binary payload, and waits synchronously on its socket until the final standard output is echoed back.

## Setup and Build

The system uses standard `gcc` with POSIX threads. A simple `Makefile` is provided.

```bash
# Compile server, worker, and client
make

# Clean compiled binaries and temporary worker dumps
make clean
```

## Usage

You can deploy these pieces together locally (or to separate servers by configuring network IP addresses):

**1. Start the Server:**
```bash
./server
```

**2. Start one or more Workers:**
*(Open in a separate terminal. You can run as many workers as you want)*
```bash
./worker  <IP>
```

**3. Submit a task:**
*(Ensure you have a `task.c` file written locally)*
```bash
./client <IP>
```

## Protocol Overview

Network communication uses explicit handshakes to ensure boundaries are maintained. 

**Client Submits:** `SUBMIT (1)` -> `File Size` -> `Binary Data`
**Worker Requests:** `REQUEST_TASK (2)` <- `File Size` <- `Binary Data`
**Worker Completes:** `Worker ID` -> `Output Size` -> `Output Data` (Server proxies this back to Client)
