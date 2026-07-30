# How ASTRA-sim decides which NPUs communicate during a collective

This document explains, at the code level, how ASTRA-sim picks communication partners for
each collective (`All-Gather`, `Reduce-Scatter`, `All-Reduce`, `All-to-All`) depending on
the configured topology and per-collective algorithm (`ring`, `direct`, `halvingDoubling`,
`doubleBinaryTree`). All formulas below were verified against the source and include
hand-worked 4-NPU examples you can reproduce yourself.

## The big picture (3 layers)

1. **Physical topology → "who is grouped with whom, per dimension."** `Sys.cc` builds one
   `GeneralComplexTopology` per collective type from your `physical_dims` (e.g. `[8]` for a
   flat ring of 8, or `[2,2]` for a 2×2 mesh). For each dimension it builds a `RingTopology`
   enumerating the NPU ids in that dimension's ring.
2. **Config string → which algorithm runs per dimension.** Your JSON has e.g.
   `"all-gather-implementation": ["ring", "ring"]` — one string *per physical dimension*,
   parsed by `CollectiveImplLookup` into `Ring` / `Direct` / `HalvingDoubling` /
   `DoubleBinaryTree`.
3. **Algorithm class → the actual send/recv formula.** `Sys::generate_collective_phase`
   turns `(enum, RingTopology)` into a concrete object (`Ring`, `AllToAll`,
   `HalvingDoubling`, ...). This is the layer that decides who talks to whom.

**Key insight: "Ring" and "Direct" aren't different topologies — they're different ways of
walking the *same* ring-ordered NPU list.** Only `DoubleBinaryTree` builds a genuinely
different (tree-shaped) structure, and it's only available for `All-Reduce`.

## Step 1 — how NPUs get ordered into a ring (mixed-radix indexing)

For a single dimension, `index_in_ring = id`. For multi-dim `physical_dims = [d0, d1, ...]`,
ASTRA-sim uses row-major-style indexing:

```
offset = 1
for each dimension dim:
    index_in_this_dim(id) = (id % (offset * d[dim])) / offset
    offset *= d[dim]
```

(`GeneralComplexTopology.cc:39-41`)

