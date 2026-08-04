# compact-SQIsign

A fixed-precision C implementation of SQIsign with the algorithms in the paper
"Compact Quaternion Algorithms for SQIsign".  The paper's main-body
worst-case magnitude bounds are `{1830, 2752, 3611}` bits at NIST levels
I / III / V.  Since `ibz_t` is a signed two's-complement type, one sign bit is
reserved before rounding to complete 64-bit limbs, giving `{29, 44, 57}` limbs
instead of the upstream `{110, 168, 222}` limbs
(https://github.com/munsanwon2/SQIsign-Fixed-Precision).

The `IBZ_LIMBS_lvl{1,3,5}` defaults in
`src/quaternion/ref/generic/include/intbig.h` are already set to
`{29, 44, 57}`.

## Requirements

- CMake (version 3.13 or later)
- C11-compatible compiler
- GMP is **not** required (the mini-gmp shipped under `src/mini-gmp/` is used
  when needed, but the default build uses fixed-precision arithmetic).
- **Low Memory** is now allowed!

### Pre-computation

The constant values in the 'src/precomp' directory were generated using the pre-computation scripts in the scripts/precomp directory.
It is not necessary to execute these scripts to compile the project.

## Build

```bash
mkdir -p build
cmake -B build -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

`SQISIGN_BUILD_TYPE` accepts:

| value | meaning |
| --- | --- |
| `ref` | plain reference build (portable C) |
| `opt` | identical to `ref` in this fork |
| `broadwell` | AVX2/BMI2 path for the finite-field arithmetic; default for the bench results below |

`CMAKE_BUILD_TYPE=Release` strips assertions (`-DNDEBUG`) and is required for
representative timing.

### Build options

- `-DENABLE_SIGN=ON` (default): build with sign/verify. `OFF` builds verify-only.
- `-DENABLE_KAT_TESTS=OFF` (default): keep stored-vector tests out of the
  normal correctness suite. `ON` enables Compact signature-verification
  fixtures; mismatching byte-for-byte replay is not registered.
- `-DENABLE_ML2_PROFILE=OFF` (default): set this to `ON` to collect per-dimension first-failure,
  permutation-recovery, precision-rejection, and exhausted-retry counters in
  the signature test binaries.
- `-DENABLE_INTBIG_OVERFLOW_CHECK=OFF` (default): instrument signed fixed-width
  addition, subtraction, multiplication, negation, and left shift, and abort a
  test at the first capacity violation.
- `-DGMP_LIBRARY={SYSTEM,BUILD,MINI}`: select libgmp source. Not relevant for
  the default fixed-precision build.
- `-DCMAKE_BUILD_TYPE={Release,Debug,ASAN,MSAN,LSAN,UBSAN}`: optimization and
  sanitizer choice.

## Test

In the build directory:

```bash
ctest --output-on-failure
```

The test harness covers:

- `sqisign_<level>_CORRECTNESS` — fixed-seed KeyGen/Sign/Verify/Open property
  tests, including in-place open, API length checks, and signature/message
  tamper rejection.
- Quaternion tests which compare the Compact ML2 output with an independent
  exact-HNF lattice oracle, including large and dependent generator sets and
  fail-closed reducer faults.  Production ML2 first tries the original
  generator order, then at most three deterministic span-preserving
  permutations; it never regenerates the surrounding protocol state.
- Sub-library unit tests for `mp`, `gf`, `ec`, `quaternion`, `hd`, `id2iso`.

Stored vectors are byte-for-byte regression fixtures, not an independent
correctness oracle.  They are disabled by default.  To run the Compact
fixtures explicitly:

```bash
cmake -B build -DENABLE_KAT_TESTS=ON
cmake --build build
ctest --test-dir build -L regression --output-on-failure
```

The legacy NIST-v2 files remain archived but are intentionally not registered
with CTest because Compact key generation consumes a different random
transcript.  See [`KAT/README.md`](KAT/README.md) for provenance and
regeneration.

The scheme correctness tests have a 7200-second per-test timeout because level
5 signing is intentionally slow in the reference build.

An opt-in deterministic synthetic measurement is also available (it is not a
CTest test):

```bash
build/src/quaternion/ref/generic/test/sqisign_test_quaternion_lvl1 \
  --ml2-stress=10000
```

This reports first-attempt and post-retry rates separately for d=4/8/16.
Synthetic rates are bug-finding measurements and must not be interpreted as
the failure probability of protocol-generated lattices.  For protocol input
counts, configure with `-DENABLE_ML2_PROFILE=ON` and run the signature tests.

## Benchmarking & reproducing the published numbers

Pre-built results from this fork vs. the KLKL25 baseline live under
[`bench-results/`](bench-results/). The methodology and machine spec used to
produce them are documented in
[`bench-results/README.md`](bench-results/README.md). What follows is the
procedure to reproduce those numbers from a clean clone.

### Machine used for the published numbers

| | |
| --- | --- |
| CPU | Intel Core i7-8700K @ 3.70 GHz (6 cores / 12 threads, Coffee Lake) |
| Pinning | `taskset -c 0..3`, four parallel workers (`PARALLEL=4`) |
| OS | WSL2 Ubuntu, kernel `6.6.114.1-microsoft-standard-WSL2` |
| Compiler | GCC 11.4.0 |
| Build | `cmake -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release` |

### Step 1. Build both implementations

`Ours` is this repository, already at the main-body worst-case
`{29, 44, 57}` signed limbs:

```bash
# in this repo
cmake -B build -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

For apples-to-apples Verify measurements, `benchmark_<level>` defaults to
`--verify-mode=original`: it reproduces the valid-input basis-recovery
workload of the original SQIsign Release benchmark.  This affects only the
benchmark executable.  The library APIs, tests, examples, and fuzz targets
continue to use the hardened verifier.  Use `--verify-mode=hardened` when the
production verifier, including its untrusted-input basis checks, is the
quantity to measure.

`KLKL25` is the reference implementation accompanying the prior work cited as
KLKL25 (URL withheld for double-blind review), defaulting to
`{110, 168, 222}` limbs. It needs five patches before it can be
benchmarked cleanly; the patches and their justification are documented at the
top of [`bench-results/README.md`](bench-results/README.md) (sections
"Patches applied"). After patching:

```bash
# in the SQIsign-Fixed-Precision checkout
cmake -B build -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Step 2. Drive the comparison

The driver scripts shipped with the bench results are in
[`bench-results/scripts/`](bench-results/scripts/):

- `run_compare.sh` — meta-driver. Iterates the six `(impl × level)` blocks in
  the order `Ours lvl1 → KLKL25 lvl1 → Ours lvl3 → Ours lvl5 → KLKL25 lvl3 →
  KLKL25 lvl5`.
- `run_until20_parallel.sh` — per-block driver. Runs `PARALLEL=4` workers
  pinned to cores 0..3, with each worker invoking
  `benchmark_<level> --iterations=1 --only=<phase>` and accumulating until 20
  exit-0 attempts per phase. Coordinates via a `flock`-protected counter dir.
- `compute_stats.sh` — recomputes `STATS.md` (mean / median / min / max per
  `impl × level × phase`) and the side-by-side comparison table.
- `auto_push_watcher.sh`, `supervisor.sh` — automation used during the run
  (auto-commit each completed block, restart drivers on death). Not required
  for a one-shot reproduction.

Invoke from the host machine:

```bash
export KLKL25_BUILD=/path/to/SQIsign-Fixed-Precision/build
export OURS_BUILD=/path/to/compact-SQIsign/build
export OUT_BASE=/path/to/results_compare
export DRIVER=/path/to/bench-results/scripts/run_until20_parallel.sh

bash /path/to/bench-results/scripts/run_compare.sh
```

Each block writes:

```
$OUT_BASE/<impl>/<level>/
  progress.log                                per-block trace
  driver.{stdout,stderr}                      outer driver raw output
  <level>/SUMMARY_{keypair,sign,verify}.tsv   per-attempt parsed table
  <level>/<phase>_iter<N>.{log,err}           raw bench output per attempt
```

A run of all six `(impl × level)` blocks takes about 13 hours on the machine
above. The split is approximately:

| Block | Wall time |
| --- | ---: |
| Ours lvl1 | ~12 min |
| KLKL25 lvl1 | ~1 h |
| Ours lvl3 | ~1 h |
| Ours lvl5 | ~3 h |
| KLKL25 lvl3 | ~3 h |
| KLKL25 lvl5 | (best-effort; KeyGen at 222 limbs is the bottleneck) |

### Step 3. Parse

After the meta-driver finishes (or any time during the run, to see partial
results), regenerate `STATS.md` from the on-disk SUMMARY tables:

```bash
bash bench-results/scripts/compute_stats.sh $OUT_BASE bench-results/results_compare
```

The output is a Markdown table with mean, median, min, max of wall-clock
seconds per `impl × level × phase`, plus a side-by-side comparison and the
median-speedup ratio.

### Per-iter timeouts

Picked from the cost-per-valid model (see `bench-results/README.md`,
section "Per-iter timeout"). Defaults in
`run_until20_parallel.sh`:

| Level | Cap |
| ---: | ---: |
| lvl1 | 350 s |
| lvl3 | 1300 s |
| lvl5 | 2600 s |

A few KLKL25 sign attempts loop indefinitely under NDEBUG (paths beyond the
zero-content one already handled by patch #5); the cap kills those quickly
so the loop can retry with a fresh seed.

## Examples

Example code using the NIST API is in `apps/example_nistapi.c`.

## Project structure

- `common` — hash, seed expansion, PRNG, memory handling.
- `mp` — saturated-representation multiprecision arithmetic.
- `gf` — GF(p^2) and GF(p) arithmetic.
- `ec` — elliptic curves, isogenies, pairings.
- `precomp` — constants and precomputed values.
- `quaternion` — quaternion orders and ideals; this is where the MLLL / ML2
  replacement of HNF lives.
- `hd` — (2,2)-isogenies in the theta model.
- `id2iso` — Ideal ↔ Isogeny.
- `verification` — verification protocol.
- `signature` — key generation and signature protocols.

## Cortex-M4

Verification on ARM Cortex-M4 is supported via the
[pqm4](https://github.com/mupq/pqm4) project. Use
`scripts/gen_pqm4_sources.sh` from the repository root to generate a
pqm4-compatible folder layout in `src/pqm4/sqisign_lvl{1,3,5}`.

## Acknowledgements

The reference finite-field arithmetic (`src/gf/ref`) was generated with
[`modarith`](https://github.com/mcarrickscott/modarith) by Michael Scott.

Our implementation is based on the original SQIsign implementation (https://github.com/SQIsign/the-sqisign) and the original SQIsign with fixed-precision implementation (https://github.com/munsanwon2/SQIsign-Fixed-Precision).

## License

Apache-2.0. See [[LICENSE](LICENSE)](https://github.com/SQISign/the-sqisign/blob/main/LICENSE) and [[NOTICE](NOTICE)](https://github.com/SQISign/the-sqisign/blob/main/NOTICE).

Third-party code retains its original license:

- `src/common/aes_c.c` — MIT, © 2016 Thomas Pornin pornin@bolet.org
- `src/common/fips202.c` — CC0, © 2023 the PQClean team
- `src/common/randombytes_system.c` — MIT, © 2017 Daan Sprenkels hello@dsprenkels.com
- `src/common/broadwell/{aes_ni.c, vaes256_key_expansion.S}` — Apache-2.0,
  © 2019 Amazon.com, Inc.
- `src/common/broadwell/ctr_drbg.c` — ISC, © 2017 Google Inc.
- `src/mini-gmp/mini-gmp.{c,h}` — LGPLv3, © 1991–2022 Free Software Foundation, Inc.
- `src/quaternion/ref/generic/dpe.h` — LGPLv3, © 2004–2024 Patrick Pélissier,
  Paul Zimmermann, LORIA/INRIA
- `apps/PQCgenKAT_sign.c`, `apps/PQCgenKAT_sign_pqm4.c`,
  `src/common/ref/randombytes_ctrdrbg.c`, `test/test_kat.c` — by NIST (Public Domain)
