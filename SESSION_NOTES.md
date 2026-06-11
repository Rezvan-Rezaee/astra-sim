# AstraSim Codebase — Session Handoff Document

cp /home/rezvan_r/.claude/plans/can-you-explain-to-temporal-aurora.md /mnt/c/Users/rezva/Documents/paradox/astra-sim/SESSION_NOTES.md

**Branch:** `network_api_implementation`
**Working directory:** `/mnt/c/Users/rezva/Documents/paradox/astra-sim`
**Session date:** 2026-06-10 / 2026-06-11
**Author:** Rezvan-Rezaee

---

## 1. Project Context

This session was focused on understanding how AstraSim reads and simulates
workloads encoded as Chakra `.et` (execution trace) files, with particular
attention to the all-gather microbenchmark generator scripts, the system-level
collective communication pipeline, and what statistical output is available at
the end of simulation. The user is working on a SystemC-based network frontend
(`build/astra_systemc/`) and uses a **direct (switch-based) topology**.

Recent commits indicate:

- All communication patterns are running
- A switch network API interface has been implemented
- A system config file exists
- Heap corruption was recently debugged

---

## 2. Chakra `.et` File Format

### 2.1 What an `.et` file is

A binary Protocol Buffers file. Contains:

1. One `GlobalMetadata` message (version string, written first)
2. N `Node` messages written sequentially — **one per operation**

**One `.et` file = one rank's entire execution trace.**
For N ranks, there are N files: `workload.0.et`, `workload.1.et`, ..., `workload.N-1.et`.

### 2.2 Node types

| NodeType         | Meaning                                            |
| ---------------- | -------------------------------------------------- |
| `COMP_NODE`      | One GPU or CPU compute operation                   |
| `COMM_COLL_NODE` | One collective (AllReduce, AllGather, etc.)        |
| `COMM_SEND_NODE` | One point-to-point send                            |
| `COMM_RECV_NODE` | One point-to-point receive                         |
| `MEM_LOAD_NODE`  | One remote memory read                             |
| `MEM_STORE_NODE` | One remote memory write                            |
| `METADATA_NODE`  | Communicator group metadata (parsed, not executed) |
| `INVALID_NODE`   | Skipped                                            |

### 2.3 Attributes on a `COMM_COLL_NODE`

Each node carries a generic `repeated AttributeProto attr` list. For a
collective node, the relevant named attributes are:

| Attribute       | Type        | Meaning                                                          |
| --------------- | ----------- | ---------------------------------------------------------------- |
| `comm_size`     | uint64      | Total bytes to communicate                                       |
| `comm_type`     | uint64 enum | ALL_REDUCE=0, ALL_GATHER=2, REDUCE_SCATTER=7, ALL_TO_ALL=6, etc. |
| `involved_dim`  | bool_list   | Which topology dimensions participate                            |
| `comm_priority` | uint32      | Scheduling priority (default 0)                                  |
| `pg_name`       | string      | Process group / communicator identifier                          |

### 2.4 The dependency graph (DAG)

Nodes are **not** executed sequentially. They form a DAG via:

- `ctrl_deps: [id, id, ...]` — control dependencies
- `data_deps: [id, id, ...]` — data dependencies

A node with all parents finished is **free** and can execute. Multiple nodes
can be free simultaneously and run in parallel (subject to hardware slots).

### 2.5 Node ID vs Rank

These are different concepts. Node ID is a unique identifier for a node
**within one rank's DAG**, used for dependency tracking. The rank is determined
by the filename suffix. In multi-node workloads, two different ranks will both
have a node with id=42 — they refer to different operations in separate graphs.

In the `all_gather.py` microbenchmark, node ID happens to equal rank number
because there is exactly one node per rank and the ID counter increments once
per file. This is coincidental.

---

## 3. Microbenchmark Generator Scripts

**Location:** `examples/workload/microbenchmarks/generator_scripts/`

Scripts: `all_gather.py`, `all_reduce.py`, `reduce_scatter.py`, `all_to_all.py`

### 3.1 What `coll_size` does

```python
coll_size_bytes = coll_size * 1024 * 1024   # --coll-size arg (MB) → bytes
node.attr.append(ChakraAttr(name="comm_size", int64_val=coll_size_bytes))
```

