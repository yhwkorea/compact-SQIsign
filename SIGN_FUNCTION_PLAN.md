# compact-SQIsign Sign 함수별 입출력 + paper bound 계획표

생성: 2026-05-19. 측정: IBZ_LIMBS_lvl1=256 (~sonnet sub 측정 + memory 종합).
Paper: `/tmp/compact_v2/{03Ideal,04Sampling,05Improve,93idtoiso}.tex`.

## 2026-05-19 후속 update (commit `5ff147b`)

3개 saturate 지점 (R3, R5, R8) 모두 해결됨. 28-limb lvl1 sign+verify EXIT=0 (seed=1, 33s).
- **R3 `quat_lattice_intersect_mlll`** — IMLLL L14 측정 **9886 → 324 bit** (28-limb). paper Issue 8 wire + LatticeDual GCD normalization (commit 71fca30) + `quat_lattice_alg_elem_mul`의 HNF→ML2 교체 (commit 5ff147b) 합산 효과. paper bound 1626 bit 안.
- **R5 `quat_alg_norm`** — Issue 9 `_with_norm` 라우팅으로 known-N 경로에서 `quat_alg_norm(x)` 자체 호출이 제거됨 (id2iso 1977 bit transient 사라짐). 남은 직접 호출 ALGNORM peak = 648 bit (response quat norm post-LLL).
- **R8 `ibz_mat_4x4_inv_with_det_as_denom`** — `quat_lideal_create_with_norm`가 norm 재계산 chain skip해서 NDEBUG에선 5233-bit det 안 만들어짐. 남은 호출처(`quat_lattice_add` 내부)는 ML2 #0 peak 1520 bit에 흡수, 28-limb cap 안.

자세한 최종 측정 테이블은 commit message 또는 [`MEMORY.md` index의 `project_sqisign_signtime_chains`] 참조.

---

## 표기
- **Max bit**: 그 함수 내부 또는 출력 ibz_t의 최대 nonzero limb 위치 (256 limbs 측정)
- **paper bound**: Lvl1 prime p ≈ 248 bit 기준
- **28 limbs status**: `_SQI_IBZ_LIMBS_lvl1=28` (paper claim, 1792 bit) 빌드에서 saturate 여부

---

## KeyGen (`alg:KeyGen` in `05Improve.tex`)

| # | 단계 | 코드 함수 | paper Algorithm | Max bit | paper bound | 28 limbs |
|---|---|---|---|---|---|---|
| K1 | I_sk 샘플 | `quat_sampling_random_ideal_O0_given_norm` | RandomIdealGivenPrimeNorm (`04Sampling:38`) | 514 | $N \le 2^{f-1}$ | OK |
| K2 | I_sk → φ_sk | `dim2id2iso_arbitrary_isogeny_evaluation` | IdealToIsogeny (`93idtoiso:71`) | curve 측 | — | — |
| K3 | sk mat | (sign.c struct) | — | small | — | OK |

---

## Sign.Commit (`alg:Sign` lines 1-3)

| # | 단계 | 함수 | paper Alg | Max bit | bound | 28 |
|---|---|---|---|---|---|---|
| C1 | I_com 샘플 | `quat_sampling_random_ideal_O0_given_norm` | RandomIdealGivenPrimeNorm | 514 | $2^f$ | OK |
| C2 | prime-norm reduce | `quat_lideal_prime_norm_reduced_equivalent` | RandomEquivalentPrimeIdeal (`04Sampling:306`) | 514 → **139** (LLL ↓) | $O(\sqrt p)$ | OK |
| C3 | I_com → φ_com | `dim2id2iso_arbitrary_isogeny_evaluation` | IdealToIsogeny | curve | — | — |

---

## Sign.Challenge (`alg:Sign` lines 4-8)

| # | 단계 | 함수 | Max bit | 28 |
|---|---|---|---|---|
| H1 | c = H(E_com, msg) | (sign.c inline) | 256 | OK |
| H2 | canon basis 변환 | `ibz_mat_2x2_eval` | 615 → 864 | OK |
| H3 | kernel → I_chl | `id2iso_kernel_dlogs_to_ideal_even` | 368 → 250 | OK |

---

## Sign.Response (`alg:Sign` lines 9-15) — **병목 구간**

