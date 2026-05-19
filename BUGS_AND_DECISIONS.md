# Bugs and Critical Decisions (2026-05-19 session)

이 문서는 paper Issue 1~13 정공 적용 + sign+verify EXIT=0 도달 과정에서 발견한 결정적 버그와 설계 판단을 기록한다.
commit `71fca30` 본문 외 별도로 보존할 가치가 있는 항목만.

---

## 결정적 버그 1: LatticeDual의 GCD normalization 누락

**위치**: `src/quaternion/ref/generic/lattice.c quat_lattice_dual_without_hnf`

**Paper 본문 (91proof.tex:435 Algorithm LatticeDual)**:
```
Line 1: (A, Δ) ← AdjugateWithDet(M)
Line 2: M^# ← r · A^T
Line 3: r^# ← Δ
Line 4-6: if r^# < 0 negate
Line 7: g ← gcd(r^#, {M^#_a,b})     ← 우리 코드 누락
Line 8: M^# /= g, r^# /= g           ← 우리 코드 누락
```

**증상**: dual.basis 가 4×|entry| bit 까지 폭발 (cofactor expansion 누적). 측정 **3598 bit** (Lvl1 110 limbs). 그 다음 단계 add_mlll 의 ML2 input이 3598 bit → 65536 iter cap 도달 → hang.

**Fix**: `quat_lattice_reduce_denom(dual, dual)` 한 줄 추가 (이미 같은 패턴 helper 존재).

**효과**: dual.basis **3598 bit → 62 bit (~58×)**. intersect_mlll 출력 **1946 bit → 255 bit (paper Line 14 bound 1626 bit 안)**.

**이게 sign+verify EXIT=0 도달의 결정적 fix.** 다른 모든 paper Issue 적용이 이 누락 한 줄로 무력화되어 있었음.

---

## 결정적 버그 2: paper MLLL = ML2를 quat_mlll로 잘못 wire

**위치**: `src/quaternion/ref/generic/lll/mlll.c quat_lattice_add_mlll`

**Paper 본문 (03Ideal.tex 초입)**:
> "we use the modified LLL algorithm with floating-point arithmetic [NS09],
> which we call **ML2** or modified LLL algorithm"
>
> "We also call **LLL as the special case of MLLL** algorithm, when d=4
> in Algorithm ML2."

즉 paper의 "MLLL"은 **NS09 Fig 9 ML2 (53-bit float)** 이지, Cohen integer GSO MLLL (Pohst 87)이 아니다.

**우리 잘못된 wire** (기존):
```c
quat_mlll(&(res->basis), &rank, generators, 8, alg);  // Cohen integer GSO
```

**증상**: 큰 input에서 swap oscillation 무한 loop. paper Issue 8 wire가 부분 적용된 형태 (sign.c:89 intersect_mlll 호출은 적용했지만 그 안의 add_mlll이 Cohen MLLL을 부르고 있었음).

**Fix**: `quat_ml2(reduced, 4, generators, 8, NULL)` 호출로 교체. paper의 진짜 MLLL.

**교훈**: paper "MLLL" 용어가 NS09 ML2를 가리킨다는 점이 paper 첫 section에 있지만 우리 구현은 함수명 그대로 따라 Cohen MLLL 호출. 함수명-알고리즘 매핑 검증 필요.

---

## 결정적 결정 3: ml2.c oscillation detection 제거

**위치**: `src/quaternion/ref/generic/lll/ml2.c ml2_main_loop`

**기존**: same-state (kappa, zeta) 8 iter 중 4번 발견 시 abort + HNF fallback.

**Paper Issue 13 author reply 인용**:
> "MLLL 진행 중에 size-reduce를 하면 entry가 증가할 수가 없음.
> (size가 계속 줄어들기 때문)"

즉 paper는 size-monotonic 보장하므로 abort 필요 없다고 주장. 우리 abort는 paper-strict convergence를 차단.

**결정**: oscillation detection 제거 + OUTER_MAX 4096 → 65536, LazySizeReduce 8192 → 65536.

**검증**: oscillation detection 제거 후 ML2 호출 3회 모두 정상 종료 (`aborted=0`, `rho=4 rank-4 basis 정상 반환`). 이전 abort 3회와 정반대. **paper Issue 13 답변 검증됨**.

