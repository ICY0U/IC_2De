# AI Pathfinding Systems for a Custom Grid-Based Game Engine: A Technical Reference

**Scope:** This document is an internal engineering reference for a solo/small-team developer building a custom 2D/3D grid-based game engine from scratch (C++ or similar) and implementing AI pathfinding within it. The organizing requirement driving this report is a **hard-block constraint**: an agent must be able to path from a point A to a point C while being structurally guaranteed to never enter a forbidden zone or point B. That guarantee has to come from the shape of the search space itself — a cell or region that is excluded from the graph entirely — not from a large cost penalty that a sufficiently motivated search could still decide to pay. Every section below is written with that requirement in view, and Section 3 addresses it directly with concrete encoding and corner-cutting rules.

Each section explains the underlying algorithm or mechanism from primary sources — academic papers and official engine documentation — and, where useful, grounds it against how Unreal Engine (Recast/Detour navmesh) and Unity (NavMesh) solve the equivalent problem, purely as real-world validation of the concepts, not as the subject of the report. Inline citations `[n]` refer to the numbered [References](#references) list at the end, which links directly to the primary sources — academic papers (fetched and read in full where available) and first-party engine documentation — rather than secondary summaries of them.

---

## Table of Contents

1. [Core Pathfinding Algorithms](#1-core-pathfinding-algorithms)
   - [1.1 Dijkstra's Algorithm and Uniform-Cost Search](#11-dijkstras-algorithm-and-uniform-cost-search)
   - [1.2 A\* Search and Heuristic Admissibility](#12-a-search-and-heuristic-admissibility)
   - [1.3 Grid Heuristics: Manhattan, Euclidean, Chebyshev, Octile](#13-grid-heuristics-manhattan-euclidean-chebyshev-octile)
   - [1.4 Jump Point Search (JPS) and JPS+](#14-jump-point-search-jps-and-jps)
   - [1.5 Theta\* and Lazy Theta\*: Any-Angle Search](#15-theta-and-lazy-theta-any-angle-search)
   - [1.6 Complexity and Benchmark Comparison](#16-complexity-and-benchmark-comparison)
2. [Grid Data Representations](#2-grid-data-representations)
   - [2.1 Dense 2D Arrays vs. Sparse/Hashed Grids](#21-dense-2d-arrays-vs-sparsehashed-grids)
   - [2.2 2.5D Heightfield Grids vs. True 3D Voxel Grids](#22-25d-heightfield-grids-vs-true-3d-voxel-grids)
   - [2.3 Choosing a Representation for a Custom Engine](#23-choosing-a-representation-for-a-custom-engine)
3. [Hard-Blocking a Forbidden Region (Point B)](#3-hard-blocking-a-forbidden-region-point-b)
   - [3.1 Encoding Permanent Exclusion Zones](#31-encoding-permanent-exclusion-zones)
   - [3.2 Diagonal Movement and Corner-Cutting Near Blocked Cells](#32-diagonal-movement-and-corner-cutting-near-blocked-cells)
   - [3.3 Dynamic Obstacle Updates and Path Cache Invalidation](#33-dynamic-obstacle-updates-and-path-cache-invalidation)
4. [Path Smoothing and Any-Angle Paths](#4-path-smoothing-and-any-angle-paths)
   - [4.1 String-Pulling / Funnel-Style Post-Processing](#41-string-pulling--funnel-style-post-processing)
   - [4.2 Any-Angle Search vs. Post-Process Smoothing](#42-any-angle-search-vs-post-process-smoothing)
5. [Scaling to Many Simultaneous Agents](#5-scaling-to-many-simultaneous-agents)
   - [5.1 Hierarchical Pathfinding (HPA\*)](#51-hierarchical-pathfinding-hpa)
   - [5.2 Flow Fields / Vector Fields](#52-flow-fields--vector-fields)
   - [5.3 Path Caching](#53-path-caching)
   - [5.4 Time-Slicing Pathfinding Across Frames](#54-time-slicing-pathfinding-across-frames)
6. [Local Avoidance and Steering](#6-local-avoidance-and-steering)
   - [6.1 Reciprocal Velocity Obstacles: RVO and ORCA](#61-reciprocal-velocity-obstacles-rvo-and-orca)
   - [6.2 Boids-Style Separation Steering](#62-boids-style-separation-steering)
   - [6.3 Combining Global Pathfinding with Local Avoidance](#63-combining-global-pathfinding-with-local-avoidance)
7. [Real-World Grounding: Unreal and Unity](#7-real-world-grounding-unreal-and-unity)
   - [7.1 Unreal: Recast/Detour, Nav Mesh Bounds Volumes, Nav Areas and Modifiers](#71-unreal-recastdetour-nav-mesh-bounds-volumes-nav-areas-and-modifiers)
   - [7.2 Unity: NavMesh, NavMeshModifierVolume, Area Costs, NavMeshObstacle Carving](#72-unity-navmesh-navmeshmodifiervolume-area-costs-navmeshobstacle-carving)
8. [Recommended Implementation Roadmap](#8-recommended-implementation-roadmap)
   - [8.1 Phase 1 — Minimum Viable: Hard-Blocked Grid A\*](#81-phase-1--minimum-viable-hard-blocked-grid-a)
   - [8.2 Phase 2 — Smoothing and Performance](#82-phase-2--smoothing-and-performance)
   - [8.3 Phase 3+ — Optional / Later](#83-phase-3--optional--later)
9. [References](#references)

---

## 1. Core Pathfinding Algorithms

### 1.1 Dijkstra's Algorithm and Uniform-Cost Search

Every algorithm in this report is a variation on the same underlying graph-search skeleton: maintain a **frontier** of nodes discovered but not yet expanded, a `cost_so_far` map recording the cheapest known cost from the start to each visited node, and a `came_from` map used to reconstruct the path once the goal is reached. Dijkstra's algorithm — breadth-first search generalized to weighted edges — expands nodes from the frontier in order of `cost_so_far`, using a priority queue, and is guaranteed to find the shortest path because it never expands a node before it has found that node's true minimum cost [2]. Its weakness for game pathfinding is that it has no notion of *where the goal is*: it explores uniformly outward in all directions, which wastes enormous effort on a grid where the goal is known in advance and lies in a specific direction [2].

Greedy Best-First Search is the opposite extreme: it prioritizes nodes purely by an estimate of remaining distance to the goal (a heuristic `h(n)`), ignoring cost already spent. This makes it fast in open terrain but not guaranteed optimal, and it can be led badly astray by obstacles that happen to lie between the current position and the goal [2]. A\* is the combination of the two: it prioritizes the frontier by `f(n) = g(n) + h(n)`, where `g(n)` is `cost_so_far` (Dijkstra's term) and `h(n)` is the heuristic estimate to the goal (Greedy Best-First's term) [2][1]. This is not an approximation trick — it is the original formulation given by Hart, Nilsson, and Raphael in the 1968 paper that introduced A\*, which formalized the idea of augmenting uniform-cost graph search with heuristic "problem-specific information" and proved the resulting class of algorithms retains an optimality guarantee under specific conditions on the heuristic [1].

```
// A* — Red Blob Games' canonical formulation [2]
frontier = PriorityQueue()
frontier.put(start, 0)
came_from[start] = None
cost_so_far[start] = 0

while not frontier.empty():
    current = frontier.get()
    if current == goal:
        break
    for next in graph.neighbors(current):
        new_cost = cost_so_far[current] + graph.cost(current, next)
        if next not in cost_so_far or new_cost < cost_so_far[next]:
            cost_so_far[next] = new_cost
            priority = new_cost + heuristic(goal, next)
            frontier.put(next, priority)
            came_from[next] = current
```

Setting `heuristic(n) = 0` for every node collapses A\* back into plain Dijkstra; this is a useful sanity check when implementing A\* for the first time — it should still find correct (if slower) paths.

### 1.2 A\* Search and Heuristic Admissibility

The property that makes A\* both fast and correct is **admissibility**: a heuristic is admissible if it never overestimates the true remaining cost to the goal [1][3]. Hart, Nilsson, and Raphael's original result is that A\* using an admissible heuristic is guaranteed to return an optimal (lowest-cost) path, because the algorithm will never commit to expanding a suboptimal path to the goal while a cheaper, unexplored one remains possible according to the heuristic's own (non-overestimating) bound [1]. Red Blob Games states the practical version of this guarantee plainly: "A\* is guaranteed to find the shortest path if the heuristic is never larger than the true distance" [2]. A closely related, stronger property used by algorithms discussed later in this report (Theta\*, Lazy Theta\*) is **consistency** (also called monotonicity): `h(n) <= cost(n, n') + h(n')` for every neighbor `n'` of `n`. A consistent heuristic is automatically admissible and additionally guarantees that once a node is expanded (removed from the frontier for the final time) its `g`-value is already optimal, so no node needs to be re-expanded — this is why A\* implementations commonly keep a closed list, and why several of the algorithms in Section 1.5 have to reason carefully about what happens when they *don't* maintain this property [8].

Practically, admissibility is a design constraint you impose on your own heuristic function, not a property the algorithm checks for you: pick a heuristic function whose value is provably always at or below the true grid distance for your movement rules, and A\* on that grid will return true-shortest-path answers. Overestimating even slightly (as in "weighted A\*", which multiplies the heuristic by a constant greater than one to trade optimality for speed) breaks the optimality guarantee, though it can still be a reasonable choice for a game where "close to shortest" is good enough and it is faster [3].

### 1.3 Grid Heuristics: Manhattan, Euclidean, Chebyshev, Octile

The correct heuristic to use is determined entirely by which moves your grid allows, because the heuristic must estimate the true minimum-cost distance under your specific movement rules, and using a heuristic built for a different movement model breaks admissibility in one direction or wastes efficiency in the other [3]:

- **Manhattan distance** — for 4-directional (cardinal-only) movement: `h(n) = D * (|dx| + |dy|)`, where `D` is the cost of a single orthogonal step. This is admissible for 4-way movement because it is exactly the number of orthogonal steps required with no obstacles, and it correctly refuses to "cut corners," since diagonal movement is not part of the movement model at all [3].
- **Euclidean distance** — for movement that is not constrained to grid edges (any-angle movement, or grids where an agent can move at an arbitrary continuous heading): `h(n) = D * sqrt(dx^2 + dy^2)`. This is the true straight-line distance, so it is admissible whenever an agent can, in principle, walk in a straight line to the goal; Red Blob Games specifically warns against using **squared** Euclidean distance as a shortcut, because the difference in scale relative to true path cost causes A\* to degenerate toward greedy best-first behavior [3].
- **Chebyshev distance** (also "diagonal distance" with equal diagonal and orthogonal cost) — for 8-directional movement where a diagonal step costs the same as an orthogonal step: `h(n) = D * max(|dx|, |dy|)`. This is the generalized formula `h(n) = D * (|dx| + |dy|) + (D2 - 2*D) * min(|dx|, |dy|)` with `D2 = D` [3].
- **Octile distance** — for 8-directional movement where a diagonal step costs `sqrt(2)` times an orthogonal step (the standard, geometrically correct cost model for a square grid): the same generalized formula with `D = 1, D2 = sqrt(2)`, i.e. `h(n) = D * (|dx| + |dy|) + (sqrt(2) - 2) * min(|dx|, |dy|)` [3]. This is the heuristic used in both the JPS and Theta\* benchmark papers discussed below, and is the correct default for 8-directional grid movement in a typical game [5][8].

The general lesson from Amit Patel's heuristics reference — which underpins the "Introduction to A\*" series on Red Blob Games — is to match the heuristic's movement assumptions exactly to the grid's actual movement rules and cost function, using `D` as the actual minimum single-step cost so the two stay in the same units [3]. A heuristic that assumes diagonal movement is available (Chebyshev/octile) is *not* admissible on a 4-directional grid, because it can underestimate work correctly only when diagonal shortcuts genuinely exist. Conversely, using Manhattan distance on an 8-directional grid is still admissible (it never overestimates — it merely underestimates more than necessary) but is a strictly weaker heuristic that will explore more nodes than octile distance for no benefit [3].

A secondary but practically important note from the same source concerns **tie-breaking**: on a uniform-cost grid many different node orderings produce paths of identical total length, and without a tie-breaking rule A\* can return zig-zagging, visually unrealistic paths even though they are numerically optimal. Red Blob Games documents several fixes — nudging the heuristic very slightly upward (`h(n) * (1.0 + epsilon)`) to bias toward the goal direction, adding a small cross-product-based penalty for deviating from the straight line to the goal, or structural fixes such as Jump Point Search and hierarchical pathfinding, which sidestep the issue by construction [3][4][13].

### 1.4 Jump Point Search (JPS) and JPS+

Jump Point Search, introduced by Harabor and Grastien at AAAI 2011, is not a different search algorithm from A\* — it is A\* combined with two neighbor-pruning rules specific to *uniform-cost grids* that eliminate path symmetry: the observation that on an open grid, many different node-by-node orderings of the same moves produce paths of identical cost, and classical A\* wastes enormous effort re-exploring these permutations [5]. JPS defines, for a node `x` reached from parent `p(x)`, a **dominance** rule that prunes any neighbor `n` of `x` whose path via `x` is no better than an alternative path that skips `x` entirely: formally, for a straight move, prune `n` if `len(<p(x),...,n> \ x) <= len(<p(x), x, n>)`, and for a diagonal move the same rule applies with strict inequality [5]. Neighbors that survive this pruning are called the **natural neighbors** of `x`. When an obstacle blocks the "would-be shorter" alternative path, a neighbor that would otherwise have been pruned must instead be evaluated — this is a **forced neighbor**, formally: `n` is forced if it is not a natural neighbor and `len(<p(x), x, n>) < len(<p(x),...,n> \ x)` [5].

A **jump point** is then defined recursively: node `y` is a jump point from `x` in direction `d` if it is the first node reached by repeatedly stepping in direction `d` from `x` such that either (1) `y` is the goal, (2) `y` has at least one forced neighbor, or (3) `d` is a diagonal move and stepping straight from `y` in either of the two component directions of `d` reaches a jump point by condition (1) or (2) [5]. Instead of adding every intermediate cell to A\*'s open list, JPS "jumps" past them in a single step and only ever puts jump points on the open list — the two prunings together (skipping symmetric neighbors, and only opening jump points) each account for roughly half of JPS's total speedup over plain A\*, according to the Game AI Pro 2 chapter on JPS+ [6]. Crucially, JPS is not an approximation: Harabor and Grastien prove that for every optimal-length path on a grid there exists an equivalent-length path expressible purely in terms of jump points, so pruning intermediate nodes never sacrifices optimality [5].

```
// Identify jump-point successors of node x — Harabor & Grastien [5]
function IDENTIFY_SUCCESSORS(x, start, goal):
    successors = {}
    for n in prune(x, neighbors(x)):        // apply straight/diagonal dominance rule
        n2 = jump(x, direction(x, n), start, goal)
        if n2 is not null:
            add n2 to successors
    return successors

function jump(x, d, start, goal):
    n = step(x, d)
    if n is an obstacle or off-grid: return null
    if n == goal: return n
    if n has a forced neighbor: return n
    if d is diagonal:
        for d_i in {d1, d2}:                 // the two straight components of d
            if jump(n, d_i, start, goal) is not null:
                return n
    return jump(n, d, start, goal)           // keep stepping in direction d
```

Benchmarked against plain A\* on four standard grid-map corpora (Adaptive Depth, Baldur's Gate II, Dragon Age: Origins, and a synthetic "Rooms" set), the original JPS paper reports average node-expansion speedups ranging from roughly 13x (Rooms) to over 215x (Baldur's Gate II), with search-time speedups on the order of 25–30x on the Baldur's Gate and Dragon Age map sets [5]. JPS also outperformed the contemporary Swamps pruning technique (3–5x speedup) on every benchmark, and was shown to be competitive with, and often faster than, HPA\* [10] despite HPA\*'s use of offline preprocessing, while JPS itself requires none [5].

**JPS+** is a further, offline-preprocessed variant, described independently by two groups: Harabor and Grastien's own 2014 ICAPS follow-up, "Improving Jump Point Search," which introduces block-wise online pruning, offline jump-point preprocessing, and tighter pruning rules, reporting further speedups "from several factors to over one order of magnitude" beyond baseline JPS [7]; and Steve Rabin and Fernando Silva's independently developed JPS+, published in *Game AI Pro 2* [6]. Rabin's JPS+ precomputes, for every walkable cell and each of the eight directions, either the distance to the nearest jump point in that direction or (encoded as a negative number) the distance to the nearest wall — collapsing JPS's runtime "scan for a jump point" step into a single array lookup [6]. On a concrete illustrative benchmark, the chapter reports that on a particular 40×40 map, A\* took 180.05 ns, JPS took 15.04 ns, and JPS+ took 1.55 ns to find the optimal path — a 116x speedup over A\* while remaining "perfectly optimal" [6]. The tradeoff is memory and a preprocessing/build step: JPS+ requires a one-time pass over the static grid to bake per-cell jump distances, and — like JPS itself — the technique is fundamentally built on the assumption of a *uniform-cost, static* grid; it does not by itself handle variable terrain costs or a grid that changes shape at runtime without re-preprocessing the affected region [6].

### 1.5 Theta\* and Lazy Theta\*: Any-Angle Search

A\*, JPS, and JPS+ all share one structural limitation: every edge in the search graph corresponds to a single grid step, so the returned path is constrained to grid-aligned headings, even where a direct diagonal-ish line-of-sight route would be shorter and more natural-looking. Nash, Daniel, Koenig, and Felner's AAAI 2007 paper introduces **Theta\*** to close this gap directly during the search, rather than as a post-process [8]. The key structural change relative to A\* is a single line in the vertex-update procedure: where A\* restricts `parent(s')` to be a graph neighbor of `s'`, Theta\* allows `parent(s')` to be *any* vertex, as long as there is line-of-sight to it [8]. Concretely, when expanding vertex `s` and considering an unexpanded successor `s'`, Basic Theta\* evaluates two candidate paths rather than one: **Path 1**, the usual A\* update via `s` (`g(s) + c(s, s')`), and **Path 2**, a path through `s`'s own parent (`g(parent(s)) + c(parent(s), s')`), which is only considered when `s'` has line-of-sight to `parent(s)` — a check that is valid by the triangle inequality, since any such path can never be longer than Path 1 [8]:

```
// Theta* UpdateVertex — Nash, Daniel, Koenig, Felner [8]
UpdateVertex(s, s'):
    if lineofsight(parent(s), s'):
        // Path 2 — any-angle update through s's parent
        if g(parent(s)) + c(parent(s), s') < g(s'):
            g(s') = g(parent(s)) + c(parent(s), s')
            parent(s') = parent(s)
            reinsert s' into open with priority g(s') + h(s')
    else:
        // Path 1 — ordinary A* update
        if g(s) + c(s, s') < g(s'):
            g(s') = g(s) + c(s, s')
            parent(s') = s
            reinsert s' into open with priority g(s') + h(s')
```

This repeated per-vertex line-of-sight check is Theta\*'s main runtime cost; the paper also presents **Angle-Propagation Theta\*** (AP Theta\*), a variant that avoids most explicit line-of-sight raycasts by incrementally maintaining an angular visibility range per vertex, at the cost of very slightly longer paths [8]. Benchmarked against A\* with post-smoothing (string-pulling, described in Section 4) and against Field D\* — the only other any-angle grid method the authors were aware of at the time — on random-obstacle grids and scaled *Baldur's Gate II* maps, the paper reports that Basic Theta\* found shorter paths than A\* with post-smoothing 95% of the time and shorter paths than Field D\* 93% of the time on 500×500 grids with 20% random obstacles, with comparable runtime to the alternatives, and produced paths with substantially fewer heading changes (a proxy for "realistic-looking") than Field D\* [8]. Theta\* is *not* guaranteed to find the true shortest path — unlike A\*, it can occasionally miss a global optimum by a small margin (the paper's own worst-case example is under 0.2% longer than optimal) because a vertex can only inherit a parent that one of its own neighbors already reached — but in practice it finds paths extremely close to optimal, and consistently shorter than any post-processed A\* alternative tested [8].

Theta\*'s per-expansion line-of-sight cost becomes proportionally worse as branching factor grows — moving from an 8-neighbor 2D grid to a 26-neighbor 3D voxel grid multiplies the number of visibility checks per expanded vertex several-fold. **Lazy Theta\***, from Nash, Koenig, and Tovey, addresses exactly this case: instead of checking line-of-sight to `parent(s)` for *every* unexpanded visible neighbor as Theta\* does, Lazy Theta\* optimistically assumes line-of-sight holds, updates `g`/`parent` under that assumption, and defers the actual visibility check to the single moment the node is popped for expansion — performing at most one line-of-sight check per expanded vertex rather than one per neighbor-per-expansion [9]. If the deferred check later turns out to be false, the vertex's `g`-value and parent are corrected via the ordinary Path-1-style update before expansion continues [9]. The paper's own 3D experiments, on 100×100×100 26-neighbor cubic grids, report that Lazy Theta\* finds paths of essentially the same length as Theta\* (both roughly 8% shorter than paths constrained to grid edges, i.e. plain A\*'s output), while performing "more than one order of magnitude fewer line-of-sight checks" and running up to about 1.6x faster than Theta\*, at the cost of expanding somewhat more vertices [9]. The same paper independently quantifies *why* any-angle planning matters at all: shortest paths constrained to the edges of an 8-neighbor 2D grid can be up to roughly 8% longer than the true shortest path in continuous space, and the equivalent figure for a 26-neighbor 3D cubic grid is roughly 13% — meaningfully worse, because a cubic grid's fixed move set approximates 3D directions more coarsely relative to true 3D freedom of movement than an 8-neighbor grid approximates 2D directions [9].

### 1.6 Complexity and Benchmark Comparison

| Algorithm | Optimality | Big-O (nodes/edges, `V`/`E`) | Preprocessing | Reported speedup vs. A\* | Primary source |
|---|---|---|---|---|---|
| Dijkstra | Optimal | `O((V + E) log V)` with a binary-heap priority queue | None | Baseline (slower than A\* in practice — no goal direction) | [2] |
| A\* | Optimal (with admissible `h`) | `O((V + E) log V)`, in practice far fewer nodes than Dijkstra when `h` is informative | None | — (reference point) | [1][2] |
| JPS | Optimal | Same worst case as A\*, but with symmetry pruning collapsing most intermediate expansions | None (fully online) | ~13x–215x fewer node expansions; ~25–30x wall-clock on realistic game maps, in the original paper's benchmarks | [5] |
| JPS+ | Optimal | Runtime lookup is `O(1)` per jump (precomputed distance table) | Offline, per static grid | ~116x wall-clock vs. A\* on the chapter's own 40×40 benchmark; "several factors to over one order of magnitude" beyond baseline JPS in the ICAPS14 academic variant | [6][7] |
| Theta\* / Lazy Theta\* | Near-optimal (small bounded suboptimality) | A\*'s complexity plus per-expansion line-of-sight cost (up to `O(cells)` per check, amortized down by AP Theta\* / Lazy Theta\*) | None | Comparable runtime to A\* with post-smoothing, but shorter, more direct paths; Lazy Theta\* up to ~1.6x faster than Theta\* on 3D grids with >10x fewer line-of-sight checks | [8][9] |
| HPA\* | Suboptimal (bounded) | Roughly `O((V/k) log(V/k))` at the abstract level, where `k` is cluster size, plus one-time `O(V)`-ish preprocessing | Offline clustering + cached intra-cluster distances | ~10x faster than highly-optimized A\*, paths within ~1% of optimal, in the paper's own benchmarks on scaled *Baldur's Gate* maps | [10] |

These are not independent choices — JPS/JPS+ and Theta\*/Lazy Theta\* solve different problems (search speed on a uniform grid vs. path *quality*/angle) and are commonly combined in practice (e.g., JPS or JPS+ to find a grid-optimal path quickly, then string-pulling or Theta\*-style smoothing to remove the residual staircasing), while HPA\* addresses a third axis — scaling to very large maps or many simultaneous agents — at the cost of giving up strict optimality. Section 8's roadmap sequences these appropriately for a solo developer building up from nothing.

---

## 2. Grid Data Representations

### 2.1 Dense 2D Arrays vs. Sparse/Hashed Grids

The simplest possible representation of a grid — and the correct starting point for a custom engine — is a **dense 2D array**: a flat `bool` or small-integer array of size `width * height`, indexed as `walkable[y * width + x]`, conceptually a special case of a graph where nodes are grid coordinates and edges are implicit cardinal/diagonal offsets rather than stored explicitly [11]. This has three concrete advantages that matter for a from-scratch engine: it is trivial to implement correctly, it has excellent cache locality (a row of the grid is contiguous in memory, so `neighbors()` queries touch nearby memory), and per-cell lookup, mutation, and iteration are all `O(1)` or `O(width * height)` respectively with no hashing or pointer-chasing overhead. The cost is that memory usage scales with the *full bounding rectangle* of the level regardless of how much of it is actually walkable space, which becomes wasteful for maps that are mostly empty space (e.g., a large outdoor world with a small fraction of it navigable) or maps whose extents are not known ahead of time.

A **sparse/hashed grid** — typically a hash map from `(x, y)` (or `(x, y, z)`) integer-coordinate keys to per-cell data, only storing entries for cells that exist or are non-default — trades that locality and simplicity for the ability to represent an unbounded or extremely sparse world without allocating for empty space. This is the right tradeoff when the "interesting" grid is a small, scattered subset of a conceptually infinite coordinate space (procedurally generated or streamed worlds, voxel worlds where most volume is air, or a level built incrementally at runtime with no fixed bounds), but it costs a hash lookup (with associated cache-miss risk) per cell access instead of a flat array index, and iterating "all neighbors of a cell" or "all cells in a region" is asymptotically the same complexity but constant-factor slower than the dense array case. For a level-sized 2D grid with known, fixed dimensions — which is the natural starting point described in Section 8 — a dense array is very hard to beat, and the added complexity of a hashed structure is not justified until the world genuinely does not fit that assumption.

### 2.2 2.5D Heightfield Grids vs. True 3D Voxel Grids

For anything beyond a flat 2D level, there is an important intermediate representation between "2D grid" and "full 3D voxel grid": the **2.5D heightfield**, where the world is still represented as a 2D array indexed by `(x, y)`, but each cell stores a *height* (and possibly walkable-surface metadata) rather than a boolean. This is exactly the representation Recast, the navmesh-generation library underlying Unreal's RecastNavMesh, builds internally: Recast's pipeline first voxelizes input collision geometry, filters out voxels an agent could not stand on or move through, and derives a walkable heightfield surface from what remains, before further processing that surface into polygons [12][16]. A heightfield is the right choice whenever the world genuinely has a single walkable surface per `(x, y)` column at any given time — a terrain, a building with one floor per grid cell, a top-down or isometric game with simple verticality (stairs, ramps, cliffs). It keeps the same `O(width * height)` memory profile and array-indexing performance as a flat 2D grid, while adding the ability to represent slopes, elevation changes, and (with a small extension) drop-offs an agent can fall from but not climb.

A **true 3D voxel grid** — a dense or sparse `(x, y, z)` volume, or a hierarchical structure over one such as an **octree** — is necessary only when a single `(x, y)` column can have *multiple, independently walkable* layers: multi-floor interiors with overlapping footprints, caves with tunnels passing over one another, or full flight/swimming/zero-gravity movement where an agent can occupy essentially any point in 3D space rather than a single surface height per column. A dense 3D array costs `O(width * height * depth)` memory, which grows fast — an octree instead subdivides space recursively, storing fine detail only where the map's geometry is complex and collapsing large empty or uniformly-solid regions into single nodes, so memory scales with surface complexity rather than raw volume [9]. The 26-neighbor cubic grid used in the Lazy Theta\* paper's own 3D benchmarks is the direct analogue of the 8-neighbor 2D grid extended one dimension further, and the same paper's finding — that grid-constrained paths in a 26-neighbor cubic grid can be roughly 13% longer than the true shortest path, versus roughly 8% for an 8-neighbor 2D grid — is itself a data point in favor of pairing any true-3D voxel pathfinding with any-angle smoothing (Section 4), since the geometric approximation error is worse in 3D than in 2D [9].

### 2.3 Choosing a Representation for a Custom Engine

For the specific developer this report is written for — starting with a 2D or simple 2.5D grid and planning to grow the system over time — the practical guidance is: start with a dense 2D (or 2.5D heightfield) array sized to the level's known bounds. This is simplest to get right, fastest to iterate on, and is exactly the representation every algorithm in Section 1 is described against. Move to a heightfield only when verticality (slopes, floors, ramps) needs representing, which is a cheap incremental change to the same dense-array foundation. Reach for a sparse/hashed structure or a true voxel/octree representation only when a concrete requirement demands it — unbounded/streamed worlds, or genuinely overlapping multi-layer 3D navigable space — since both add real implementation and performance complexity that is wasted effort if the simpler representation would have sufficed.

---

## 3. Hard-Blocking a Forbidden Region (Point B)

This is the section that answers the core requirement driving this report: a forbidden zone (point B, or any region around it) must be **structurally excluded** from the graph the search algorithm operates over, so that no path the search can possibly return passes through it — not merely a region the search is discouraged from entering by cost.

### 3.1 Encoding Permanent Exclusion Zones

The mechanism is straightforward and is exactly how Unreal's Recast-based navmesh implements its own hard exclusions: a cell (or navmesh polygon) can be flagged as belonging to a **non-walkable area class**, and the generation/search process treats that flag as absolute. Unreal's own documentation states this explicitly for its built-in `NavArea_Null` area class used by Nav Modifier Volumes: marking a region with a non-walkable area means "the Navigation Mesh will not generate navigation data inside this volume" at all [14]. This is the structural distinction that matters: a *cost* modifier (Unreal's `NavArea_Obstacle`, or Unity's costed `NavMeshModifierVolume` areas) still leaves the region part of the searchable graph, just expensive to traverse, so an agent with no better option can and will cross it; a *non-walkable/null* area means the region is never even a candidate — there is no polygon or cell there for the search to consider [14][17]. The equivalent operation on a raw grid, rather than a generated navmesh, is simpler still: maintain a `walkable[x, y]` (or `walkable[x, y, z]`) boolean array or bitset, and have your neighbor-generation function skip any cell where `walkable == false` unconditionally, before the cell is ever inserted into the open list.

```
// Hard-block encoding on a dense grid
struct Grid {
    int width, height;
    std::vector<bool> walkable;   // false == permanently excluded (point B / forbidden zone)

    bool isWalkable(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        return walkable[y * width + x];
    }

    void blockRegion(int x0, int y0, int x1, int y1) {   // e.g. mark forbidden zone B
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                walkable[y * width + x] = false;
    }
};

// A* neighbor generation with the exclusion baked in structurally
std::vector<Cell> neighbors(const Grid& grid, Cell c) {
    std::vector<Cell> result;
    for (auto [dx, dy] : EIGHT_DIRECTIONS) {
        Cell n{c.x + dx, c.y + dy};
        if (!grid.isWalkable(n.x, n.y))
            continue;                      // hard exclusion: never generated, never enqueued
        result.push_back(n);
    }
    return result;
}
```

Because a non-walkable cell is never returned by `neighbors()`, it can never be inserted into A\*'s open list, can never be assigned a `g`-value, and can never appear as a `came_from` parent of any other node — it is not in the graph the search algorithm can see at all, which is the guarantee the "hard block, not soft penalty" requirement calls for. This is a stronger and simpler guarantee than a large finite cost penalty can ever provide: a sufficiently long detour around a heavily-penalized-but-not-excluded region can still, in principle, cost more than crossing it, whereas an excluded cell has no cost at all because it is never a valid part of any candidate path.

### 3.2 Diagonal Movement and Corner-Cutting Near Blocked Cells

A hard-blocked region is only actually impassable if the *movement rule itself* is also checked against it, not just cell occupancy — the classic gap here is diagonal movement through the gap between two orthogonally-blocked cells. Consider a diagonal step from `(x, y)` to `(x+1, y+1)` where `(x+1, y)` and `(x, y+1)` are both blocked: geometrically, the two blocked cells share only a single corner point, and depending on how thick your engine treats cell walls, a diagonal move here either clips through that shared corner or is a legitimate way around it. The Theta\* paper's own problem formulation makes an explicit assumption on this: "we assume for ease of description that the path can pass through diagonally touching blocked cells" [8] — but that is a *choice*, not a universal rule, and it is exactly the choice a hard-block implementation must make deliberately, because getting it wrong either lets an agent visually clip through the corner of your forbidden zone B, or unnecessarily forbids legitimate diagonal moves next to a single blocked cell.

The standard, conservative rule — and the one to use for a hard-blocked exclusion zone specifically, since any leniency here is a literal crack in the "guaranteed to never enter B" requirement — is: **a diagonal move from `(x, y)` to `(x+dx, y+dy)` is only legal if both orthogonally-adjacent cells `(x+dx, y)` and `(x, y+dy)` are also walkable.** If either of the two "flanking" cells is blocked, the diagonal step is rejected even though the destination cell itself might be walkable, because the move would otherwise let the agent's path clip the corner of the blocked cell(s) — an especially important rule right at the boundary of a hard-excluded region, since a permitted corner-cut there is functionally a permitted partial entry into forbidden territory if your collision/visual representation gives the excluded cell any physical extent at all.

```
// Corner-cutting check for 8-directional movement adjacent to a hard-blocked cell
bool isDiagonalMoveLegal(const Grid& grid, int x, int y, int dx, int dy) {
    // dx, dy in {-1, 0, 1}, and this is only meaningful when both are nonzero (diagonal)
    if (dx == 0 || dy == 0) return true;         // not a diagonal move
    bool flankA = grid.isWalkable(x + dx, y);     // cell orthogonally adjacent, along x
    bool flankB = grid.isWalkable(x, y + dy);     // cell orthogonally adjacent, along y
    return flankA && flankB;                      // require BOTH flanks open — no corner-cutting
}

// Neighbor generation, extended with the corner-cutting rule
std::vector<Cell> neighbors(const Grid& grid, Cell c) {
    std::vector<Cell> result;
    for (auto [dx, dy] : EIGHT_DIRECTIONS) {
        int nx = c.x + dx, ny = c.y + dy;
        if (!grid.isWalkable(nx, ny)) continue;
        if (!isDiagonalMoveLegal(grid, c.x, c.y, dx, dy)) continue;   // reject the corner-cut
        result.push_back({nx, ny});
    }
    return result;
}
```

This rule interacts directly with Jump Point Search's forced-neighbor logic in Section 1.4 — JPS's forced-neighbor cases are themselves defined around exactly this situation, where an obstacle adjacent to the current node forces evaluation of a neighbor that would otherwise be pruned [5][6] — so a JPS implementation needs the same corner-cutting convention applied consistently in its jump function, or its jump points will not match the paths a plain A\* with the same convention would find.

### 3.3 Dynamic Obstacle Updates and Path Cache Invalidation

A forbidden zone that is fixed for the entire level's lifetime only needs the encoding above, applied once at load time. Most games also need to add or remove blocked regions at runtime — a temporary hazard, a destructible wall, a door closing, or (relevant to this report's framing) a forbidden zone B that only becomes forbidden partway through play, such as a spreading hazard or an enemy-controlled area. Two production navmesh systems illustrate the two main approaches, and both apply directly to a hand-rolled grid:

**Rebuild the affected region, not the whole map.** Unreal's RecastNavMesh exposes a `RuntimeGeneration` setting with three modes: `Static` (the navmesh never changes after initial generation — spawned actors that would affect navigation are simply ignored), `Dynamic` (actors relevant to navigation "dirty" the base navmesh and trigger rebuilding of the navigation tiles they overlap), and `DynamicModifiersOnly` (a middle ground where only actors carrying an explicit Nav Modifier component can trigger a rebuild, without full dynamic geometry re-scanning) [16][14]. The important architectural detail is that Unreal's navmesh is internally tiled specifically so that a change can dirty and rebuild only the tiles it actually overlaps, rather than the entire level's navigation data [15][16] — the same principle applies directly to a hand-rolled grid: track blocked-region changes as a bounding rectangle (or set of touched cells) and only re-run whatever expensive step your system needs (e.g., re-flagging `walkable[]` cells, or re-baking any JPS+-style preprocessed jump-distance table from Section 1.4) over that bounding rectangle, not the whole grid.

**Carve, rather than rebuild, for objects that move often.** Unity's `NavMeshObstacle` component takes a different approach tuned for objects that move frequently rather than being placed once: with **carving** disabled, the obstacle behaves like a soft, local-avoidance-only obstruction that agents steer around reactively but which is invisible to global path *planning*; with carving enabled, a stationary obstacle "carves a hole" directly in the navmesh so that global pathfinding correctly routes around it [17]. Because re-carving on every frame of a moving object would be wasteful, Unity defaults to a **"Carve Only Stationary"** mode: while an obstacle is moving, agents rely purely on local reactive avoidance, and the navmesh hole is only recomputed once the obstacle has been still for a short duration, explicitly trading a one-frame-or-so delay for avoiding constant re-carving cost on fast-moving objects [17]. This is a generally useful pattern for a hand-rolled grid too — distinguish "obstacles that need to structurally exclude cells from the pathfinding graph" (rare changes, worth a rebuild/recompute) from "obstacles agents just need to steer around locally" (Section 6), and only pay the grid-mutation-plus-invalidation cost for the former.

**When to invalidate a cached path.** Any in-flight agent path that was computed before a grid mutation needs to be checked against the change, not blindly trusted: the cheapest correct check is to walk the cached path's remaining cells (or, for a smoothed/any-angle path, do line-of-sight/segment-intersection tests as in Section 4) against the newly-blocked region and only trigger a full replan if the path actually intersects the change — this avoids replanning every agent on the map every time any obstacle anywhere changes. HPA\*'s own paper frames this exact tradeoff for its hierarchical structure: it recomputes the abstract-graph information only for the modified cluster and leaves the rest of the hierarchy untouched, and separately notes that when a computed path becomes blocked by another moving unit, the practical answer is to "replan for another abstract path from the current position of the character" rather than eagerly re-solving from scratch on every minor change [10]. The general rule that carries over cleanly to a custom engine: mutate only the region that changed, invalidate/replan only the agents whose cached path actually crosses that region, and prefer localized incremental recomputation over global re-solves wherever the data structure (tiled navmesh, clustered HPA\* abstraction, or a plain grid with a dirty-rectangle tracker) makes that possible.

---

## 4. Path Smoothing and Any-Angle Paths

### 4.1 String-Pulling / Funnel-Style Post-Processing

A path returned by grid-constrained A\* or JPS is a sequence of grid cells, which — even when numerically shortest under the grid's own move set — tends to look artificially blocky, since it can only turn at grid-aligned headings. **String-pulling** (also called path smoothing, or "pulling the path taut" like a string around obstacle corners) is a post-processing pass that walks the returned path and removes any waypoint that is not actually necessary, by checking line-of-sight between non-adjacent waypoints. The Theta\* paper documents the specific algorithm it benchmarks against under the name **A\* with Post-Smoothing (A\* PS)**, attributing the technique to Botea, Müller, and Schaeffer's HPA\* paper [8][10]: starting from the first vertex on the raw path as "current," repeatedly check whether the current vertex has line-of-sight to the vertex *two steps ahead* on the path (the parent of its parent); if so, remove the intermediate vertex from the path and repeat the same check one step further ahead; if not, advance "current" to the next vertex on the path and repeat from there [8].

```
// String-pulling post-process — A* PS, as described in the Theta* paper [8]
// path = [s0, s1, s2, ..., sn], as returned by grid A* / JPS
List<Cell> stringPull(List<Cell> path, Grid grid) {
    List<Cell> smoothed = { path[0] };
    int current = 0;
    int lookahead = 2;
    while (lookahead < path.size()) {
        if (hasLineOfSight(grid, path[current], path[lookahead])) {
            lookahead++;                 // skip the intermediate waypoint, look further ahead
        } else {
            smoothed.push_back(path[lookahead - 1]);
            current = lookahead - 1;     // that waypoint was actually necessary — keep it
            lookahead = current + 1 + 1;
        }
    }
    smoothed.push_back(path.back());
    return smoothed;
}
```

String-pulling's appeal is that it is entirely decoupled from the search algorithm — any grid pathfinder's output can be fed through it — and its cost is proportional to path length times the cost of a line-of-sight check, which is comparatively cheap. Its weakness, demonstrated directly by the Theta\* paper's own worked counterexample, is that it can only ever *remove* waypoints the underlying grid search already committed to; it cannot discover a genuinely shorter route the grid-constrained search never considered in the first place. In the paper's Figure 2 example, plain A\* returns a path that happens to already be a shortest path *under the grid's edge set*, so post-smoothing has literally nothing to remove — yet the true geometric shortest path (available to any-angle search) is shorter still [8].

### 4.2 Any-Angle Search vs. Post-Process Smoothing

Theta\* and Lazy Theta\* (Section 1.5) solve the same "blocky grid path" problem by folding line-of-sight reasoning *into* the search itself, so the algorithm can discover and prefer a genuinely shorter any-angle route during expansion, rather than only cleaning up whatever grid-constrained route A\* happened to settle on afterward. The Theta\* paper's own benchmark result is the clearest available evidence for this: Basic Theta\* found shorter paths than A\* with post-smoothing 95% of the time on the paper's 500×500, 20%-obstacle random grids, and Basic Theta\* had "the best trade-off between path length and runtime among all path planning methods" tested, including A\* PS and Field D\* [8].

The practical tradeoff for a custom engine is implementation complexity versus path quality: string-pulling is a small, self-contained post-process that can be bolted onto whatever grid search you already have (plain A\*, JPS, JPS+) with no changes to the search itself, and is a very reasonable first step; Theta\*/Lazy Theta\* require modifying the core `UpdateVertex` step of the search and adding a fast line-of-sight primitive the search will call many times per expansion (a cost Lazy Theta\* specifically exists to reduce, per Section 1.5) [9], but produce measurably better (shorter, more direct, fewer heading-change) paths in exchange. For a solo developer's staged roadmap (Section 8), string-pulling on top of grid A\*/JPS is the pragmatic first implementation — it is a small amount of code with a large visible payoff — with Theta\*-style any-angle search as a later upgrade only if path quality genuinely becomes a problem post-smoothing does not fix (e.g., very open, obstacle-sparse maps where the grid-constrained shortest path is a poor approximation of the true shortest path to begin with, per the ~8%/~13% figures from Section 1.5 [9]).

---

## 5. Scaling to Many Simultaneous Agents

### 5.1 Hierarchical Pathfinding (HPA\*)

Botea, Müller, and Schaeffer's HPA\* addresses the case where either the map is very large or many agents need paths computed frequently, by trading strict optimality for a large constant-factor speedup: the map is partitioned into fixed-size **clusters** (the paper's own recommended default is a cluster size of roughly 10x10 cells [5]), the optimal crossing distance between every pair of **entrances** on a cluster's border is precomputed and cached once, and a global search operates on a much smaller **abstract graph** whose nodes are entrances and whose edges are either intra-cluster (cached, precomputed distances) or inter-cluster (adjacent entrances directly across a cluster border) [10]. A path query only needs to insert the true start and goal into this abstract graph — connecting them to their local cluster's entrances — and then run ordinary A\* over the much smaller abstract graph rather than the full grid; the hierarchy generalizes to more than two levels by clustering clusters, trading more preprocessing time and memory for a smaller top-level search space on very large maps [10]. The paper's own benchmark on scaled *Baldur's Gate* maps reports up to a 10-fold speed improvement over a highly-optimized A\* baseline, in exchange for path lengths within about 1% of true optimal after a string-pulling-style smoothing pass is applied to the abstract path [10].

HPA\*'s architecture has two properties specifically valuable for scaling to many agents rather than just large maps. First, because it "returns a complete path of sub-problems" rather than one monolithic path, an agent can start moving on the first refined segment of its abstract path while later segments are refined lazily, which matters when dozens of agents all need a path in the same frame and full low-level refinement of every agent's entire route is too expensive to do all at once [10]. Second, its clustered structure is naturally well-suited to the "recompute only what changed" dynamic-obstacle pattern from Section 3.3: the paper explicitly notes that a local topology change (its own example: "a bomb destroys a bridge") only requires recomputing the cached entrance-to-entrance distances for the modified cluster, leaving the rest of the abstraction untouched, and that when a computed path is blocked by another moving agent, the practical response is to replan cheaply at the abstract level and only refine the new abstract path to low-level detail as the agent actually needs it [10].

### 5.2 Flow Fields / Vector Fields

Where HPA\* speeds up *individual* path queries, flow fields change the shape of the problem entirely for the specific case of many agents converging on the same (or a small number of shared) goal(s) — a common situation in RTS-style unit movement or crowd simulation. A flow field is computed once, per destination, by running a single Dijkstra-style search *backward* from the goal cell across the entire relevant portion of the grid, filling in every reachable cell's cost-to-goal, exactly the "distance field" produced by the uniform-cost-search sweep described in Section 1.1 [2], and then deriving, per cell, a direction vector pointing toward whichever neighbor has the lowest cost-to-goal. Once this field exists, *every* agent moving toward that same destination can simply look up its current cell's direction vector each frame — an `O(1)` operation per agent per frame — rather than each agent running its own independent A\*/JPS search. The one-time cost of building the field is paid once regardless of how many agents share the destination, so the technique's value scales directly with agent count: it is a poor fit for a handful of agents with distinct individual goals (where the field-construction overhead is not amortized over enough lookups to pay for itself) and a very strong fit for large numbers of agents converging on a shared point or region.

For a custom engine growing incrementally from the roadmap in Section 8, flow fields are best understood as a specialized, later addition layered on top of — not a replacement for — the grid representation and hard-block encoding already built in Sections 2–3: the backward Dijkstra sweep that builds the field must itself respect the same `walkable[]` hard-exclusion data described in Section 3.1, simply by having its own neighbor-generation function skip non-walkable cells exactly as the forward-search `neighbors()` function in Section 3.1 does, so a forbidden zone B remains structurally unreachable in the flow field's own coverage exactly as it is for point-to-point A\*.

### 5.3 Path Caching

The simplest scaling technique available, and one that composes with every other technique in this report, is to cache and reuse path results rather than recomputing from scratch whenever avoidable. Two forms are worth distinguishing for a custom engine. **Full-path caching** stores previously computed start→goal routes (or, for symmetric movement costs, treats a cached goal→start route as reusable in reverse) keyed by start/goal cell pairs, useful when the same handful of routes get requested repeatedly — patrol routes, common spawn-to-objective paths, or any scripted/repeated traversal — and is invalidated exactly as described in Section 3.3: only when a grid mutation's affected region actually intersects the cached route. **Structural caching** is what HPA\*'s cluster-local entrance-to-entrance distances already are (Section 5.1) — precomputed sub-results that many different full-path queries can reuse without recomputation, which is a strictly more powerful and more generally applicable form of caching than caching complete point-to-point routes, since it pays off even when no two agents share exactly the same start and goal.

### 5.4 Time-Slicing Pathfinding Across Frames

Even a fast search (JPS, JPS+, or HPA\*'s abstract-level search) has a cost, and requesting dozens of full path computations in the same frame — a common situation right after a wave of units spawns, or a formation issues a group-move order — can produce a visible frame-time spike if all of them are solved synchronously in one frame. The standard mitigation, applicable to any of the search algorithms in Section 1 without modifying the algorithm itself, is to **time-slice** path requests: maintain a queue of pending path requests, and each frame, budget a fixed amount of search work (either a fixed number of algorithm iterations/node-expansions, or a fixed wall-clock time slice measured against a hard per-frame budget) across that queue rather than letting any single request run to completion synchronously. This requires the underlying search to be expressible as a resumable step function — expand a bounded number of nodes, then return control to the frame loop and continue from the same open-list/closed-list state next frame — which is a natural fit for A\*'s frontier/`came_from`/`cost_so_far` structure from Section 1.1, since that entire state can simply persist across frames rather than living on a single function's call stack. Two complementary practices reduce how much time-slicing pressure ever needs to be relieved in the first place: preferring the fastest sufficient algorithm for the actual map (Section 1.6's benchmark table makes clear how large a difference JPS/JPS+ or HPA\* make relative to plain A\* under load), and applying the lazy/incremental refinement pattern HPA\* itself uses — computing only the near-term portion of a long path immediately and deferring refinement of the remainder, so an agent can start moving on frame one without the full path ever needing to be solved to completion synchronously [10].

---

## 6. Local Avoidance and Steering

Global pathfinding (Sections 1–5) answers "what route gets this agent from A to C without ever entering B," computed against the *static* (or slowly-changing) navigable-space representation. It does not, by itself, prevent two agents both correctly following their own valid paths from colliding with each other, since neither agent's individually-computed path has any knowledge of where other agents will be at the same moment. Local avoidance is a separate, complementary layer that runs every frame on top of whatever global path each agent is already following, adjusting each agent's actual velocity to avoid other agents (and, incidentally, to react to obstacles more responsively than a full replan would allow) without discarding or replacing the global path itself.

### 6.1 Reciprocal Velocity Obstacles: RVO and ORCA

Van den Berg, Guy, Lin, and Manocha's ORCA (Optimal Reciprocal Collision Avoidance) formalizes multi-agent local avoidance as a per-agent, per-frame optimization problem, building on the earlier Velocity Obstacle concept: for two agents A and B, the **velocity obstacle** `VO(A|B)` is the set of relative velocities that would cause a collision within some time window `tau`, geometrically a truncated cone in velocity space [18]. ORCA's contribution is to derive, for each pair of nearby agents, a **half-plane** of permitted velocities in agent A's own velocity space — the set of velocities on the correct side of a line perpendicular to the minimum "escape vector" `u` needed to avert the predicted collision, positioned so that each agent takes on exactly half the responsibility for avoiding the other, which is what makes the approach *reciprocal* and collision-free without any communication between agents [18]. Formally, `ORCA(A|B) = { v | (v - (v_A^opt + u/2)) . n >= 0 }`, where `n` is the outward normal of the velocity obstacle boundary at the closest escape point [18]. Each agent computes one such half-plane per nearby agent, intersects all of them together with its own maximum-speed constraint, and then solves a small linear program to find the velocity inside that intersected feasible region closest to its preferred velocity (the direction its global path currently wants it to move) [18]:

`ORCA(A) = D(0, v_max_A) ∩ (intersection over all other nearby agents B of ORCA(A|B))`
`v_new_A = argmin over v in ORCA(A) of ||v - v_pref_A||`

Because each half-plane constraint is linear, this reduces to a low-dimensional linear program solvable in expected `O(n)` time in the number of nearby constraints, and the paper reports simulations with thousands of agents computing collision-free actions for all of them "in only a few milliseconds," fully parallelizable since each agent's velocity is computed independently [18]. In genuinely dense configurations the linear program can become infeasible (no velocity satisfies every constraint simultaneously); the paper's fallback is a relaxed 3D linear program that instead selects the velocity minimizing maximum constraint violation — the "safest possible" velocity rather than a strictly guaranteed collision-free one [18]. Static obstacles are handled by the same underlying formalism: **RVO2**, the reference open-source implementation from the same research group [20], exposes `addObstacle()` for defining rigid polygonal obstacle geometry (processed once via `processObstacles()` before simulation begins) which agents are guaranteed to never cross during simulation, in addition to the reciprocal per-agent avoidance handled via `setAgentDefaults()`, `addAgent()`, `setAgentPrefVelocity()`, and a per-frame `doStep()` call that advances every agent's ORCA-derived velocity [19]. This makes RVO2's static-obstacle mechanism directly analogous in spirit to Section 3's hard-blocked grid cells, just expressed in continuous velocity-space geometry rather than discrete grid occupancy — a genuinely impassable boundary rather than a costed deterrent, which is worth knowing if a local-avoidance layer is ever added around the same forbidden zone B that global pathfinding already hard-excludes, since both layers can enforce the exclusion independently and redundantly (a useful defense-in-depth property, not a conflict).

### 6.2 Boids-Style Separation Steering

A lighter-weight alternative or complement to full ORCA, particularly for a first implementation, is classic reactive **separation steering** in the boids tradition: each frame, each agent computes a repulsive vector away from every other agent within some neighbor radius (typically weighted inversely by distance, so closer agents push harder), sums that with its desired "move toward next path waypoint" vector, and clamps the result to the agent's maximum speed and turn rate. This is dramatically simpler to implement than ORCA's linear-programming solve and requires no velocity-obstacle geometry — it is a small number of vector operations per neighboring agent per frame — but it gives up ORCA's formal collision-free guarantee and can, without careful tuning, produce jitter, oscillation, or agents getting locally "stuck" pushing against each other in dense configurations, precisely the class of problem the reciprocal, half-plane-based ORCA formulation was designed to solve rigorously rather than heuristically [18].

### 6.3 Combining Global Pathfinding with Local Avoidance

The two layers have a clean division of responsibility that is worth keeping explicit in a custom engine's architecture: global pathfinding (Sections 1–5) decides *the route*, computed against the grid/navmesh representation with any permanently forbidden zone (point B) hard-excluded as in Section 3, and is recomputed relatively rarely (only when the goal changes, or when a grid mutation actually invalidates the cached route, per Section 3.3); local avoidance (this section) decides *the exact velocity this frame*, recomputed every frame, and never needs to know anything about the static level geometry beyond what the global path and any nearby dynamic obstacles already tell it. In practice this means an agent continuously steers toward the next waypoint (or the next string-pulled/any-angle waypoint, per Section 4) of its globally-computed path, with ORCA or separation steering adjusting the actual per-frame velocity to avoid colliding with other agents converging on similar routes — which is exactly the scenario this report's framing anticipates: multiple agents all independently routing around the same hard-excluded zone B, needing a second layer to keep them from colliding with *each other* while they do.

---

## 7. Real-World Grounding: Unreal and Unity

This section is included as validation and comparison, not as the report's primary subject: it confirms that the hard-exclusion, corner-cutting, and dynamic-update principles laid out in Section 3 are exactly how two of the most widely shipped, heavily-battle-tested engine navigation systems in the industry solve the same problem, and are not a bespoke or unusual approach.

### 7.1 Unreal: Recast/Detour, Nav Mesh Bounds Volumes, Nav Areas and Modifiers

Unreal's navigation system is built on **Recast** (navmesh generation) and **Detour** (runtime pathfinding/queries), the open-source library Epic integrated as `RecastNavMesh` [12][15]. Recast's generation pipeline rasterizes input collision geometry into voxels, filters out voxels that are not walkable (too steep, too low a ceiling, etc.), derives a walkable heightfield, and re-triangulates the resulting walkable regions into the navigation polygon mesh actual queries run against [12] — precisely the "voxelize, then derive a 2.5D heightfield" pipeline discussed in Section 2.2. At the authoring level, a **Nav Mesh Bounds Volume** defines the region of the level Unreal will generate navigation data for at all; navmesh generation happens automatically as soon as such a volume is added to a level or resized [15]. Within that bounds volume, a **Nav Modifier Volume** applies a chosen **Nav Area** class to override generation behavior in a sub-region: the built-in `NavArea_Null` class produces a genuine hard exclusion — the documentation is explicit that the navmesh "will not generate navigation data inside this volume" at all — while `NavArea_Obstacle` instead assigns a high but finite traversal cost, and custom Nav Area classes can assign arbitrary intermediate costs (e.g., a "shallow water" area agents will use only if it is genuinely the best remaining route) [14]. A **Nav Modifier Component** attached to an actor achieves the same per-region area override driven by an actor's own shape rather than a placed volume, and works at runtime under the `Dynamic` or `DynamicModifiersOnly` runtime-generation modes [14][16]. For dynamic changes generally, Unreal's `RuntimeGeneration` setting on `RecastNavMesh` governs whether spawned/moved actors that affect navigation ever trigger a rebuild at all (`Static` = never), a full tile rebuild (`Dynamic`), or a lighter-weight rebuild restricted to actors carrying an explicit Nav Modifier component (`DynamicModifiersOnly`); under either dynamic mode, changes "dirty" the base navmesh and rebuilding is scoped to the specific navigation tiles a change actually overlaps, rather than the whole level [16].

### 7.2 Unity: NavMesh, NavMeshModifierVolume, Area Costs, NavMeshObstacle Carving

Unity's equivalent system, the **AI Navigation** package (`com.unity.ai.navigation`), centers on a small set of components: `NavMeshSurface` defines what gets baked and where, `NavMeshModifier` and `NavMeshModifierVolume` override area type and cost per-object or per-volume, `NavMeshLink` connects otherwise-disjoint navigable regions (jumps, teleports), and `NavMeshAgent`/`NavMeshObstacle` handle runtime agent movement and obstruction respectively [21]. Precisely mirroring Unreal's null-area-vs-costed-area distinction from Section 7.1, a `NavMeshModifierVolume` can be configured either to mark its volume as **non-walkable** — a genuine hard exclusion, baked out of the mesh entirely and requiring a re-bake to change — or to tag it as a **custom area** (the documentation's own example is water) carrying its own finite traversal cost, which the agent "can still cross... it just weighs the price and prefers the cheaper route," the same soft-deterrent-versus-hard-exclusion distinction this report's Section 3.1 is built around [17][21]. For obstacles that need to change at runtime without a full re-bake, `NavMeshObstacle` offers two modes: with **carving** off, the obstacle only affects local reactive avoidance and is invisible to global path planning; with carving on, a stationary obstacle "carves a hole" directly into the baked navmesh so that global pathfinding correctly routes around it — and because re-carving every frame for a fast-moving object would be wasteful, Unity defaults to **"Carve Only Stationary,"** deferring the hole recompute until the obstacle has been still for a short duration and relying on local avoidance alone while it is actually moving, explicitly accepting a small delay (documented as roughly one frame once the obstacle does settle) in exchange for not paying continuous re-carving cost [17]. This is the same "structural rebuild for rare/settled changes, cheap reactive avoidance for frequent/fast changes" split laid out generally in Section 3.3.

---

## 8. Recommended Implementation Roadmap

This roadmap sequences the material above for the specific developer this report is written for: solo/small-team, building a custom engine from scratch, already committed to starting with a grid representation, with a firm, non-negotiable requirement that a forbidden zone B is a **hard structural exclusion**, never a cost penalty — and an explicit intent to improve the system incrementally over time rather than build the "final" version immediately.

### 8.1 Phase 1 — Minimum Viable: Hard-Blocked Grid A\*

Build, in order:

1. **A dense 2D (or 2.5D heightfield, if verticality is needed immediately) grid** as described in Section 2.1/2.2 — a flat array of walkable flags sized to the level's known bounds. This is the correct starting representation regardless of whether the eventual game is 2D or 3D; do not reach for a sparse/hashed structure or a full voxel/octree until a concrete requirement (unbounded streamed world, or genuinely overlapping multi-layer 3D navigable space, per Section 2.3) demands it.
2. **Hard-block encoding for the forbidden zone**, exactly as in Section 3.1: a `walkable[]` boolean array/bitset, with neighbor-generation unconditionally skipping any non-walkable cell before it can ever enter the open list. This is what actually satisfies the "guaranteed to never enter B" requirement — verify it by construction (the excluded cells are structurally absent from the graph the search can see), not by testing that the search "usually" avoids them.
3. **Plain A\*** over that grid (Section 1.1/1.2), using octile distance as the heuristic for 8-directional movement or Manhattan distance for 4-directional movement (Section 1.3) — whichever matches the movement rules you actually allow.
4. **The corner-cutting rule from Section 3.2**, applied to diagonal moves — required from day one, not a later polish pass, because an unhandled corner-cut right at the boundary of the forbidden zone is a direct, literal violation of the hard-block requirement, not a cosmetic issue.

This alone is a complete, correct system: guaranteed-optimal paths, and a genuinely hard-excluded forbidden zone. Everything after this point is a performance or quality improvement layered on top of the same grid and the same exclusion mechanism, not a replacement for it.

### 8.2 Phase 2 — Smoothing and Performance

Once Phase 1 works correctly, in roughly this order:

1. **String-pulling smoothing** (Section 4.1) as a post-process on Phase 1's output — small amount of code, immediately visible improvement to how paths look, and it composes with everything that comes later without requiring changes to the search itself.
2. **Jump Point Search** (Section 1.4) as a drop-in replacement for plain A\*'s node expansion, once search performance (not path quality) becomes the bottleneck — this requires no additional memory and no preprocessing step, so it is a strictly cheaper upgrade than HPA\* or JPS+ for the same optimality guarantee, and the corner-cutting convention from Section 3.2 needs to be applied consistently inside JPS's own jump function.
3. **Basic dynamic-obstacle support** (Section 3.3): a bounding-rectangle "dirty region" tracker so that adding/removing a blocked zone at runtime only re-touches the affected cells, plus a cheap invalidation check (does an agent's cached remaining path intersect the changed region?) before triggering any replan.

### 8.3 Phase 3+ — Optional / Later

These are genuinely optional, and should each be added only once a concrete, observed problem justifies the added complexity — not preemptively:

- **JPS+ preprocessing** (Section 1.4), if profiling shows JPS's runtime jump-scanning is still a bottleneck on a large, mostly-static grid — adds a one-time bake step and memory cost in exchange for the largest further speedup available for a single-agent, uniform-cost query.
- **Theta\*/Lazy Theta\*** (Section 1.5), if string-pulled grid paths are still visibly suboptimal for the game's specific layouts (very open maps are the case where this matters most, per the ~8%/~13% figures in Section 1.5) — adds a line-of-sight primitive and a modified vertex-update rule to the core search.
- **HPA\*** (Section 5.1), once either map size or simultaneous-agent count makes per-query search cost the actual bottleneck rather than path quality — adds an offline clustering/preprocessing step and an abstract-graph layer on top of the existing grid, and its clustered structure also gives the cheapest path to scoped dynamic-obstacle recomputation described in Section 3.3.
- **Flow fields** (Section 5.2), specifically and only once many agents are routinely converging on the same shared destination(s) — otherwise the one-time field-construction cost is not amortized over enough per-agent lookups to be worthwhile, and per-agent A\*/JPS remains simpler and sufficient.
- **ORCA / RVO2-style local avoidance** (Section 6.1), once multiple agents following independently-computed global paths are visibly colliding with or walking through each other — a simpler boids-style separation steering pass (Section 6.2) is a reasonable interim step before committing to a full ORCA implementation, since it requires far less code even though it lacks ORCA's formal guarantees.

Each of these is additive to, not a replacement for, the Phase 1 foundation: the hard-blocked grid and its exclusion guarantee from Section 3 remain the substrate every later algorithm in this roadmap searches over.

---

## References

1. Hart, P. E.; Nilsson, N. J.; Raphael, B. "A Formal Basis for the Heuristic Determination of Minimum Cost Paths." *IEEE Transactions on Systems Science and Cybernetics*, SSC-4(2):100–107, 1968. https://doi.org/10.1109/TSSC.1968.300136
2. Patel, A. (Red Blob Games). "Introduction to A*." https://www.redblobgames.com/pathfinding/a-star/introduction.html
3. Patel, A. "Heuristics." Amit's Game Programming Pages / Red Blob Games. https://theory.stanford.edu/~amitp/GameProgramming/Heuristics.html
4. Patel, A. (Red Blob Games). "Implementation of A*." https://www.redblobgames.com/pathfinding/a-star/implementation.html
5. Harabor, D.; Grastien, A. "Online Graph Pruning for Pathfinding on Grid Maps." *Proceedings of the 25th AAAI Conference on Artificial Intelligence (AAAI-11)*, pp. 1114–1119, 2011. https://doi.org/10.1609/aaai.v25i1.7994 (full text: http://grastien.net/ban/articles/hg-aaai11.pdf)
6. Rabin, S.; Silva, F. "JPS+: An Extreme A\* Speed Optimization for Static Uniform Cost Grids." In *Game AI Pro 2*, Chapter 14. CRC Press, 2015. http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter14_JPS_Plus_An_Extreme_A_Star_Speed_Optimization_for_Static_Uniform_Cost_Grids.pdf
7. Harabor, D.; Grastien, A. "Improving Jump Point Search." *Proceedings of the 24th International Conference on Automated Planning and Scheduling (ICAPS-14)*, 2014. https://users.cecs.anu.edu.au/~dharabor/data/papers/harabor-grastien-icaps14.pdf
8. Nash, A.; Daniel, K.; Koenig, S.; Felner, A. "Theta\*: Any-Angle Path Planning on Grids." *Proceedings of the 22nd AAAI Conference on Artificial Intelligence (AAAI-07)*, pp. 1177–1183, 2007. http://idm-lab.org/bib/abstracts/papers/aaai07a.pdf
9. Nash, A.; Koenig, S.; Tovey, C. "Lazy Theta\*: Any-Angle Path Planning and Path Length Analysis in 3D." *Proceedings of the AAAI Conference on Artificial Intelligence*, 2010 (extended abstract in *Proceedings of the 3rd Annual Symposium on Combinatorial Search, SOCS-10*, pp. 153–154). https://ojs.aaai.org/index.php/SOCS/article/download/18152/17943/21668
10. Botea, A.; Müller, M.; Schaeffer, J. "Near Optimal Hierarchical Path-Finding." *Journal of Game Development*, 1(1):7–28, 2004. http://webdocs.cs.ualberta.ca/~mmueller/ps/2004/hpastar.pdf
11. Patel, A. (Red Blob Games). "Introduction to Graphs." https://www.redblobgames.com/pathfinding/grids/graphs.html
12. Mononen, M. et al. "recastnavigation" (Recast & Detour navigation-mesh toolset), README. GitHub. https://github.com/recastnavigation/recastnavigation/blob/main/README.md
13. Patel, A. (Red Blob Games). "Grid Pathfinding Optimizations." https://www.redblobgames.com/pathfinding/grids/algorithms.html
14. Epic Games. "Overview of How to Modify the Navigation Mesh in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-how-to-modify-the-navigation-mesh-in-unreal-engine
15. Epic Games. "Basic Navigation in Unreal Engine." https://dev.epicgames.com/documentation/en-us/unreal-engine/basic-navigation-in-unreal-engine
16. Epic Games. "World Partitioned Navigation Mesh." https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partitioned-navigation-mesh
17. Unity Technologies. "About NavMesh Obstacles." AI Navigation package manual. https://docs.unity3d.com/Packages/com.unity.ai.navigation@2.0/manual/AboutObstacles.html
18. van den Berg, J.; Guy, S. J.; Lin, M.; Manocha, D. "Reciprocal n-Body Collision Avoidance." In *Robotics Research*, Springer Tracts in Advanced Robotics, vol. 70, pp. 3–19. Springer, 2011. https://gamma.cs.unc.edu/ORCA/publications/ORCA.pdf
19. van den Berg, J.; Guy, S. J.; Snape, J.; Lin, M.; Manocha, D. "RVO2 Library — Using RVO2 Library." Documentation. https://gamma.cs.unc.edu/RVO2/documentation/2.0/using.html
20. Snape, J. et al. "RVO2: Optimal Reciprocal Collision Avoidance (C++)." GitHub repository. https://github.com/snape/RVO2
21. Unity Technologies. "AI Navigation." Package manual. https://docs.unity3d.com/6000.1/Documentation/Manual/com.unity.ai.navigation.html

**Note on source access:** Every academic paper and every engine-documentation page cited above was fetched and read directly (full text extracted from the original PDF where the source is a paper, or the live documentation page where the source is Unreal/Unity documentation) rather than summarized from a secondary description. The one exception is Hart, Nilsson, and Raphael's original 1968 A\* paper [1], which sits behind the IEEE Xplore paywall with no freely available full-text mirror found; it is cited by its DOI and full bibliographic record, and its content is corroborated throughout Section 1 via the Theta\* paper's own formal restatement of the A\* algorithm and its citation of Hart, Nilsson, and Raphael [8], and via Red Blob Games' and Amit Patel's treatments of A\* and heuristic admissibility, which themselves cite the same original paper [1][2][3].