`coll_size` affects **exactly one thing**: the `comm_size` attribute on the
single `COMM_COLL_NODE` in each rank's file.

| Aspect                   | Effect of changing `coll_size`           |
| ------------------------ | ---------------------------------------- |
| Number of nodes per file | **None** — always exactly 1              |
| `comm_size` attribute    | **Yes** — linearly proportional          |
| GPU/CPU compute time     | **None** — no `COMP_NODE`s exist         |
| `involved_dim`           | **None** — not set; defaults to all dims |
| Dependencies             | **None** — node has no parents           |

These are **comm-only** traces by design. The log will only show `Wall time`
and `Comm time` (and they will be equal) because there is nothing else.

### 3.2 How `comm_size` propagates through the system

```
.et node: comm_size = C bytes
    │
    ▼  Workload::issue_coll_comm() — Workload.cc:362
comm_size = node->comm_size<uint64_t>()   ← first read of the value
    │
    ▼  Sys::generate_all_gather(comm_size, ...)
    │
    ▼  Sys::determine_chunk_size() — Sys.cc:1060
chunk_size = comm_size / preferred_dataset_splits
streams    = ceil(comm_size / chunk_size)
    │
    ▼  Per stream: CollectivePhase with remain_size = chunk_size
    │              For ALL_GATHER: remain_size grows per dimension
    │              remain_size *= nodes_in_dimension
    │
    ▼  AllToAll / Ring algorithm:
msg_size = remain_size            (ALL_GATHER — no division)
msg_size = remain_size / nodes    (ALL_REDUCE, REDUCE_SCATTER, ALL_TO_ALL)
    │
    ▼  front_end_sim_send(..., msg_size, ...)
    │
    ▼  comm_NI->sim_send(..., msg_size, ...)  ← network backend receives this
```

**Effect of larger `comm_size`:**

- Larger `chunk_size` → larger `msg_size` at the network layer
- More streams if `comm_size / preferred_dataset_splits > 1`
- Longer simulated transfer time → larger `Wall time` / `Comm time`
- Larger `comm_size / wall_time` = reported bandwidth

---

## 4. ETFeeder and ETFeederNode

### 4.1 ETFeeder

Owns and manages one `.et` file for one rank. Responsibilities:

1. **Startup scan** — reads all nodes sequentially, caches byte offsets, builds
   the dependency graph. Does NOT load node attributes yet.
2. **Dependency tracking** — maintains the set of currently free nodes
   (zero unfinished parents).
3. **Node serving** — `getNextIssuableNode()` returns the next free node.
4. **Graph advancement** — `freeChildrenNodes(id)` decrements dependency
   counters on children; promotes any that hit zero to free.

### 4.2 ETFeederNode

A thin wrapper around one deserialized protobuf `Node` message. Created
on-demand via `lookupNode(id)` — the feeder seeks to the cached byte offset,
reads and deserializes the full protobuf, and wraps it. Exposes typed getters:
`node->comm_size<uint64_t>()`, `node->comm_type<uint64_t>()`, `node->runtime()`, etc.

### 4.3 How often the `.et` file is physically read

| Phase      | What is read                        | When                               |
| ---------- | ----------------------------------- | ---------------------------------- |
| Startup    | Node IDs + dependency lists only    | Once, full sequential scan         |
| Issue time | Full node protobuf (all attributes) | Once per node, on-demand via seekg |

---

## 5. Workload Execution Loop (step by step)

For the `all_gather.py` microbenchmark with one node:

1. **`Workload` constructor (Workload.cc:24)** — builds `ETFeeder`, scans file,
   node 0 is immediately free (no dependencies).
2. **`Workload::fire()`** — calls `call(EventType::General, NULL)`.
3. **`issue_dep_free_nodes()` (Workload.cc:132)** — finds node 0 free; calls
   `et_feeder->lookupNode(0)` (second file read, full deserialization);
   checks `hw_resource->is_available()` → true; calls `issue(node)`.
4. **`issue()` (Workload.cc:163)** — marks node in-flight, occupies GPU comm
   slot, records start time, dispatches to `issue_comm()` → `issue_coll_comm()`.
