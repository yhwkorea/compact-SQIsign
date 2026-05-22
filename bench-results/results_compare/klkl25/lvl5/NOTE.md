# KLKL25 lvl5

## Methodology divergence from lvl1 / lvl3

Wall-clock measurement of `KeyGen` and `Sign` for the KLKL25 fixed-precision
baseline at NIST-V was **not feasible** on this machine. A single KeyGen
invocation at lvl5 spends most of its time in `quat_lll_core`
(`quat_lideal_reduce_basis → dim2id2iso_ideal_to_isogeny_clapotis →
sqisign_keypair`) — i.e. LLL reduction on 222-limb integers, which the paper
this fork accompanies *replaces* with the cheaper MLLL / ML2 pipeline at the
paper's reduced precision. Confirmed with `gdb`-attached backtraces over a
multi-minute window: PC walks through different offsets inside
`quat_lll_core` (so the function is making progress, not stuck in an
infinite loop) — it just genuinely takes far longer than our per-iter budget.

Concretely:

- 4-parallel attempts with a 1-hour per-iter cap: 0 / 4 valid, all 4
  hit the 3600 s cap.
- Per the upstream's own `RESULTS_KLKL25.md`, NIST-V at lvl5 was 0 / 20
  valid under a 300 s cap.

## Verify, however, is measurable

Verify itself runs on the EC side and does not pay the 222-limb LLL cost,
so it is fast even at lvl5. We measure it from precomputed KAT data
(`KAT/PQCsignKAT_701_SQIsign_lvl5.rsp`, entry `count = 0`) using a new
`apps/benchmark_verify_from_kat.c` driver shipped in the patched KLKL25
zip (`PATCHES.md`, patch 8). The driver reads `(pk, m, sm)` from the KAT
file and times `crypto_sign_open` 20 times.

This is methodologically equivalent to measuring Verify after a successful
KeyGen + Sign: the same `crypto_sign_open` codepath runs on the same
fixed-precision arithmetic, regardless of where the (pk, sm) came from.

## Files

- `lvl5/SUMMARY_verify.tsv` — 20 valid samples
- `lvl5/SUMMARY_keypair.tsv` — absent (KG not measurable on this hardware)
- `lvl5/SUMMARY_sign.tsv` — absent (Sign not attempted; KG bottleneck makes
  20 valid Sign samples impractical too)
- `lvl5/verify_iter<N>.{log,err}` — per-attempt raw output (20 entries)
