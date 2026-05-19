# Paper Bound 도달 계획표 (2026-05-19 04:05 KST)

목표: paper Theorem 2 sign bound (Line 13: $2^{41}p^5\approx1281$bit, Line 14: $2^{138}p^6\approx1626$bit, Line 24: $2^{21}p^7\approx1757$bit, 총합 1774 bit / 28 limbs) **안에 측정값이 들어올 때까지**.

## 현 진척 (paper Issue 1~13 정공 진행)

| # | Issue | 답변 정공 | 현 상태 | 측정 |
|---|---|---|---|---|
| 1~4 | 새 RandomIdealGivenPrimeNorm | normeq.c:257 패치 (γ + β + γβ mod N) | ✓ 코드 적용 | ⏳ 검증 진행 중 |
| **5** | **mod N 반드시** | 같음 | ✓ 코드 적용 | ⏳ 검증 |
| 6, 7 | bound 안 바뀜 | - | - | - |
| **8** | HNF만 ML2/MLLL | 3-tier wire (mlll.c:649 intersect_mlll Line 5-6/14 LLL + ideal.c:189 wire + Issue 5 mod N) | ✓ 코드 적용 | ⏳ 검증 |
| 9 | J_t bound | KLKL25 norm 재사용 | ❌ 미적용 | - |
| 10 | 병목 재파악 | quat_alg_norm 등 | ⏳ 측정 |
| 11, 12 | IdealToIsogeny Clapoti spec대로 | 현재 J̄·I 곱만 | ❌ 미적용 | - |
| **13** | **dpe 53-bit + size 단조** | **paper-strict size_reduce (oscillation detection 제거, iter cap 65k)** | ✓ **검증됨** | **ML2 abort=0, rho=4 정상 (이전 osc 3→0)** |

## 남은 문제 (순위)

| # | 문제 | 출처 | 해결방안 | 우선순위 |
|---|---|---|---|---|
| **P1** | **sign 10분 timeout (commit() 안 hang)** | stdout `--commit done--` 미출력 | **commit() trace 추가** (방금) + 측정 → sampling/prime_norm/dim2id2iso 중 어디 식별 | ★ now |
| P2 | paper bound 측정 (ibz_mul max bit) | 이전 측정 9886 bit, paper-strict 진행 후 미측정 | sign 끝까지 진행 후 BD-INTER trace + ibz_mul max bit | ★ |
| P3 | paper Issue 9 (ideal mult bound J_t) | KLKL25 J_t norm 재사용 안 됨 | quat_lideal_inter 또는 별도 helper에서 J_t norm 활용 | 중 |
| P4 | paper Issue 11/12 (Clapoti spec대로) | dim2id2iso 가 paper 다른 알고리즘 사용 가능 | dim2id2iso 본문 vs spec 비교 | 중 |
| P5 | quat_alg_norm 2× growth | 자릿수 본질적 2배 | reduce_denom + LLL output 후 norm 작아짐 검증 | 저 |

## P1 해결 단계 (즉시)

1. commit() 단계별 stderr trace 추가 (방금 완료): COMMIT enter / sampling done / prime_norm_reduced_equivalent done / dim2id2iso done
2. 빌드 + sign 5분 측정 → 어느 trace까지 찍히고 hang
3. 식별된 위치 paper-strict 비교 + fix
4. iter

## P2 해결 단계 (sign 진행 후)

1. ibz_mul caller breakdown trace (이전 sonnet subagent infrastructure 재활용)
2. paper bound 1281/1626/1757 bit와 측정 비교
3. 차이 식별 → 추가 paper fix

## 측정 가설

paper Issue 1~5 mod N 적용 후 ibz_mul max 줄어들 것 (γβ mod N → coord 작음). 측정 필요.

paper Issue 8 wire (intersect_mlll Line 5-6/14 LLL) 후 lattice basis size 줄어들 것. 측정 필요.

paper Issue 13 size_reduce monotonic + paper-strict 진행 후 ML2 output basis size 작음. **이번 측정 ML2 rho=4 (rank-4 정상 반환)** = 검증 ✓.

paper bound 1774 bit / 28 limbs 도달 가능성: P3, P4 적용 후 재측정에 달림.