5. **`issue_coll_comm()` (Workload.cc:326)** — reads `comm_size`, `comm_type`,
   defaults `involved_dims` to `[T,T,T,T]` (not set in microbenchmark), calls
   `sys->generate_all_gather(comm_size, ...)`.
6. **`Sys::generate_all_gather()` → `generate_collective()` (Sys.cc:704)** —
   computes `chunk_size`, creates `streams` `StreamBaseline` objects each with
   `CollectivePhase` objects per active dimension, inserts into ready list.
7. **Algorithm runs** (`AllToAll` for direct topology) — fires
   `front_end_sim_send()` with `msg_size` bytes to the network backend.
8. **Network backend completes** — fires `CollectiveCommunicationFinished`
   callback on `Workload`.
9. **`Workload::call()`** — releases GPU comm slot, records end time, calls
   `freeChildrenNodes(0)` (no children), `issue_dep_free_nodes()` (empty free
   set), no ongoing nodes → calls `report()` → simulation ends.

---

## 6. Hardware Components

### 6.1 HardwareResource (workload layer)

Three single-occupancy slots — only one operation of each type per rank at a time:

| Slot                         | Tracks                |
| ---------------------------- | --------------------- |
| `num_in_flight_cpu_ops`      | CPU compute ops       |
| `num_in_flight_gpu_comp_ops` | GPU compute ops       |
| `num_in_flight_gpu_comm_ops` | GPU communication ops |

`is_available()` returns true when the relevant counter == 0.
`occupy()` sets it to 1. `release()` sets it back to 0.

### 6.2 Network components (system layer)

| Name                | What it is                                                                                              |
| ------------------- | ------------------------------------------------------------------------------------------------------- |
| `comm_NI`           | `AstraNetworkAPI*` — the NIC abstraction; boundary between AstraSim and the network backend             |
| `vnet`              | Virtual network ID on each `sim_request` — identifies the injection queue / virtual channel per message |
| `QueueLevelHandler` | Per-dimension queue allocator; assigns `vnet` IDs to streams                                            |
| `SchedulerUnit`     | Tracks active streams per queue; enforces `queue_threshold`                                             |

Flow: `CollectiveAlgorithm` → `Sys::front_end_sim_send()` → `comm_NI->sim_send(msg_size, dst, vnet, ...)` → network backend.

---

## 7. Collective Algorithm Implementations

Config strings used in system JSON (`all-reduce-implementation`, etc.):

| Config string                  | Algorithm            | Step count                    | Notes                                          |
| ------------------------------ | -------------------- | ----------------------------- | ---------------------------------------------- |
| `"ring"`                       | Ring (bidirectional) | `2(N-1)` for AllReduce        | Both directions on separate queues             |
| `"oneRing"`                    | Ring (one direction) | Same                          | Only Clockwise or Anticlockwise                |
| `"direct"` / `"direct4"`       | AllToAll             | `N-1` with window parallelism | Best match for switch topology                 |
| `"oneDirect"` / `"oneDirect4"` | AllToAll (one dir)   | Same                          | Same algorithm, one-direction queue allocation |
| `"halvingDoubling"`            | Halving-Doubling     | `2·log₂(N)`                   | Standard MPI choice for switched networks      |
| `"oneHalvingDoubling"`         | HD (one dir)         | Same                          | —                                              |
| `"doubleBinaryTree"`           | Double Binary Tree   | Tree reduce + broadcast       | AllReduce only                                 |

### 7.1 `direct` vs `oneDirect`

Algorithmically identical at runtime — both instantiate `AllToAll` with the
same window parameter (parsed as `stoi(str.substr(6,5))`). The only difference
is queue direction allocation:

- `direct` uses both Clockwise and Anticlockwise queues simultaneously
- `oneDirect` uses only one direction

For a switch topology this distinction is meaningless (no physical ring).
`"direct"` with no number → `window = -1` = unlimited (all peers at once).

### 7.2 For a switch topology

**Best choices:**

- `"direct"` — sends to all peers simultaneously; most natural for a
  non-blocking switch
- `"halvingDoubling"` — logarithmic steps; better latency at large N

**Poor choice:**

- `"ring"` — artificially serializes traffic through a chain; wastes the switch