For a 2×2 mesh (4 NPUs, ids 0-3 — think "2 GPUs/node via NVLink = dim0, 2 nodes via switch
= dim1"):

| id | dim0 idx (id%2) | dim1 idx (id/2) |
|----|----|----|
| 0 | 0 | 0 |
| 1 | 1 | 0 |
| 2 | 0 | 1 |
| 3 | 1 | 1 |

- dim0 rings (fixed dim1): `{0,1}` and `{2,3}` — adjacent pairs
- dim1 rings (fixed dim0): `{0,2}` and `{1,3}` — stride-2 pairs

`Sys::generate_collective` loops over dimensions, running one full instance of the
per-collective algorithm (Steps 2-5 below) per dimension, in sequence, using that
dimension's config entry — e.g. `["direct", "ring"]` on this mesh means Direct within each
NVLink pair, then Ring across nodes. `"oneRing"/"oneDirect"/"oneHalvingDoubling"` flatten
all NPUs into one group regardless of dimensions (`GeneralComplexTopology.cc:43-56`).

The rest of this doc uses a flat 4-NPU ring (`0,1,2,3`) since it's easiest to hand-calculate
— the multi-dim case is just "run the same per-dimension logic once per dimension."

---

## All-Gather

### Ring
Partner formula (`RingTopology.cc:124-158`): `receiver(id) = (index+1) mod n`,
`sender(id) = (index-1) mod n` — **fixed for the whole collective**.

All-Gather takes exactly `n-1` steps; each node forwards whatever it most recently received
to its clockwise neighbor:

```
Step1: 0→1 sends C0   1→2 sends C1   2→3 sends C2   3→0 sends C3
Step2: 0→1 sends C3   1→2 sends C0   2→3 sends C1   3→0 sends C2
Step3: 0→1 sends C2   1→2 sends C3   2→3 sends C0   3→0 sends C1
→ after 3 steps everyone has {C0,C1,C2,C3}
```

Rule: at step `s`, NPU `i` sends what it received at `s-1` to `(i+1) mod n`. Every link
carries exactly 1 chunk/step → total per link = `(n-1)×chunk_size`. Bandwidth-optimal,
hence the default in nearly every shipped config. (`Ring.cc:75-78`)

### Direct
No separate `Direct.cc` — `"direct"` instantiates `AllToAll` (a `Ring` subclass) that talks
to everyone **directly** instead of relaying: `parallel_reduce = nodes_in_ring - 1` when
unwindowed (`AllToAll.cc:20-24`). All-Gather on 4 NPUs becomes **one round**: NPU0 sends
`C0` to 1, 2, 3 simultaneously (and receives their chunks the same round).

**Direct needs `n-1` simultaneous connections per node** (great for small `n`/high
bisection bandwidth, bad at scale from contention); **Ring only ever needs 2 links/node**
but takes `n-1` sequential hops. Windowed variants like `"direct2"` set
`parallel_reduce = min(2, n-1)` — a tunable middle ground (2 rounds instead of 1 or 3).

### HalvingDoubling
Distance starts at `n/2` and halves each round; rounds = `log2(n)`.

**4 NPUs, 2 rounds:**
- Round 1 (offset=2): pairs `(0,2)` and `(1,3)` exchange → everyone has 2/4 chunks
- Round 2 (offset=1): pairs `(0,1)` and `(2,3)` exchange their 2-chunk sets → everyone has all 4

Fewer rounds than Ring (`log2(n)` vs `n-1`), but each round moves more data per link — a
latency-vs-bandwidth trade, best when per-message overhead dominates.
(`HalvingDoubling.cc:61-65, 93-104, 157-179`)

---

## Reduce-Scatter

### Ring
Same fixed neighbor as All-Gather (`i → i+1`), but the data flow is mirrored: instead of
*growing* what each node holds, it *shrinks* it via reduction. Over `n-1` steps, node `i`
forwards a running partial-sum chunk (size `data_size/n`) to its neighbor, who adds its own
local chunk before forwarding again. After `n-1` steps, each node ends up holding exactly
one fully-reduced `1/n`-sized block — link cost is identical to Ring All-Gather:
`(n-1) × (data_size/n)` per link, just summing instead of copying.

### Direct
One round, `parallel_reduce = n-1`: every node sends its `data_size/n` contribution
directly to each of the other `n-1` peers (the piece relevant to *their* final block) and
reduces locally whatever it receives. Same trade-off as All-Gather: **Direct needs `n-1`
simultaneous connections per node** (great for small `n`/high bisection bandwidth, bad at
scale from contention); **Ring only needs 2 links/node** but takes `n-1` sequential hops.

### HalvingDoubling
Distance *doubles* each round (opposite of All-Gather's halving), `log2(n)` rounds, msg
size halves each round.

**4-NPU example:**
```
Round1 (distance=1, msg=data/2): pairs (0,1) and (2,3) reduce their halves
Round2 (distance=2, msg=data/4): pairs (0,2) and (1,3) reduce further
→ after log2(4)=2 rounds, each node holds its final data/4 block
```

---

## All-Reduce

### Ring
The one case where Ring does it *in a single object*: `stream_count = 2×(n-1)`, using the
same fixed neighbor the whole time (`Ring.cc:44`). It's literally reduce-scatter
immediately followed by all-gather, back to back, no separate config phase needed on a
single dimension.

**4-NPU example:** 6 total steps — the first 3 are a reduce-scatter (each node ends up with
1 fully-reduced block), the next 3 relay that block around the ring so everyone ends with
all 4 reduced blocks. Same 2-links-per-node, `2(n-1)`-hop cost as classic "ring all-reduce"
(NCCL/Horovod use this exact pattern).

### Direct
`stream_count = 2×(n-1)` inherited from Ring, but `parallel_reduce = n-1`, so it collapses
to **2 rounds** instead of `2(n-1)` sequential hops: round 1 = direct reduce-scatter (send
your contribution to all `n-1` peers at once, reduce locally), round 2 = direct all-gather
(broadcast your reduced block to all `n-1` peers at once). Same trade-off: 2 rounds of
`n-1` simultaneous connections vs Ring's `2(n-1)` sequential 2-link hops.

### HalvingDoubling
Combines both phases in one algorithm: distance **doubles** (1→2→...→n) for the
reduce-scatter half, then **halves** back down (n/2→...→1) for the all-gather half —
`2×log2(n)` rounds total. (`HalvingDoubling.cc:157-171`, the `rank_offset == nodes_in_ring`
switch from `offset_multiplier=2` to `0.5`.)

**4-NPU example:**
```
Round1 (dist=1): (0,1),(2,3) reduce
Round2 (dist=2): (0,2),(1,3) reduce        → each node now has 1 fully-reduced quarter
Round3 (dist=2): (0,2),(1,3) exchange (copy, no more reducing)
Round4 (dist=1): (0,1),(2,3) exchange (copy)
→ after 2×log2(4)=4 rounds, everyone has the full all-reduced result
```

Fewer total rounds than Ring's 6, more data moved per round — same latency-vs-bandwidth
trade as before.

### DoubleBinaryTree
All-Reduce-only algorithm; not available for the other three collectives. Instead of a
ring, each node gets a fixed `parent`/`left_child`/`right_child` from a binary tree
(`BinaryTree.cc`), and ASTRA-sim actually builds **two complementary trees** (one
root-at-lowest-id, one root-at-highest-id — `DoubleBinaryTreeTopology.cc`) and alternates
between them per chunk, so no single node is the root (the bottleneck) for both halves of
the data.

**4-NPU example** (one of the two trees, ids 0-3):
```
        0 (root)
        |
        2 (intermediate)
       / \
      1   3   (leaves)
```
Leaves (1, 3) send their data up to 2; node 2 waits for both, sums them with its own, and
sends the reduced result up to root 0; root 0 sends the final sum back down to 2; 2
broadcasts it back down to leaves 1 and 3. No node ever needs more than 3 links total
(parent + 2 children), so this scales better in link-count than Ring at very large `n`, at
the cost of tree depth (`log2(n)` hops up + `log2(n)` down) — a link-count vs latency trade
distinct from the Ring/Direct one.

---

## All-to-All

### Direct
(`AllToAll` class, config `"direct"`) — the "obvious" all-to-all everyone pictures:
`stream_count = n-1`, `parallel_reduce = n-1` unwindowed → **one round**, each node sends
its personalized `data_size/n` chunk directly to each of the other `n-1` nodes, no
relaying. Needs `n-1` simultaneous connections per node.

### Ring
(config `"ring"`, plain `Ring` class with `ComType::All_to_All`) — this is *not* direct
pairwise exchange; it's a **relay** over the same fixed neighbor used everywhere else.
Every message, even one destined for a node several hops away, gets forwarded hop-by-hop
through the ring rather than sent straight there.

**4-NPU example:** node `i`'s personalized chunk to its immediate neighbor takes 1 hop, to
the node 2 away takes 2 hops, to the node 3 away (all the way around) takes 3 hops — total
hops carried by node `i`'s outbound link = `1+2+3 = 6`, which is exactly
`n(n-1)/2 = 6` (`Ring.cc:47`), matching `stream_count` in the code.

So Ring All-to-All trades bandwidth (more total link-hops than the `n-1` "ideal" direct
messages) for needing only the same 2 links/node as every other Ring collective — the
identical Direct-vs-Ring trade-off, just paid in relay-hop volume instead of step count.

### HalvingDoubling — not supported
The code explicitly errors out: `HalvingDoubling.cc`'s `default` case logs
`"unknown communication type"` and calls `std::exit(1)` for any `ComType` other than
`All_Reduce`, `All_Gather`, `Reduce_Scatter`. Setting
`"all-to-all-implementation": ["halvingDoubling"]` will crash the simulation — only
`"ring"` or `"direct"` (windowed or not) are valid there.

---

## Where to look yourself / how to verify

| What | File |
|---|---|
| Ring partner formula | `astra-sim/system/astraccl/native_collectives/logical_topology/RingTopology.cc:124-158` |
| Multi-dim ring construction (mixed-radix `offset` math) | `astra-sim/system/astraccl/native_collectives/logical_topology/GeneralComplexTopology.cc:19-74` |
| `Ring` algorithm (fixed neighbor, relay; per-`ComType` stream_count/msg_size table) | `astra-sim/system/astraccl/native_collectives/collective_algorithm/Ring.cc` |
| `AllToAll`/"Direct" algorithm (walks partner every hop, `parallel_reduce`) | `.../collective_algorithm/AllToAll.cc` |
| `HalvingDoubling` algorithm (distance halves/doubles per round) | `.../collective_algorithm/HalvingDoubling.cc` |
| `DoubleBinaryTreeAllReduce` (parent/child state machine) | `.../collective_algorithm/DoubleBinaryTreeAllReduce.cc` |
| Binary tree construction (in-order id assignment, two complementary trees) | `.../logical_topology/BinaryTree.cc`, `DoubleBinaryTreeTopology.cc` |
| Config string → algorithm enum parsing | `astra-sim/system/astraccl/CollectiveImplLookup.cc:27-58` |
| Top-level dispatch per collective/dimension | `astra-sim/system/Sys.cc` (`generate_all_gather` ~665, `generate_collective` ~719, `generate_collective_phase` ~1016) |
| Example configs with the `*-implementation` arrays | `inputs/system/analytical/*.json` (e.g. `hgx_h100_4gpu.json`, `dgx_v100_8gpu.json`) |

To verify by running it yourself: pick a small config (e.g.
`inputs/system/analytical/hgx_h100_4gpu.json`), change e.g.
`"all-gather-implementation"` between `"ring"`, `"direct"`, and `"halvingDoubling"`, and
diff the generated event/send-recv logs (or step through `Ring::ready()` /
`AllToAll::process_max_count()` in a debugger) to see the partner sequence match the
hand-worked examples above.
