# ASTRA-sim `system.json` Reference Notes

Quick reference for the non-network parameters in ASTRA-sim's system configuration,
plus how collective algorithms map to topology building blocks.

---

## 1. Scheduling Parameters

| Parameter | Values | What it decides |
|---|---|---|
| `scheduling-policy` | `LIFO` / `FIFO` | Which **whole collective** gets priority when several are pending. LIFO = newest collective goes first. |
| `intra-dimension-scheduling` | `FIFO` / `SCF` | Within one dimension's queue, which **chunk** goes first. SCF = smallest remaining chunk first. |
| `inter-dimension-scheduling` | `baseline` / `themis` | How issuing is prioritized **across dimensions** when several are active in parallel. `baseline` = fixed static schedule. `themis` = load-aware, favors the least-busy dimension. |
| `active-chunks-per-dimension` | int | Max number of chunks allowed in flight at once, per logical dimension. |
| `preferred-dataset-splits` | int | Number of chunks each NPU's own contribution to a collective is split into (enables pipelining). |
| `endpoint-delay` | int (cycles) | Fixed delay an NPU spends processing a message *after* receiving it. |
| `boost-mode` | 0 / 1 | Speed optimization — only valid if every link **within a dimension** is symmetric (same BW/latency). Skips redundant per-link computation. |

**Key clarifications:**
- A **chunk** is a slice of one NPU's own data for one collective call — not a network packet.
- **Dimension order/path is fixed** by the topology + chosen algorithm (e.g. `ring_ring_ring` = always dim1→dim2→dim3). `inter-dimension-scheduling` does **not** choose the path — it only decides *timing*: which dimension's idle capacity to prioritize feeding chunks into right now.
- A "dimension" = a whole sub-network (e.g. `Ring(4)` = 4 physical links forming one ring), not a single wire.

---

## 2. Collective Algorithm Selection

| Parameter | Format |
|---|---|
| `all-reduce-implementation` | `Dim1Alg_Dim2Alg_..._DimNAlg` |
| `reduce-scatter-implementation` | same format |
| `all-gather-implementation` | same format |
| `all-to-all-implementation` | same format |

- One algorithm string per **logical dimension**, underscore-joined. List length must match the number of logical dimensions.
- `oneRing` / `oneDirect` ignore physical dimension count and run one single-phase algorithm across **all** NPUs at once.
- Custom alternative: `all-reduce-implementation-custom` (and equivalents) — points to a Chakra ET file defining your own algorithm instead of a built-in name.

| Parameter | Values | What it does |
|---|---|---|
| `collective-optimization` | `baseline` / `localBWAware` | `baseline`: naive multi-dim all-reduce. `localBWAware`: reduce-scatter across dims 1→N-1, all-reduce on last dim, all-gather back N-1→1 — shrinks data volume on lower-BW dims. |

---

## 3. Topology ↔ Native Algorithm Pairing

| Building Block | Native Collective Algorithm | Why |
|---|---|---|
| `Ring(k)` | `ring` | Only talks to 2 neighbors — matches ring connectivity. |
| `FullyConnected(k)` | `direct` | Every node can reach every other node directly in one hop. |
| `Switch(k)` | `halvingDoubling` | Assumes uniform any-to-any connectivity at equal cost — exactly what a switch provides. |

### All-Reduce algorithm comparison (general, not just for Switch)

| Algorithm | Steps | Bandwidth per NPU | Best suited for |
|---|---|---|---|
| `ring` | 2(N−1) | Minimal (bandwidth-optimal) | Ring topologies. Wastes fanout on a switch. |
| `direct` | 1 + 1 | Same total volume, but split into N−1 small messages/step | Fully-connected topologies. High per-message overhead at large N. |
| `doubleBinaryTree` | O(log N) | Slightly above optimal | Tree-shaped physical links. |
| `halvingDoubling` | log₂(N) | Near bandwidth-optimal | Switch / uniform any-to-any connectivity. Best of both worlds (low latency + good bandwidth) — but only because it assumes uniform pairwise cost. |

**Takeaway:** Running a mismatched algorithm (e.g. forcing `ring` on a `Switch` topology) still works correctly, just suboptimally — useful as a deliberate sensitivity experiment to show the cost of algorithm-topology mismatch.

---

## 4. Compute Backend Toggles (also in `system.json`)

| Parameter | Values | Default behavior if unset |
|---|---|---|
| `roofline-enabled` | 0 / 1 | 0 — compute time taken directly from the Chakra ET ("measured runtime"). |
| `local-mem-bw` | GB/s | Only used if roofline enabled. |
| `peak-perf` | TFLOPS | Only used if roofline enabled. |

---

## 5. Communicator Groups (`--comm-group-configuration`)

- Optional flag. If omitted: **default = one single group containing all NPUs.**
- Only needed if your Chakra ET tags different `COMM_COLL_NODE`s with different `pg_name` values that should map to different NPU subsets (e.g. separate DP vs. TP communicator groups running concurrently).