### 7.3 Multi-dimensional config

Each collective can use a different algorithm per topology dimension:

```json
{
  "all-reduce-implementation": ["ring", "halvingDoubling"],
  "all-gather-implementation": ["direct", "direct"],
  "reduce-scatter-implementation": ["direct"],
  "all-to-all-implementation": ["halvingDoubling"]
}
```

Array index = dimension index.

---

## 8. Simulation Statistics Output

### 8.1 What is always printed per rank

```
sys[X] finished, <wall_time> cycles, exposed communication <exposed_comm> cycles.

sys[X], Wall time: <ns>
sys[X], GPU time: <ns>         ← only if COMP_NODEs exist
sys[X], Comm time: <ns>        ← only if comm nodes exist
sys[X], Remote mem time: <ns>  ← only if MEM_* nodes exist
sys[X], Total compute-communication overlap: <ns>  ← only if both GPU and comm exist
```

- **Wall time** = `max(end_time across all nodes)`
- **Exposed communication** = `wall_time - gpu_compute_tics`
- **Overlap** = `GPU_time + Comm_time - wall_time`
- Intervals are merged before summing (parallel ops not double-counted)
- All values in **nanoseconds**

For comm-only microbenchmarks (`all_gather.py` etc.): only `Wall time` and
`Comm time` appear, and they are equal.

### 8.2 Optional outputs

| Feature        | Config flag               | Output                                            |
| -------------- | ------------------------- | ------------------------------------------------- |
| Roofline model | `roofline_enabled = true` | Compute/memory utilization %, operation intensity |
| Memory trace   | `track_local_mem = true`  | Perfetto JSON file, viewable at ui.perfetto.dev   |

### 8.3 What is NOT reported by default

- Per-node bandwidth (tracked internally as `comm_size / execution_time` in
  `Statistics::OperatorStatistics` but reporting is commented out)
- Per-layer breakdown (only aggregate totals per node type)

---

## 9. Dead / Vestigial Code

### `comm_scale` parameter

`Sys.hh:285` declares `double comm_scale`. It is:

- Accepted as `--comm-scale` CLI argument in all frontends
- Stored as `this->comm_scale = comm_scale` in the Sys constructor
- **Never read again anywhere in the codebase**

In `build/astra_systemc/main_switch_simulation.cpp:276` it is explicitly
hardcoded back to 1 before being passed to the constructor:

```cpp
comm_scale = 1;
```

Original intent (per comment): "times workload size by this factor
(1 MiB workload default)". The scaling was never wired into the collective
generators. Changing `--comm-scale` has **zero effect** on simulation results.

---

## 10. Key File Reference

| File                                                                                   | Purpose                                           |
| -------------------------------------------------------------------------------------- | ------------------------------------------------- |
| `extern/graph_frontend/chakra/schema/protobuf/et_def.proto`                            | Node/graph schema definition                      |
| `extern/graph_frontend/chakra/src/feeder_v3/et_feeder.h/.cpp`                          | File loading, byte-offset index, dependency graph |
| `extern/graph_frontend/chakra/src/feeder_v3/dependancy_solver.cpp`                     | Free node tracking, finish_node logic             |
| `astra-sim/workload/Workload.cc`                                                       | Main execution loop, node dispatch, comm issue    |
| `astra-sim/workload/HardwareResource.hh/.cc`                                           | CPU/GPU/comm slot tracking                        |
| `astra-sim/workload/Statistics.cc`                                                     | Per-type timing aggregation and report            |
| `astra-sim/system/Sys.cc`                                                              | Collective generation, chunking, stream creation  |
| `astra-sim/system/astraccl/CollectiveImplLookup.cc`                                    | Config string → CollectiveImplType parser         |
| `astra-sim/system/astraccl/native_collectives/collective_algorithm/Ring.cc`            | Ring algorithm, msg_size formulas                 |
| `astra-sim/system/astraccl/native_collectives/collective_algorithm/AllToAll.cc`        | Direct/AllToAll algorithm                         |
| `astra-sim/system/astraccl/native_collectives/collective_algorithm/HalvingDoubling.cc` | HD algorithm                                      |
| `astra-sim/system/astraccl/native_collectives/logical_topology/RingTopology.cc`        | Ring direction, get_sender/receiver               |
| `astra-sim/common/AstraNetworkAPI.hh`                                                  | Network interface abstract base                   |
| `build/astra_systemc/main_switch_simulation.cpp`                                       | SystemC frontend entry point                      |
| `examples/workload/microbenchmarks/generator_scripts/all_gather.py`                    | Microbenchmark .et generator                      |