**미해결**: 일부 측정 (LatticeDual GCD fix 적용 전) 에서 ML2 65536 iter cap 도달 (`MAIN-OVER rho=-1`). LatticeDual GCD fix 후에는 도달 안 함. 즉 ML2 cap 도달은 LatticeDual GCD 누락이 만들어낸 비정상 input이 원인이지 ML2 자체 numerical 문제 아님.

---

## 결정적 결정 4: paper Issue 8 wire 완성도 — ideal.c:189 누락 발견

**위치**: `src/quaternion/ref/generic/ideal.c quat_lideal_inter`

**기존**: `quat_lattice_intersect(&inter->lattice, ...)` — HNF version 호출.

**문제**: sign.c hot path는 두 chain:
- sign.c:89 `quat_lattice_intersect_mlll` — 이미 wire (commit `ec69081`)
- sign.c:81 `quat_lideal_inter` → ideal.c:189 → **HNF version 호출** ← wire 빠짐

즉 sign에서 호출하는 lattice intersection 중 절반이 HNF version으로 polynomial-time blow-up 일으키고 있었음.

**Fix**: ideal.c:189 도 `quat_lattice_intersect_mlll` 로 변경. paper Issue 8 본격 완성.

---

## 결정적 결정 5: normeq.c paper Issue 5 (mod N) 본격 적용

**Paper Issue 5 author reply 인용**:
> "**반드시** mod N을 적용해야만 함. (안그러면 γβ, N을 이용해서
> modified LLL 돌릴 때 터짐) mod N·O_0가 맞음"

**기존 코드**: γ 샘플만 (β 곱과 mod N 단계 누락).

**Fix**: `quat_sampling_random_ideal_O0_given_norm` 의 `is_prime` path에 β 샘플 + `quat_alg_mul(gen, gen, gen_rerand)` + `ibz_mod(gen.coord[i], norm)` 추가. paper Algorithm body 본문대로.

**효과**: ideal generator coord 가 [0, N) 안. 그 후 lattice 연산의 input invariant 보장.

---

## 측정 결과 (paper Theorem 2 bounds)

NIST Lvl1 (p ≈ 248 bit), 256 limbs build, sign 2 round EXIT=0 측정:

| Step | Measured max bit | Paper Theorem 2 bound (Lvl1) | Margin |
|---|---|---|---|
| `quat_lattice_intersect_mlll` output (Line 14) | 255-320 | 2^138·p^6 = 1626 | 5-6× 여유 |
| `quat_lattice_add_mlll` ML2 input (dual after GCD) | 62 / 5 | - | 이전 9886 → 62 |
| `sample_response` radius (Line 24) | 520 | 2^21·p^7 = 1757 | 3.4× 여유 |

**모든 측정이 paper Theorem 2 bound 안**.

---

## 미해결 / 다음 세션 진입점

1. **debug fprintf trace 정리**: 본 commit에 `[COMMIT]`, `[RESP]`, `[IMLLL]`, `[ML2 #N]` trace 다수 포함. 정상 동작 검증 후 NDEBUG guard 또는 제거 필요.
2. **IBZ_LIMBS_lvl1 = 28 (paper claim) 시도 미수행**. 현재 110 (KLKL25 baseline). 28 빌드 + sign 테스트는 후속 검증.
3. **paper Issue 9, 11, 12 미적용**:
   - Issue 9: J_t bound — KLKL25 J_t norm 재사용 (현재 미적용)
   - Issue 11, 12: IdealToIsogeny Clapoti spec대로 (현재 J̄·I 곱만)
4. **paper Line 13 bound (2^41·p^5 ≈ 1281 bit) 미측정**. Line 14, 24만 확인.
5. **sample_response 내부 LLL 시간**: 측정 안 함. paper bound 안에 들어와도 wall-clock 측정 필요.

---

## 참고: 이전 세션과의 차이

이전 세션 (2026-05-17 ~ 18)은 paper-strict `quat_mlll` (Cohen integer GSO)이 lvl1 110 limbs에서 swap oscillation 무한 loop라 결론냈음 (메모리 `project-compact-sqisign` "최종 결론"). **이번 세션은 그 결론을 뒤집음** — 진짜 원인은:
- LatticeDual GCD 누락이 만들어낸 비정상 input
- paper "MLLL" = ML2 인데 Cohen MLLL을 호출하는 오용

두 fix 후 ML2가 paper-strict로 정상 작동. **paper Issue 13 답변은 정확했고, 우리 구현이 paper와 달랐던 것**.
