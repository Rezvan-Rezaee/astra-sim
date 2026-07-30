# How ASTRA-sim calculates compute time (roofline model)

This document explains, at the code level, how ASTRA-sim determines the elapsed time of a
single compute node when the roofline model is enabled (`"roofline-enabled": 1` in the
system config).

## The trigger: a compute node in the workload trace

When ASTRA-sim replays a workload trace (a Chakra execution graph) and hits a compute node,
it calls `Workload::issue_comp(node)` (`astra-sim/workload/Workload.cc:255-305`). This is the
function that decides how long that operation takes — it produces the node's `elapsed_time`.
This only runs in roofline mode; it's one of several ways ASTRA-sim can model compute
duration (the others use durations recorded directly in the trace).

## The two input numbers: `num_ops` and `tensor_size`

```cpp
double num_ops = static_cast<double>(node->num_ops<uint64_t>());
double tensor_size = static_cast<double>(node->tensor_size<uint64_t>());
```

- **`num_ops`** — compute requirement: total FLOPs the op performs.
- **`tensor_size`** — memory requirement: total bytes the op reads/writes (its inputs +
  outputs).

These are **not computed by ASTRA-sim**. They're read as attributes already present on the
trace node (`node->num_ops<uint64_t>()` looks up an attribute literally named `"num_ops"` on
the Chakra protobuf node; `extern/graph_frontend/chakra/src/feeder_v3/et_feeder_node_attr.h`
registers both via `REGISTER_ATTR(num_ops)` / `REGISTER_ATTR(tensor_size)`). Whatever tool
produced the execution trace (e.g. a PARAM/Chakra trace generator that profiled a real
PyTorch model) must have already computed and embedded these two numbers per op.

If `tensor_size` is 0 the node is treated as invalid and skipped (division-by-zero guard).

### Are they dependent on each other?

Not directly — the code treats them as two independently-supplied attributes; neither is
derived from the other. But they're correlated in a shape-dependent way, since both are
functions of the same operation's tensor shapes/dtypes, just via different formulas. Take a
matmul `(M×K) @ (K×N) → (M×N)` in fp16:

- `num_ops ≈ 2·M·N·K` (multiply-add per output element)
- `tensor_size ≈ (M·K + K·N + M·N) · 2 bytes` (bytes of the three tensors)

Both grow with M, N, K, but at different rates — `num_ops` scales roughly cubically while
`tensor_size` scales roughly quadratically. That divergence is exactly why their ratio
varies by op: a large matmul has a high ratio (compute-bound), while an elementwise op like
a bias-add has `num_ops` roughly proportional to `tensor_size` (ratio near 1,
memory-bound). Together, the pair *is* the compute+memory requirement of the node; feeding
both into the model (rather than either alone) is what lets the roofline formula tell
compute-bound ops apart from memory-bound ones.

## Operational intensity — how "compute-heavy" the op is per byte moved

```cpp
double operational_intensity = num_ops / tensor_size;
```

FLOPs per byte. High → the op does a lot of math per byte fetched (compute-heavy, e.g. a
large matmul). Low → the op mostly moves memory relative to how much math it does
(memory-heavy, e.g. an elementwise op).

## Achieved performance — the roofline `min()`

`astra-sim/system/Roofline.cc`:
```cpp
double Roofline::get_perf(double operational_intensity) {
    return min(bandwidth * operational_intensity, peak_perf);
}
```
called as:
```cpp
double perf = sys->roofline->get_perf(operational_intensity);
```

Two hardware constants, both read from the **system config JSON** at startup (not the
trace), in `astra-sim/system/Sys.cc:398-411`:

- **`peak_perf`** — hardware peak compute throughput (FLOP/s), from config key
  `"peak-perf"` (given in TFLOPS, scaled ×10¹² internally).
- **`bandwidth` / `local_mem_bw`** — hardware memory bandwidth (bytes/s), from config key
  `"local-mem-bw"` (given in GB/s, scaled ×10⁹ internally).

Roofline logic: the op *could* run as fast as `peak_perf` if it never waited on memory, but
if it's memory-hungry (low operational intensity) it's actually capped by how fast bytes can
be streamed in: `bandwidth × operational_intensity` (bytes/s × FLOPs/byte = achievable
FLOPs/s given the memory feed rate). Whichever ceiling is lower is the one actually hit —
hence `min(...)`. This is the classic roofline model: a diagonal memory-bound line and a
flat compute-bound line; achieved FLOP/s is whichever is smaller for the op's operational
intensity.

## Elapsed time — convert achieved FLOP/s back into seconds

```cpp
double elapsed_time = node->num_ops() / perf;  // seconds
uint64_t runtime = elapsed_time * 1e9;          // seconds -> nanoseconds
```

`perf` is in FLOP/s and `num_ops` is total FLOPs for this op, so dividing gives seconds:
how long it takes to push `num_ops` FLOPs through a pipe that delivers `perf` FLOPs/sec.
`runtime` (in ns) is what gets scheduled as the op's duration
(`sys->register_event(this, EventType::General, wlhd, runtime)`).

## Full chain, variable by variable

| Variable | Meaning | Source |
|---|---|---|
| `num_ops` | FLOPs this op performs | trace attribute, precomputed upstream |
| `tensor_size` | bytes this op moves | trace attribute, precomputed upstream |
| `operational_intensity` | FLOPs per byte | computed: `num_ops / tensor_size` |
| `peak_perf` | hardware peak FLOP/s | system config `"peak-perf"` |
| `local_mem_bw` | hardware memory bandwidth | system config `"local-mem-bw"` |
| `perf` | achieved FLOP/s for this op | `min(local_mem_bw × operational_intensity, peak_perf)` |
| `elapsed_time` | this op's compute duration | `num_ops / perf` |

So compute time isn't a single lookup — it's: take the op's known FLOP/byte profile (from
the trace), pin it against the hardware's two ceilings (from config), find which ceiling
binds, and convert the resulting achievable rate back into wall-clock time for that op's
FLOP count.

## File map

- `astra-sim/system/Roofline.hh` / `.cc` — the `min(bw·OI, peak_perf)` model class.
- `astra-sim/system/Sys.hh:261-269`, `Sys.cc:398-411` — config parsing (`local-mem-bw`,
  `peak-perf`, `roofline-enabled`), `Roofline` construction.
- `astra-sim/workload/Workload.cc:255-305` — `issue_comp`, ties both formulas together.
- `astra-sim/workload/Statistics.hh` / `.cc` — reports `operation_intensity`,
  `compute_utilization`, `memory_utilization`, `is_memory_bound` per op.
- `extern/graph_frontend/chakra/src/feeder_v3/et_feeder_node_attr.h` — where `num_ops` /
  `tensor_size` are registered as trace attributes.
- `inputs/system/analytical/hgx_h100_{8,16,32}gpu.json` — example configs with roofline
  enabled.