---

## 11. Exact Step-by-Step: How a `.et` Node is Read and Processed

This traces a single `COMM_COLL_NODE` (ALL_GATHER) from the `all_gather.py`
microbenchmark through the entire system. `comm_size = C bytes`.

---

### Phase 1 — Startup: the initial scan (once, before simulation)

**`Workload` constructor, `Workload.cc:25`:**

```cpp
string workload_filename = et_filename + "." + to_string(sys->id) + ".et";
this->et_feeder = new ETFeeder(workload_filename);
```

Each rank opens its own file (e.g. `all_gather.47.et` for rank 47).

**Inside `ETFeeder` constructor:**

- Full sequential scan of the file
- For each node: reads `id`, `ctrl_deps`, `data_deps` only
- Caches the **byte offset** of each node in the file (for later random access)
- Builds the in-memory dependency graph
- Attributes (`comm_size`, `comm_type`, etc.) are **not read yet**

For the microbenchmark: 1 node, no dependencies → immediately marked **free**.

`comm_size` is not touched during this phase.

---

### Phase 2 — Simulation starts: `Workload::fire()`

```cpp
void Workload::fire() {
    call(EventType::General, NULL);
}
```

Kicks off the main callback handler.

**`Workload::call()` → `issue_dep_free_nodes()` (`Workload.cc:132`):**

```cpp
auto dependancy_free_nodes = dependancy_resolver.get_dependancy_free_nodes();
// → {node_id = 0}

for (const auto node_id : dependancy_free_nodes_set) {
    shared_ptr<ETFeederNode> node = et_feeder->lookupNode(node_id); // ← SECOND READ
    if (hw_resource->is_available(node)) {
        issue(node);
    }
}
```

**`et_feeder->lookupNode(0)` — second read from disk:**
The feeder seeks to the cached byte offset and **fully deserializes the protobuf
Node message** into an `ETFeederNode`. This is when `comm_size`, `comm_type`,
`involved_dim`, etc. are loaded into memory for the first time.

`hw_resource->is_available()`: GPU comm slot == 0 → true. `issue(node)` called.

---

### Phase 3 — Issuing the node: `Workload::issue()` (`Workload.cc:163`)

```cpp
this->et_feeder->getDependancyResolver().take_node(node->id());
// → node 0 marked "in-flight", removed from free set

this->hw_resource->occupy(node);
// → GPU comm slot = 1 (no new comm nodes can start)

stats->record_start(node, Sys::boostedTick());
// → records start timestamp

// node->type() == COMM_COLL_NODE:
issue_comm(node);  // → issue_coll_comm(node)
```

---

### Phase 4 — `issue_coll_comm()` (`Workload.cc:326`): first read of `comm_size`

```cpp
// "involved_dim" not set in microbenchmark → default:
for (int i = 0; i < 4; i++) involved_dims.push_back(true);
// → [true, true, true, true]

CommunicatorGroup* comm_group = extract_comm_group(node); // → nullptr

const auto comm_type = static_cast<ChakraCollectiveCommType>(
    node->comm_type<uint64_t>());          // → ALL_GATHER

const auto comm_size = node->comm_size<uint64_t>(); // → C bytes  ← READ HERE

stats->get_operator_statistics(node->id()).comm_size = comm_size;
// → stored for bandwidth stat at end of simulation

// comm_type == ALL_GATHER:
DataSet* fp = sys->generate_all_gather(
    comm_size,     // ← only value from the .et node
    involved_dims,
    nullptr,       // comm_group
    0,             // comm_priority
    node->id());
```

`comm_size` is now in the system layer. The `.et` file is not read again for
this node.

---

### Phase 5 — `Sys::generate_all_gather()` → `generate_collective()` (`Sys.cc:704`)

**Step 1 — chunk size (`Sys.cc:1060`):**

