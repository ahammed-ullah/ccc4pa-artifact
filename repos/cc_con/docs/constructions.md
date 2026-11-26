# Construction Guide

This repository implements the six clique-cover constructions evaluated in
[*Concise Clique Covers for Parsimonious Graph Algorithms*](https://arxiv.org/abs/2604.28096).
The CLI names are `bcc`, `bpa`, `cpa`, `dpf`, `fpa`, and `lzf`.

## Common notation

Every construction receives the same canonical graph `G=(V,E)`: it is simple,
undirected, has vertex IDs `0,...,n-1`, has sorted duplicate-free adjacency
lists, and has no isolated vertices. Raw readers establish and validate these
properties; prepared CGF input is created by the same path, and construction
runs decode it without repeating full validation.
`cc_check graph` explicitly repeats the file and canonical-graph checks.

A clique cover `C=(C[0],...,C[p-1])` is a family of vertex subsets such that
each `C[ell]` induces a clique and every edge of `G` occurs in at least one
clique. The reports distinguish:

- `|C|`, reported as `num_cliques`, is the number of cliques;
- `sigma(C) = sum_ell |C[ell]|`, reported as `cover_size`, is the number of
  vertex-to-clique assignments.

A cover is composition-minimal when no two distinct cliques have a union that
is also a clique. The manuscript calls a cover concise when it is
composition-minimal and `sigma(C)=O(m)`. It also uses

```text
mu = binom(n,2) - m + 1.
```

Here `N(v)` is the open neighborhood of `v`, and `N[v]=N(v) union {v}` is its
closed neighborhood.

The paper numbers clique and color labels starting at one. The source uses
zero-based vectors, so paper label `l` is stored at vector position `l-1` when
the labels are compared literally. The construction pages use source-style
zero-based labels when naming concrete vectors and paper notation when quoting
a definition or pseudocode step.

## Local and phased admissibility

For an output clique `C[ell]`, define its admissible vertices by

```text
D[ell] = {v : C[ell] is a subset of N[v]}.
```

Equivalently, `A[v]` contains label `ell` exactly when `v` belongs to
`D[ell]`. Local-Augmentation (LA), defined in Definition 3.13, processes an
uncovered edge `{u,v}` by
augmenting one clique with label in `A[u] intersect A[v]`, when such a label
exists, and otherwise creating the edge clique `{u,v}`.

Phased admissibility stores `A` and `D` only for labels created in the current
phase. Here `sigma(D)=sum_ell |D[ell]|` counts the stored admissibility
relation. A phase stops creating labels after the strict threshold
`sigma(D) > tau*m`; the label causing the overflow remains active, and later
work in that phase may still augment active labels. Work that cannot be handled
is deferred to another pass, after which the phase-local relation is discarded.
The default is `tau=4`.

The paper permits any one label from the relevant intersection. BPA, FPA, and
CPA expose four deterministic selection rules. This changes which valid cover
is produced, but not the LA or phased-admissibility condition.

BPA, FPA, and CPA keep `L[v]`, the output-clique labels containing `v`, and
test whether an edge is covered by intersecting its endpoints' lists. FPA forms
its restricted first-fit sets one neighborhood at a time. CPA restricts one
CrossCover clique to the endpoints of its uncovered owned edges. The order of
work differs, but both use the same covered-edge test and the same
phased-admissibility relation.

## Implemented constructions

| CLI | Construction | Manuscript correspondence |
| --- | --- | --- |
| `lzf` | first-fit variant of Lovasz's construction | Definition 3.12, with the maximum-clique peeling replaced by the first-fit complement coloring described after that definition. It performs no augmentation or phasing. |
| `bpa` | Basic Phased Admissibility | Definition 3.13 together with the phased-admissibility rule following Definition 3.15. Appendix C.2 describes this direct edge-scanning construction used in the evaluation. |
| `fpa` | First-fit Phased Admissibility | FirstFit-PA and AugmentCover in Figures 2 and 3. Lemmas 3.16 and 3.18 give its cover and resource bounds. |
| `bcc` | Balanced Cross-Cover | CrossCover and BCC in Definitions 3.20 and 3.21. Lemma 3.22 gives its cover-size bounds. |
| `cpa` | CrossCover Phased Admissibility | CrossCover-PA in Figure 4. Lemma 3.24 gives its conciseness, time, and space bounds. |
| `dpf` | Degeneracy Pivoted First-Fit | DP-FirstFit in Figure 15. Lemma A.4 proves assignment-minimality and the resource bounds. |

LZF and BCC are the unaugmented covers from which FPA and CPA are developed.
They satisfy the stated size bounds but are not guaranteed to be
composition-minimal. FPA and CPA apply phased admissibility to obtain concise
covers. BPA is the direct LA-plus-phasing construction used in the evaluation.

DPF takes a degeneracy order and visits pivots from the end of that order to the
beginning. For a pivot `v`, it first-fit colors the complement induced by the
still-uncovered earlier neighbors of `v`, and emits each resulting class
together with `v`. It never augments an existing output clique and therefore
needs no admissibility relation.

The per-construction documents give the concrete vectors, loop order,
optimizations, and reported statistics:

- [LZF](lzf.md)
- [BPA](bpa.md)
- [FPA](fpa.md)
- [BCC](bcc.md)
- [CPA](cpa.md)
- [DPF](dpf.md)

## Timing and verification

Each construction builds all of its own algorithmic state inside its timed
function. Shared sorted-vector operations and complement-coloring procedures
keep no graph data between calls. Graph preparation, optional assignment-minimal
postprocessing, verification, report writing, and CCF encoding are outside
`construction_time_seconds`. The separate
[assignment-minimal postprocessing guide](assignment-minimal.md) gives its
greedy procedure, implementation invariants, correctness argument, and
`O(sum_ell |C[ell]|^2)` runtime bound.

When a CCF is requested, clique-cover verification is mandatory. For
statistics-only runs, verification follows `[verification].enabled` in the run
configuration. See [Reporting](reporting.md) and
[Output formats](output-formats.md) for the exact fields and binary layouts.
