# compact-SQIsign Sign 함수별 입출력 + paper bound 계획표

생성: 2026-05-19. 측정: IBZ_LIMBS_lvl1=256 (~sonnet sub 측정 + memory 종합).
Paper: `/tmp/compact_v2/{03Ideal,04Sampling,05Improve,93idtoiso}.tex`.

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
| **R3** | **L = I_sk·I_chl ∩ dual(I_com)** | **`quat_lattice_intersect_mlll`** | **CompactLatticeIntersection (`03Ideal:152`)** | **9886 (ibz_mul internal)** | **L13: $2^{41}p^5 \approx 1281$**<br>**L14: $2^{138}p^6 \approx 1626$** | **❌ stuck (500k iter cap, swap oscillation, `/tmp/sig37.err`)** |
| R4 | small x ∈ L | `quat_lattice_sample_from_ball` | (`05Improve:60` L24) | rad=649, out=325 | L24: $2^{21}p^7 \approx 1757$ | OK (R3 통과 후) |
| R5 | x norm | `quat_alg_norm` | — | 865 → **1977** | $2\cdot\text{in}$ | ❌ saturate |
| R6 | x conjugate | `quat_alg_conj` | — | 865 | — | OK |
| R7 | (x̄, N) → I_com_resp | `quat_lideal_create` | — | 865+513 → 514 | — | OK |
| R8 | I_com_resp 내부 inv | `ibz_mat_4x4_inv_with_det_as_denom` | AdjugateWithDet | det=**5233**, inv=2348 | — | ❌ saturate |

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

| Path | 설명 | IBZ_LIMBS | paper claim 도달 | 작업 크기 |
|---|---|---|---|---|
| **A** | ml2-fallback + reduce_denom 재측정 | 110 → 측정 가능 | ❌ (1942 bit) | 소 (HANDOFF 이어서) |
| **B** | IBZ_LIMBS=200 baseline 측정 | 200 | ❌ 명시 포기 | 중 |
| **C** | 1저자 보고: paper Issue 8 wire 한계 + paper bound 재정정 제안 | — | — | 중 (REPORT 갱신) |
| **D** | Paper Algorithm 수정 제안 (Path A: 새 ideal 표현) | — | ? | 대 (한 세션 초과) |

**사용자 답변(이전 turn)**: Path A (ml2-fallback + reduce_denom 굽파기)로 진행 결정.

---

## 다음 step (Path A 기준)

1. mlll.c:649 `quat_lattice_intersect_mlll` 본문 검토 — paper-strict 분기 vs ml2-fallback 분기 어느 경로 실행되는지
2. ml2 (`lll/ml2.c`) wire가 fallback에서 active한지 확인
3. `reduce_denom` 추가 적용 위치 확인 (HANDOFF.md Path B)
4. 28 limbs 시도하지 말고 110 limbs baseline에서 BD-INTER bit 측정 — paper bound 1626 vs 측정값 직접 확인
5. ml2-fallback 경로의 BD-INTER 측정 + paper bound 비교가 1저자 보고의 데이터 핵심