| # | 단계 | 함수 | paper Alg | Max bit | paper bound | 28 limbs |
|---|---|---|---|---|---|---|
| R1 | I_sk · I_chl | `quat_lideal_inter` | — | 260 → 386 | — | OK |
| R2 | I_com conjugate | `quat_lattice_conjugate_without_hnf` | — | 139 → 139 | — | OK |
| **R3** | **L = I_sk·I_chl ∩ dual(I_com)** | **`quat_lattice_intersect_mlll`** | **CompactLatticeIntersection (`03Ideal:152`)** | ~~9886~~ → **324** (28-limb) | **L14: $2^{138}p^6 \approx 1626$** | ✅ **RESOLVED (5ff147b)** |
| R4 | small x ∈ L | `quat_lattice_sample_from_ball` | (`05Improve:60` L24) | rad=649, out=325 | L24: $2^{21}p^7 \approx 1757$ | OK (R3 통과 후) |
| R5 | x norm | `quat_alg_norm` | — | ~~1977~~ → **648** (Issue 9 라우팅) | $2\cdot\text{in}$ | ✅ **RESOLVED (5ff147b)** |
| R6 | x conjugate | `quat_alg_conj` | — | 865 | — | OK |
| R7 | (x̄, N) → I_com_resp | `quat_lideal_create` | — | 865+513 → 514 | — | OK |
| R8 | I_com_resp 내부 inv | `ibz_mat_4x4_inv_with_det_as_denom` | AdjugateWithDet | ~~5233~~ → skip된 경로 (NDEBUG) | — | ✅ **RESOLVED (5ff147b, via _with_norm)** |

---

## Sign.AuxIsogeny (`alg:Sign` lines 16-20)

| # | 단계 | 함수 | Max bit | 28 |
|---|---|---|---|---|
| A1 | aux ideal 샘플 | `quat_sampling_random_ideal_O0_given_norm` | 514 | OK |
| A2 | aux pushforward | `quat_lideal_inter` | 386 | OK |
| A3 | aux isogeny eval | `dim2id2iso_arbitrary_isogeny_evaluation` | curve | — |

## Sign.HD (`alg:Sign` lines 21-24)

| # | 단계 | 함수 | 측 |
|---|---|---|---|
| HD1 | dim-2 isogeny | `theta_chain_compute_and_eval_randomized` | curve |

## Verify (`alg:Sign` 별도 `alg:Verify`)

| # | 단계 | 측 | 28 |
|---|---|---|---|
| V | challenge 재구성 + 비교 | curve only | OK (ibz heavy 0) |

---

## 핵심 진단

**3개 saturate 지점 (28 limbs 도달 막는 곳)**:

1. **R3 `quat_lattice_intersect_mlll`** — **알고리즘 수준 막힘**.
   - paper bound 1626 bit, 측정 9886 bit (5.5× 초과)
   - paper-strict `quat_mlll` 500k iter swap oscillation (`MLLL-CAP` trigger)
   - **이게 paper claim 28 limbs 불가능의 결정적 근거**
   - 메모리 `project_compact_sqisign.md` 최종결론 = 이 측정 그대로

2. **R5 `quat_alg_norm`** — 자릿수 2배 (대수적 사실)
   - 입력 865 bit (Lvl1 $\nrd \le 2^{\log p \cdot 3.x}$) → 출력 1977 bit
   - 28 limbs 안에 못 들어감

3. **R8 `ibz_mat_4x4_inv_with_det_as_denom`** — cofactor expansion
   - det 5233 bit, inv 2348 bit
   - quat_lideal_create 내부에서 호출됨

---

## 진행 가능 path

~~Path A~~ → **DONE in commit `5ff147b`** (5월 19일).

paper Issue 8 본 정공이 `quat_lattice_alg_elem_mul`에 미적용이던 게 R3/R5/R8 saturate의 공통 source였음. 거기 HNF를 ML2(d=4)로 교체하니 모든 측정 paper bound 안에 들어옴. 다음 step 들 (Path B/C/D) 불필요.

## 최종 측정 (lvl1, IBZ_LIMBS=28, seed=1)

```
ML2 #0 (keygen)       peak 1520 bit < 1757 (paper sign bound)
ML2 #1 (commit)            1516
ML2 #2 (response)          1411
ALGNORM (R5 영역)           648
IMLLL L14 (R3 영역)          324
sample_response (R4)        522
LMUL post_mul              250
sampling (mod N path)       512
```
모두 28-limb cap 1792 bit 안. paper Theorem 2 / Theorem keygen-bound 모든 라인 만족.
