# SQIsign

This library is a C implementation of SQIsign.

## Requirements

- CMake (version 3.13 or later)
- C11-compatible compiler
- GMP is NOT required anymore!
- Low memory is allowed now!

### Pre-computation

The constant values in the `src/precomp` directory were generated using the
pre-computation scripts in the `scripts/precomp` directory. It is not necessary
to execute these scripts to compile the project. The scripts have the following
requirements:
- [two-isogenies](https://github.com/ThetaIsogenies/two-isogenies)
  (`Theta-SageMath` version).
- [deuring-2D](https://github.com/Jonathke/deuring-2D)

## Build

For a generic build
```
$ mkdir -p build
$ cd build
$ cmake -DSQISIGN_BUILD_TYPE=ref ..
$ make
$ ulimit -s unlimited
$ make test
```

An optimized executable with debug code and assertions disabled can be built
replacing the `cmake` command above by
```
cmake -DSQISIGN_BUILD_TYPE=<ref/broadwell> -DCMAKE_BUILD_TYPE=Release ..
```

## Build options

CMake build options can be specified with `-D<BUILD_OPTION>=<VALUE>`.

### SQISIGN_BUILD_TYPE

Specifies the build type for which SQIsign is built. The currently supported values are:
- `ref`: builds the plain reference implementation.
- `opt`: builds the optimized implementation which is the same as the reference
  implementation.
- `broadwell`: builds an additional optimized implementation targeting the Intel
  Broadwell architecture (and later). The optimizations are applied to the
  finite field arithmetic.

### GMP_LIBRARY

If set to `SYSTEM` (by default), the gmp library on the system is dynamically linked.

If set to `BUILD`, a custom gmp library is linked, which is built as part of the overall build process.
In this case, the following further options are available:
- `ENABLE_GMP_STATIC`: Does static linking against gmp. The default is `OFF`.
- `GMP_BUILD_CONFIG_ARGS`: Provides additional config arguments for the gmp build (for example `--disable-assembly`). By default, no config arguments are provided.

If set to `MINI`, the mini-gmp library is used, whose sources are included in the repository, in the folder `src/mini-gmp`. In this case, no copies of the full gmp library (system or custom-built) are required.

### ENABLE_SIGN

If set to `ON` (default), SQIsign is built with signature and verification functionality.
If set to `OFF`, SQIsign is built with verification functionality only.
In the latter case, SQIsign is built with smaller memory.

### CMAKE_BUILD_TYPE

Can be used to specify special build types. The options are:

- `Release`: Builds with optimizations enabled and assertions disabled.
- `Debug`: Builds with debug symbols.
- `ASAN`: Builds with AddressSanitizer memory error detector.
- `MSAN`: Builds with MemorySanitizer detector for uninitialized reads.
- `LSAN`: Builds with LeakSanitizer for run-time memory leak detection.
- `UBSAN`: Builds with UndefinedBehaviorSanitizer for undefined behavior detection.

The default build type uses the flags `-O3 -Wstrict-prototypes -Wno-error=strict-prototypes -fvisibility=hidden -Wno-error=implicit-function-declaration -Wno-error=attributes`. (Notice that assertions remain enabled in this configuration, which harms performance.)

## Test

In the build directory, run `make test` or `ctest`.

The test harness consists of the following units:

- KAT test: `SQIsign_<level>_KAT`- tests against the KAT files in the `KAT`
  directory.
- Self-tests: `SQIsign_<level>_SELFTEST` - runs random self-tests
  (key generation, signature and verification).
- Sub-library specific unit-tests.

Note that, `ctest` has a default timeout of 1500s, which is applied to all tests
except the KAT tests. To override the default timeout, run
`ctest --timeout <seconds>`.

## Paper-bound verification (this fork)

This fork resolves the quaternion-side bound issues so that
`IBZ_LIMBS_lvl1 = 28` matches paper §5.2's "1774 bit / 28 limbs" claim. See
[`SIGN_FUNCTION_PLAN.md`](SIGN_FUNCTION_PLAN.md), [`BUGS_AND_DECISIONS.md`](BUGS_AND_DECISIONS.md),
and [`PAPER_BOUND_PLAN.md`](PAPER_BOUND_PLAN.md) for the gap-by-gap history.

### Default (110-limb baseline)

```bash
mkdir -p build && cd build
cmake -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) sqisign_test_signature_lvl1
ulimit -s unlimited
./src/signature/ref/lvl1/test/sqisign_test_signature_lvl1 --iterations=1 --seed=1
```
Should print `All tests passed!` and finish in ~2 min on a recent x86.

### 28-limb paper-claim verification

Two changes from default:
1. resize the lvl1 quaternion precomp data from 110→28 limbs (helper script `_PR8_handoff/_precomp_resize.py` ships in-tree),
2. flip `IBZ_LIMBS_lvl1` in `src/quaternion/ref/generic/include/intbig.h` from `110` to `28`.

```bash
# from repo root
cp src/precomp/ref/lvl1/quaternion_data.c src/precomp/ref/lvl1/quaternion_data.c.bak110
python3 _PR8_handoff/_precomp_resize.py \
  src/precomp/ref/lvl1/quaternion_data.c.bak110 \
  src/precomp/ref/lvl1/quaternion_data.c 110 28
sed -i 's|#define IBZ_LIMBS_lvl1 110.*|#define IBZ_LIMBS_lvl1 28|' \
  src/quaternion/ref/generic/include/intbig.h

# rebuild + run sign
( cd build && make -j$(nproc) sqisign_test_signature_lvl1 )
ulimit -s unlimited
./build/src/signature/ref/lvl1/test/sqisign_test_signature_lvl1 --iterations=1 --seed=1
```
On the 2026-05-19 commits this passes in ~33–69 s for lvl1 with seeds 1..6.
To revert, restore the `.bak110` precomp and the `110` line in `intbig.h`.

**Seed coverage at 28-limb (lvl1)** as of commit `0195e54` (conditional ML2(d=16) path):

| seed | 110-limb | 28-limb | notes |
|------|----------|---------|-------|
| 1    | PASS     | PASS    |       |
| 2    | PASS     | PASS    | needs `f4ef487` DPE bound fix (the `bound_parallelogram_dpe` path); was a 28-limb regression before that |
| 3    | (was HANG, recheck) | **PASS** | 28-limb fixed by `0195e54`; 110-limb hang at LMUL `post_LLL` in `quat_lideal_lideal_mul_reduced` was a separate pre-existing path — recheck at this HEAD |
| 4    | PASS     | **PASS** | 28-limb fixed by `0195e54` (HNF mod core 2×hnfmod transient overflow bypassed via ML2(d=16) on `bar(I)·J` generators) |
| 5    | **HANG (new)** | PASS | 110-limb [INV4x4]-loop hang first observed at `0195e54` HEAD; unrelated to 28-limb work — follow-up |
| 6    | PASS     | **PASS** | same pattern as seed=4, same fix |

In short: **all six seeds 1..6 pass at 28-limb** as of `0195e54`, validating
the paper §5.2 "1774 bit / 28 limbs" claim for lvl1. The remaining open items
are on the 110-limb side: seed=5 has a newly-observed `[INV4x4]`-loop hang at
this HEAD, and seed=3's pre-existing `[LMUL] post_LLL` hang should be rechecked.

### Reading the bit-size traces

Each hot quaternion path logs one bit-size line per call. Run the binary with
stderr captured and grep:

```bash
./build/src/signature/ref/lvl1/test/sqisign_test_signature_lvl1 --iterations=1 --seed=1 2> /tmp/sig.stderr
grep -E '\[ML2.*peak_bit|\[ALGNORM|\[IMLLL\] L14 done|\[LATTICE-ADD|\[LMUL\] post_mul' /tmp/sig.stderr \
  | sort -t= -k2 -n | tail -10
```
At lvl1 seed=1 the max bit observed is **1520** (ML2 #0 during keygen),
under paper sign bound `2^21·p^7 ≈ 1757` and the 28-limb cap `1792`.
See `SIGN_FUNCTION_PLAN.md` for the per-function table mapping R-tags
(R3/R4/R5/R7/R8) to paper Sign algorithm lines.

## Known Answer Tests (KAT)

KAT are available in the `KAT` directory. They can be generated by running the
apps built in the `apps` directory:
```
apps/PQCgenKAT_sign_<level>
```

A successful execution will generate the `.req` and `.rsp` files.

A full KAT test is done as part of the test harness (see the [Test](#test)
section).

## Benchmarks

A benchmarking suite is built and can be executed with the following command:
```
apps/benchmark_<level> [--iterations=<iterations>]
```
where `<level>` specifies the SQIsign parameter set and `<iterations>` is the
number of iterations used for benchmarking; if the `--iterations` option is
omitted, a default of 10 iterations is used.

The benchmarks profile the key generation, signature and verification functions. The results are reported in CPU cycles if available on the host platform, and timing in nanoseconds otherwise.

## Examples

Example code that demonstrates how to use SQIsign with the NIST API is available
in `apps/example_nistapi.c`.

## Project Structure

The source code consists of a number of sub-libraries used to implement the
final SQIsign library:
- `common`: common code for hash function, seed expansion, PRNG, memory handling.
- `mp`: code for saturated-representation multiprecision arithmetic.
- `gf`: GF(p^2) and GF(p) arithmetic.
- `ec`: elliptic curves, isogenies and pairings. Everything that is purely
   finite-fieldy.
- `precomp`: constants and precomputed values.
- `quaternion`: quaternion orders and ideals.
- `hd`: code to compute (2,2)-isogenies in the theta model.
- `id2iso`: code for Ideal <-> Iso.
- `verification`: code for the verification protocol.
- `signature`: code for the key generation and signature protocols.

The dependencies are depicted below.
```
 ┌─┬──────────┬─┐        ┌─┬──────────┬─┐      ┌─┬──────────┬─┐
 │ ├──────────┤ │        │ ├──────────┤ │      │ ├──────────┤ │
 │ │  Keygen  │ │        │ │   Sign   │ │      │ │  Verify  │ │
 │ ├──────────┤ │        │ ├──────────┤ │      │ ├──────────┤ │
 └─┴────┬─────┴─┘        └─┴────┬─────┴─┘      └─┴────┬─────┴─┘
        │                       │                     │
        └──────────────────┐    │                     │
                           │    │                     │
┌─────────────────┐    ┌───▼────▼────────┐            │
│                 │    │                 │            │
│   Quaternions   ◄────┤  Ideal <-> Iso  ├────────┐   │
│                 │    │                 │        │   │
└────────┬────────┘    └────────┬────────┘        │   │
         │                      │                 │   │
         │                      │     ┌───────────────┘
         │                      │     │           │
┌────────▼────────┐    ┌────────▼─────▼──┐    ┌───▼────────────┐
│                 │    │                 │    │                │
│ Multiprecision  │    │       2D        ├────► Precomputation │
│ integers (GMP)  │    │    Isogenies    │    │                │
│                 │    │                 │    │                │
└─────────────────┘    └────────┬────────┘    └───▲────────────┘
                                │                 │
                                │                 │
                                │                 │
                       ┌────────▼────────┐        │
                       │                 │        │
                       │ Elliptic curves ├────────┘
                       │   & isogenies   │
                       │                 │
                       └──┬───────────┬──┘
                          │           │
                          │           │
                          │           │
              ┌───────────▼───┐   ┌───▼───────────┐
              │     GF(p)     │   │     Fixed     │
              │       &       │   │   precision   │
              │    GF(p^2)    │   │   integers    │
              └───────────────┘   └───────────────┘
```

## Cortex-M4 implementation

Verification routines are supported in 32-bit embedded architectures running on bare metal environments such as the ARM Cortex-M4, but they are not directly supported by the build system of the present repository. The [pqm4 project](https://github.com/mupq/pqm4) is supported for evaluating SQIsign verification in the ARM Cortex-M4.

pqm4 assumes that the full NIST API (keypair generation, signing and verification) is available. Since only verification is supported, the remaining routines are mocked and must meet certain constraints, such as sharing the public key, signing a prespecified message and being of a specific size used by the testing and benchmarking binaries of pqm4. Therefore, additional KATs must be generated specifically for pqm4, which is done by a dedicated KAT generator for pqm4, found in `apps/PQCgenKAT_sign_pqm4.c`.

A copy of the most recent version of pqm4 as of the round 2 submission deadline, including an implementation of SQIsign verification generated directly from this repository using the procedure explained next, is made available [here](https://github.com/SQISign/the-sqisign-pqm4), in the `sqisign` branch.

If changes are made to the library, the `scripts/gen_pqm4_sources.sh` shell script can be run, from the root folder of the repository, to generate a pqm4-compatible folder structure in `src/pqm4/sqisign_lvl{1,3,5}`, which can then be copied to the `crypto_sign` folder of pqm4. Note that the pqm4 KAT generator is automatically run by this script.

## Acknowledgements

The reference implementation for finite field arithemtic (i.e., `src/gf/ref`)
was generated using [modarith](https://github.com/mcarrickscott/modarith) by
Michael Scott.

## License

SQIsign is licensed under Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

Third party code is used in some files:

- `src/common/aes_c.c`; MIT: "Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>"
- `src/common/fips202.c`: CC0: Copyright (c) 2023, the PQClean team
- `src/common/randombytes_system.c`: MIT: Copyright (c) 2017 Daan Sprenkels <hello@dsprenkels.com>
- `src/common/broadwell/{aes_ni.c, vaes256_key_expansion.S}`: Apache-2.0: Copyright 2019 Amazon.com, Inc.
- `src/common/broadwell/ctr_drbg.c`: ISC: Copyright (c) 2017, Google Inc.
- `src/mini-gmp/mini-gmp.c` and `src/mini-gmp/mini-gmp.h`: LGPLv3: Copyright 1991-1997, 1999-2022 Free Software Foundation, Inc.
- `src/quaternion/ref/generic/dpe.h`: LGPLv3: Copyright (C) 2004-2024 Patrick Pelissier, Paul Zimmermann, LORIA/INRIA
- `apps/PQCgenKAT_sign.c`, `apps/PQCgenKAT_sign_pqm4.c`, `src/common/ref/randombytes_ctrdrbg.c`, `test/test_kat.c`: by NIST (Public Domain)