```cpp
uint64_t chunk_size = determine_chunk_size(size, ComType::All_Gather);
// chunk_size = comm_size / preferred_dataset_splits
// (ALL_GATHER skips the minimum-size guard)
```

**Step 2 — number of streams (`Sys.cc:720`):**

```cpp
int streams = ceil((double)size / chunk_size);
DataSet* dataset = new DataSet(streams);
```

Larger `comm_size` → more streams → more pipeline parallelism.

**Step 3 — dimension loop (`Sys.cc:764`), reversed for ALL_GATHER:**

```cpp
// For each active dimension (topology dim size > 1 AND involved_dims[dim] == true):
pair<int, RingTopology::Direction> queue =
    vLevels->get_next_queue_at_level(dim);   // allocate vnet queue ID

CollectivePhase phase = generate_collective_phase(
    ComType::All_Gather,
    topology->get_basic_topology_at_dimension(dim, ComType::All_Gather),
    remain_size,          // = chunk_size for first dimension
    queue.first,          // queue ID (vnet)
    queue.second,         // direction
    InjectionPolicy::Normal,
    implementation_per_dimension[dim]);

vect.push_back(phase);
remain_size = phase.final_data_size;
// ALL_GATHER: final_data_size = remain_size * nodes_in_dimension
// → data GROWS as it passes through each dimension
```

**Step 4 — stream creation:**

```cpp
StreamBaseline* newStream = new StreamBaseline(this, dataset, stream_id, vect, pri);
insert_into_ready_list(newStream);
```

`comm_size` is now encoded as stream count + per-phase `remain_size` values.

---

### Phase 6 — Stream execution: algorithm calls `sim_send`

Scheduler picks the stream. `AllToAll` (Direct topology) runs:

```cpp
// Ring constructor for ALL_GATHER:
this->msg_size = data_size;                        // = remain_size for this dim
this->final_data_size = data_size * nodes_in_ring; // data grows

// During execution:
snd_req.vnet = this->stream->current_queue_id;
stream->owner->front_end_sim_send(
    0, Sys::dummy_data,
    msg_size,                  // ← derived from comm_size
    UINT8,
    packet->preferred_dest,
    stream->stream_id,
    &snd_req, ...);

// Sys::front_end_sim_send → comm_NI->sim_send(..., msg_size, ...)
```

The network backend receives `msg_size` bytes and simulates the transfer.

**`comm_size` transformation summary:**

| Stage                            | Formula                                             |
| -------------------------------- | --------------------------------------------------- |
| `determine_chunk_size`           | `chunk_size = comm_size / preferred_dataset_splits` |
| `streams`                        | `ceil(comm_size / chunk_size)`                      |
| `remain_size` (dim 0)            | `= chunk_size`                                      |
| `msg_size` ALL_GATHER            | `= remain_size` (no division)                       |
| `msg_size` ALL_REDUCE / RS / A2A | `= remain_size / nodes_in_ring`                     |

---

### Phase 7 — Completion callback

Network backend finishes → fires `CollectiveCommunicationFinished` on `Workload`.

**`Workload::call()`:**

```cpp
hw_resource->release(node);            // GPU comm slot = 0
stats->record_end(node, tick);         // records end timestamp
et_feeder->freeChildrenNodes(node_id); // promotes children to free (none here)
issue_dep_free_nodes();                // checks for next batch (empty)
// no free nodes + no ongoing nodes → report() → simulation ends
```

---

### Summary table: what `comm_size` controls end-to-end

| Stage                   | Effect of larger `comm_size`                               |
| ----------------------- | ---------------------------------------------------------- |
| ETFeeder scan           | No effect (not read)                                       |
| `lookupNode()`          | Larger integer loaded into `ETFeederNode`                  |
| `issue_coll_comm`       | Larger value stored in stats; passed to generator          |
| `determine_chunk_size`  | Larger `chunk_size` (proportional)                         |
| Stream count            | More streams if `comm_size / preferred_dataset_splits > 1` |
| `msg_size` in algorithm | Larger per-message payload                                 |
| Network backend         | Simulates more bytes → longer simulated time               |
| Final stat              | Larger `comm_size / wall_time` → higher reported bandwidth |
