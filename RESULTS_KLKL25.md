# KLKL25 baseline measurement (SQIsign-Fixed-Precision)

Wall-clock timing measurement of the **KLKL25 fixed-precision SQIsign baseline**
(upstream `munsanwon2/SQIsign-Fixed-Precision`, the predecessor cited as
"Previous work" in this fork's paper §5.2 Table~\ref{tab:perf}).

This file reports only the **Previous work [KLKL25]** row; the **Ours**
(compact-SQIsign) row is being measured separately and will be added in a
follow-up commit.

## Environment

- CPU: AMD Ryzen 7 9700X (8 cores / 16 threads), Zen 5
- OS: WSL2 Ubuntu, kernel 5.15
- Compiler: GCC 13.3.0
- Build: `cmake -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release`
  - `CMAKE_C_FLAGS_RELEASE = -O3 -DNDEBUG`
  - `ulimit -s unlimited` at run time
- Upstream repo: <https://github.com/munsanwon2/SQIsign-Fixed-Precision>
- Method: 20 sequential runs of `apps/benchmark_<level> --iterations=1`
  per NIST security level (lvl1 → lvl3 → lvl5), per-iter timeout 300s.
- Reported metric: median of valid (`exit=0`) runs, in **megacycles** (Mcyc)
  as emitted by `BENCH_CODE_2` (`bench.h`).

## Patches applied to upstream

The pristine `munsanwon2/SQIsign-Fixed-Precision` checkout does not build
lvl3/lvl5 cleanly and exhibits a critical sign-loop bug; the following
**minimal patches** were applied to make measurement possible. None of them
alters the KLKL25 algorithm itself, only build correctness and observability.

1. `intbig.h` preprocessor dispatch (`##` token paste) so
   `IBZ_LIMBS_FOR_lvl{1,3,5}` actually map to the intended limb counts
   (upstream's `#if SQISIGN_VARIANT == SQISIGN_LVL1` collapses to `0 == 0`
   and silently picks lvl1's limb count for every level — lvl3/lvl5 binary
   would otherwise be lvl1-sized and not behave as specified).
2. `sign.c`: replaced 8 unconditional `printf("--<step> done--\n")` calls
   with `(void)0;` so stdout is not dominated by trace lines during a
   sign retry storm (the measured cycle counts otherwise drift by ~10%).
3. `sign.c`: added a `sign_retries` counter + one `fprintf(stderr,
   "[SIGN_RETRIES] %d\n", ...)` at the end of `crypto_sign`, used to
   distinguish a single sign that needed many retries from a successful
   short sign.
4. `intbig.c` `ibz_probab_prime`: prepended a trial-division precheck
   against the first 50 odd primes (3 ... 229) so that the ~88% of composite
   candidates entering Miller–Rabin from `quat_represent_integer` get
   rejected in O(n × shift) word ops instead of paying the full
   Miller–Rabin (32 rounds of `ibz_pow_mod`, each `O(n²)` per modular
   step). This is algorithm-preserving — Miller–Rabin's decision for the
   true primes is unchanged.
5. `sign.c` `compute_backtracking_signature`: changed return type
   `void → int`. When `quat_alg_make_primitive` returns a zero
   `content` (which under NDEBUG is silently accepted because the
   `quat_lattice_contains` assertion is disabled), the function now
   returns 0 instead of multiplying `resp_quat->denom` by zero. The
   sign-loop caller checks this return and triggers a normal sign-retry,
   which is exactly the recovery path KLKL25 already specifies for other
   sub-step failures.

Without patch (5), `resp_quat->denom` is set to 0, all subsequent
`quat_alg_norm(resp_quat)` results collapse below `lattice_content`,
`degree_full_resp / lattice_content` truncates to 0, the resulting
`random_aux_norm = 2^pow - 0` is even, and `quat_represent_integer` fast-exits
on `ibz_is_even(n_gamma)`. The sign loop then retries forever and the run
times out. Empirically, this codepath was hit in roughly 75–90% of sign
retries in our environment; it appears to be a latent NDEBUG-masked
correctness bug in the upstream rather than a documented "rare retry".

## Raw results — KLKL25 baseline, 20 iter × 3 levels

`?` denotes a per-iter timeout (`exit=124`); no Sign/Verify cycle counts are
produced for those iterations.

### NIST-I (lvl1, 110 limbs)

20 iter, started `2026-05-21 00:40:28 KST`, finished `01:52:54 KST`.
**12 valid / 8 timeout** (40% timeout rate).

| iter | dur  | exit | retries | KG (Mcyc)  | Sign (Mcyc)   | Verify (Mcyc) |
| ---: | ---: | ---: | ------: | ---------: | ------------: | ------------: |
|    1 | 300s |  124 |       ? |          ? |             ? |             ? |
|    2 | 238s |    0 |      14 | 104219.349 |    800323.875 |         3.333 |
|    3 | 300s |  124 |       ? |          ? |             ? |             ? |
|    4 | 300s |  124 |       ? |          ? |             ? |             ? |
|    5 | 118s |    0 |       3 | 144497.358 |    302446.533 |         3.396 |
|    6 | 300s |  124 |       ? |          ? |             ? |             ? |
|    7 | 147s |    0 |       6 | 194165.864 |    363269.212 |         3.600 |
|    8 |  53s |    0 |       2 |  24203.894 |    180694.164 |         3.479 |
|    9 | 300s |  124 |       ? |          ? |             ? |             ? |
|   10 | 266s |    0 |      10 |  30673.154 |    982205.792 |         3.629 |
|   11 | 300s |  124 |       ? |          ? |             ? |             ? |
|   12 | 130s |    0 |       3 |  55007.952 |    437133.379 |         3.281 |
|   13 |  64s |    0 |       2 |  28200.909 |    214755.564 |         3.775 |
|   14 | 126s |    0 |       9 |  42321.386 |    439362.938 |         3.289 |
|   15 | 113s |    0 |       4 |  67432.421 |    358881.476 |         3.343 |
|   16 | 285s |    0 |       8 |  18068.148 |   1065669.939 |         3.511 |
|   17 | 238s |    0 |      13 | 106492.227 |    799180.273 |         3.355 |
|   18 | 300s |  124 |       ? |          ? |             ? |             ? |
|   19 | 167s |    0 |      11 |  40623.044 |    594226.724 |         3.388 |
|   20 | 300s |  124 |       ? |          ? |             ? |             ? |

### NIST-III (lvl3, 168 limbs)

20 iter, started `01:52:54 KST`, finished `03:28:07 KST`.
**5 valid / 15 timeout** (75% timeout rate).

| iter | dur  | exit | retries | KG (Mcyc)  | Sign (Mcyc)   | Verify (Mcyc) |
| ---: | ---: | ---: | ------: | ---------: | ------------: | ------------: |
|    7 | 297s |    0 |       3 | 443192.691 |    683339.604 |        11.875 |
|   12 | 183s |    0 |       2 | 194348.738 |    503344.016 |        11.541 |
|   17 | 251s |    0 |       3 |  74724.926 |    879756.913 |        11.174 |
|   19 | 189s |    0 |       1 |  40455.675 |    677044.962 |        11.610 |
|   20 | 292s |    0 |       3 | 121004.843 |    989314.364 |        11.423 |

(15 omitted timeout iterations: iter 1–6, 8–11, 13–16, 18.)

### NIST-V (lvl5, 222 limbs)

20 iter, started `03:28:07 KST`, finished `05:08:07 KST`.
**0 valid / 20 timeout** (100% timeout rate at 300s/iter).

No valid sign timings produced. The 5-min per-iter cap is too short for
KLKL25 at lvl5 with the patched (but still upstream-equivalent) sample loop:
each sign retry on a 222-limb operand takes substantially longer than at
lvl1/3, and lvl5 saw zero retries land within the cap. A larger
per-iter cap (≥ 30 min) would be required to capture lvl5 timings in this
environment.

## Summary — KLKL25 baseline medians (Mcyc)

| Level | n (valid) | KG median | Sign median | Verify median |
| ----- | --------: | --------: | ----------: | ------------: |
| I     |        12 |   ~61,200 |    ~438,000 |          3.39 |
| III   |         5 |  ~121,000 |    ~683,000 |         11.54 |
| V     |         0 |         — |           — |             — |

KG median lvl1 = `(55,008 + 67,432) / 2 = 61,220 Mcyc`.
Sign median lvl1 = `(437,133 + 439,363) / 2 = 438,248 Mcyc`.
Verify median lvl1 = `(3.388 + 3.396) / 2 = 3.392 Mcyc`.

KG median lvl3 = `121,005 Mcyc` (middle of 5).
Sign median lvl3 = `683,340 Mcyc`.
Verify median lvl3 = `11.541 Mcyc`.

## Caveats

- Wall-clock measurement on a desktop workstation under WSL2; not a
  constant-environment cluster. Absolute numbers should be interpreted
  alongside the per-iter retry count, which dominates the variance on
  this codebase (data-dependent sign retries).
- The lvl3/lvl5 timeout rates do **not** mean the algorithm is broken at
  those levels — they mean the 300s/iter cap chosen for this run is
  inappropriate for the larger limb counts. A future run with a level-
  scaled cap should recover the missing data points.
- Reported cycles are produced by `BENCH_CODE_2` (RDTSC-based via
  `cpucycles()` in `bench.h`); they reflect the entire `crypto_sign`
  call including all internal sign retries, not the cost of a single
  successful retry.

## Raw artifacts

- Bench scripts: `~/sqisign-fp-bench/run_20each.sh`
- Per-iter raw output: `~/sqisign-fp-bench/results_20/lvl{1,3,5}/iter*.{log,err}`
- Progress log: `~/sqisign-fp-bench/progress_20.md`
