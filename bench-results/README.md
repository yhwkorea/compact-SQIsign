# SQIsign benchmark — environment & methodology

Wall-clock and Mcyc measurements for **KLKL25 baseline** (`SQIsign-Fixed-Precision`,
patched) and **Ours** (`compact-SQIsign`, this fork, paper-bound 28/43/56 limbs)
at NIST levels I/III/V.

## Machine

| | |
| --- | --- |
| CPU | **Intel Core i7-8700K @ 3.70 GHz** (Coffee Lake, 6 cores / 12 threads) |
| Pinning | `taskset -c 0` — single P-core, all runs share core 0 to avoid cross-core variance |
| Memory | (system default; not RSS-constrained) |
| OS | WSL2 Ubuntu, kernel `6.6.114.1-microsoft-standard-WSL2` |
| Compiler | GCC 11.4.0 (Ubuntu `11.4.0-1ubuntu1~22.04.3`) |
| Build | `cmake -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release` |
| Stack | `ulimit -s unlimited` at run time |

## Precision

| Variant | lvl1 | lvl3 | lvl5 |
| --- | ---: | ---: | ---: |
| KLKL25 (upstream HNF) | 110 limbs | 168 limbs | 222 limbs |
| **Ours** (MLLL/ML2, paper §5.2) | **28 limbs** | **43 limbs** | **56 limbs** |

## Per-iter timeout (seconds)

| Level | Cap |
| --- | ---: |
| lvl1 | 400 |
| lvl3 | 1800 |
| lvl5 | 3600 |

A handful of KLKL25 sign attempts loop infinitely under NDEBUG (paths beyond
the zero-content one already handled by our patch #5). The cap kills those
quickly so the loop can retry with a fresh seed.

## Measurement methodology

- Each *level × phase* runs `benchmark_<level> --iterations=1 --only=<phase>`
  per attempt, with a fresh random seed. Phases are **measured separately** so
  that a sign timeout cannot wipe out the KeyGen / Verify samples from the same
  iteration.
- `--only=keypair` times only KeyGen.
- `--only=sign` does KeyGen as **untimed setup**, then times Sign.
- `--only=verify` does KeyGen + Sign as **untimed setup**, then times Verify.
- We accumulate until **20 valid** (exit-0) attempts per phase, with a hard cap
  of 200 attempts per phase as a safety net.
- Both Mcyc (`RDTSC` via the existing `bench.h` cycle counter) and wall-clock
  seconds (`clock_gettime(CLOCK_MONOTONIC)`, added as bench.h patch #6) are
  recorded per attempt.

## Patches applied

To both the KLKL25 baseline and Ours (where applicable):

1. `intbig.h` — token-paste dispatch for `IBZ_LIMBS_<variant>` (upstream `#if`
   collapsed all variants to lvl1; this caused lvl3/lvl5 binaries to be silently
   under-sized).
2. `sign.c` — silence 8× per-step `printf("--<step> done--\n")` (replaced with
   `(void)0;`) — these dominated stdout during sign-retry storms and drifted
   the measured cycle counts ~10%.
3. `sign.c` — add `sign_retries` counter + one `fprintf(stderr,
   "[SIGN_RETRIES] %d\n", ...)` at the end of `protocols_sign`, so the bench
   driver can tell a single slow sign from a sign that needed many retries.
4. `intbig.c` `ibz_probab_prime` — trial-division precheck against the first 50
   odd primes before Miller–Rabin. Algorithm-preserving; skips the
   `O(reps × limbs²)` MR cost for the ~88 % of `quat_represent_integer`
   candidates that are composite.
5. `sign.c` `compute_backtracking_signature` — return-type `void → int`. When
   `quat_alg_make_primitive` returns a zero `content` (silently accepted under
   NDEBUG), the function used to set `resp_quat->denom = 0`, collapsing every
   downstream `quat_alg_norm`, and trapping the sign loop in
   `quat_represent_integer`'s `is_even` fast-exit forever. New behavior: return
   0, let the outer sign loop retry from `commit`.
6. `bench.h` — add `clock_gettime(CLOCK_MONOTONIC)` pair inside
   `BENCH_CODE_1/2`, emit a second line per phase with median/min/max in
   seconds alongside the existing Mcyc line.

Plus, for the benchmark driver only:

- `apps/benchmark.c` — `--only=<phase>` flag (keypair / sign / verify / all).

## Layout of the on-disk results

```
bench-results/
  README.md                          this file
  results_compare/
    META.log                         meta-driver block trace
    STATS.md                         mean/median/min/max per impl/level/phase
    ours/lvl{1,3,5}/
      progress.log                   per-block phase trace
      lvl{1,3,5}/SUMMARY_<phase>.tsv per-attempt parsed table
    ours/lvl1_110limb_OLD/           preserved 110-limb baseline run for record
    klkl25/lvl{1,3,5}/
      (same layout)
```

## How to read the numbers

- `n` = number of *valid* (exit-0) attempts for that level/phase.
- `mean` and `median` are the arithmetic mean and median of wall-clock seconds
  over those `n` attempts.
- `min` / `max` give a sense of variance — KLKL25 sign in particular has high
  spread because some sign attempts hit slow retry chains.

## Run order

This run interleaves Ours and KLKL25 in the order requested by the paper author:

1. **Ours lvl1**
2. KLKL25 lvl1 (KeyGen taken from the prior all-in-one run; Sign + Verify
   re-measured here)
3. Ours lvl3
4. Ours lvl5
5. KLKL25 lvl3
6. KLKL25 lvl5

Each block is auto-pushed to `main` as it completes.
