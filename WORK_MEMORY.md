# Compact SQIsign implementation memory

Last updated: 2026-08-04 (Asia/Seoul)

## Scope

- Work only in this `compact-SQIsign` repository, on branch `worst-case`.
- Do not mix this task with the sibling `cw-sqisign-response` project.
- Source specification: `Compact_Quaternion_Algorithms_for_SQIsign (31).pdf`,
  main body.

## Confirmed paper requirements

- Algorithm 2, `CompactIdealMultiplication`: LLL/ML2-reduce the input
  bases, form the 16 ordered products, then run ML2 on those generators.
- Lemma 13: for nonzero integral invertible ideals with
  `O_R(I1) = O_L(I2)` and coprime norms, `I1 intersect I2 = I1 I2`.
- Algorithm 3, `CompactIdealIntersection`: for two integral left
  `O_0`-ideals, reduce both bases, compute all 16 reduced-trace pairings,
  set `d = gcd(nrd(I1), nrd(I2), traces)`, set
  `a = nrd(I1)/d`, `b = nrd(I2)/d`, and run ML2 on the eight generators
  of `b I1 + a I2`.
- Main-body uniform worst-case bit bounds are 1830/2752/3611 bits for
  NIST-I/III/V. Because `ibz_t` is signed two's-complement, the selected
  complete 64-bit storage widths are 29/44/57 limbs.
- ML2 floating-point precision remains 53 significant bits (`double`).

## Implemented state

- Baseline branch: `worst-case`, commit `25f3485`.
- `quat_lideal_inter` now routes the two-left-`O_0` ideal case through the
  paper's Algorithm 3. The implementation L2-reduces both inputs, computes
  the 16 exact reduced-trace pairings, derives `d`, `a`, and `b`, then runs
  full-rank ML2 on the eight scaled generators. The returned ideal norm is
  set directly to `nrd(I1) * nrd(I2) / d`.
- `quat_lattice_mul_mlll` now implements Algorithm 2: both inputs are
  L2-reduced before forming the 16 ordered quaternion products; a full-rank
  ML2 retry policy produces the result over the product denominator.
- `compute_response_quat_element` uses
  `conjugate(I_commit) * I_chall_secret` in that noncommutative order. It
  first checks the Lemma-13 coprime-norm condition and requests a signing
  retry if it is not met.
- Both compact operations publish output only after all exact divisions,
  overflow preflights, and rank checks succeed. Input/output aliasing is
  supported.
- `IBZ_LIMBS_lvl1/lvl3/lvl5` are `29/44/57`. The corresponding negative
  precomputation constants were shortened by exact two's-complement sign
  extension; their represented integer values are unchanged.
- `IBZ_HNF_ROUTE_BITS_lvl1/lvl3/lvl5` remain `28*64/43*64/56*64`. The extra
  signed storage limb must not route larger values into HNF.

## Changed files

- Precision and documentation: `README.md`, `include/intbig.h`, and the
  three `src/precomp/ref/lvl{1,3,5}/quaternion_data.c` files.
- Compact ideal algorithms and API: `ideal.c`, `lll/mlll.c`,
  `include/quaternion.h`, and `mlll_internals.h`.
- Signing dispatch: `src/signature/ref/lvlx/sign.c`.
- Regression coverage: `test/ideal.c` and `test/ml2_correctness.c`. The
  former includes a trace-GCD fixture with `gcd(N1,N2)=17`, `d=1`, and
  unequal denominators; the latter includes a composable noncommutative
  product checked against the small exact-HNF path and against its reverse.
- Persistent handoff: this `WORK_MEMORY.md`.

## Baseline verification

- WSL Ubuntu 24.04, GCC 13.3, CMake 3.28.3 and Ninja are available.
- Clean-baseline `sqisign_test_quaternion_lvl1`: PASS.
- Clean-baseline Release/ref `sqisign_lvl1_CORRECTNESS`: PASS.
- A pre-existing full-build link failure affects
  `sqisign_test_verification_encoding_lvl1` (`randombytes` undefined); it is
  unrelated to this change.

## Changed-tree verification

- WSL Debug/ref build with `ENABLE_INTBIG_OVERFLOW_CHECK=ON`: all lvl1/3/5
  quaternion and end-to-end scheme targets compile.
- Debug quaternion tests: lvl1 and lvl3 passed. The first lvl5 run failed
  only the randomized `lat_ball_sample_from_ball_randomized` test while all
  new ML2/ideal tests passed; an immediate independent lvl5 rerun passed.
- Debug end-to-end `sqisign_lvl{1,3,5}_CORRECTNESS`: all passed, followed by
  five consecutive passes per security level.
- Release/ref build: all lvl1/3/5 quaternion and scheme targets compile;
  all six corresponding CTest cases pass.
- `git diff --check`: pass.
- A focused review plus an independent second audit found no concrete
  correctness defect in Algorithm 2/3 arithmetic, denominator handling,
  fixed-width preflights, publication semantics, or signing order.
- The repository's separate `sqisign_test_signature_lvl{1,3,5}` targets
  still fail to link on the pre-existing missing
  `sqisign_gen_sqisign_secure_free` symbol. The end-to-end scheme binaries,
  which exercise signing, link and pass at all levels.

## Second correctness audit

- Three independent read-only audits rechecked the Algorithm 2/3 formulas,
  Lemma-13 assumptions and multiplication order, signed precision, HNF
  routing, precomputation constants, overflow preflights, and Git state. No
  concrete implementation defect or unsafe-overflow path was found.
- A mechanical second check covered all 87 shortened negative precomputation
  initializers. Every retained prefix was identical, every discarded limb
  was all ones, and every new top limb retained its sign bit.
- Fresh overflow-check Debug tests passed for all three quaternion targets.
  End-to-end signing passed ten consecutive fresh-randomness runs at each
  security level, followed by another all-level pass after test hardening.
- The audit found two test-discrimination gaps, not production defects, and
  closed both:
  - Algorithm 3 now has a fixed case where the norm gcd is 17 but the exact
    sum norm and trace-derived `d` are 1, with input denominators 1 and 2.
    The compact result and norm 1156 match the independent dual/HNF oracle.
  - Algorithm 2 now has a composable noncommutative product with nontrivial
    denominators. It matches the independent small exact-HNF product, while
    the reverse product is explicitly different.
- With those fixtures present, Debug/overflow-check and Release/ref builds
  and all lvl1/3/5 quaternion plus end-to-end scheme CTest cases pass.
- The Release rebuild emitted the already-existing `intbig.c:1277`
  `-Wmaybe-uninitialized` warning in the random test helper; it is outside
  these changes and does not fail the build.

## Delivery / next action

- Initial implementation commit `f772348` was pushed to
  `origin/worst-case`. The second audit adds only the two deterministic
  regression fixtures and this updated handoff record.
- A fresh pre-commit fetch confirmed that both local `HEAD` and
  `origin/worst-case` were still `f772348`; the remote had not advanced.
- This file is included in the delivery commit. If this copy was read from
  `origin/worst-case`, the push completed. If it exists only in a local
  clone, the sole remaining delivery command is `git push origin worst-case`.
