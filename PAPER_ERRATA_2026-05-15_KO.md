# 페이퍼 오류 / 누락 보고 — 2026-05-15

작성: yhwkorea (Sol)
대상 페이퍼:
- **[A] compact-SQIsign 페이퍼** "Compact Quaternion Algorithms for SQIsign" (Won Kim, Changmin Lee, Hyunwoo Yoo)
- **[B] KLKL25** "SQIsign with Fixed-Precision Integer Arithmetic" (Kim, Lee, Kim, Lee — ePrint 2025/1649)

배경: compact-SQIsign LVL1 sign timeout 무한 loop 진단 과정에서 두 페이퍼의 모델 불일치 / 분석 누락 발견. 핵심은 페이퍼가 cite한 "fixed-precision integer model"의 정확한 의미와 본문 algorithm 표기 사이의 mapping이 명시 안 됨.

---

## [A] compact-SQIsign 페이퍼 오류

### A-1. Appendix A.1 ↔ Algorithm 1 본문 표기의 model 불일치

**위치**: `91proof.tex` Appendix A.1, Lines 7-11
> "We follow the same fixed-precision integer model as in the bound analyses of LLL/L2/SizeReduce in [KLKL25]: all *integer* objects that Algorithm 1 manipulates are stored with a fixed budget. (namely `h_i` and, when the input lattice is integral, `b_i` **or an equivalent integer Gram-matrix representation**)"

cite한 KLKL25 model = **G 정수 + µ/r FLOAT** (KLKL25 Algorithm 13 line 1-2: `r_{0,0} ← FLOAT(G_{0,0}), µ ← FLOAT, T ← FLOAT`).

그러나 본문 Algorithm 1 line 2:
> `µ_{βj} ← b_β^T b_j*/B_j`,  `b_β* ← b_β − Σ µ_{βj} b_j*`,  `B_β ← (b_β*)^T b_β*`

`b_j*` (rational vector)와 `µ`(rational)을 직접 계산하는 표기. 이는 **Pohst rational model** 표기로 자연스러우나, cite한 model은 L2 (G integer + float µ).

**구현 문제**:
- 셋 (Pohst rational / Cohen integer GSO d-Λ / L2 float µ) 중 무엇을 골라야 하는지 명시 X
- compact-SQIsign 5e2782e가 ibq_t (Pohst rational) 골랐다가 어제 (2026-05-15) `compute_gs_single → ibq_reduce → ibz_gcd` **무한 loop** 발생
- 원인: ibq_mul 결과 buffer overflow (μ²·B 3-term 곱셈에서 분자/분모 누적)

**권장**: §3.1 또는 Appendix A.1에 표현 결정 명시 — KLKL25 L2 model 그대로 (G integer + float µ) 또는 Cohen integer GSO 둘 중 하나로 권고. fp precision 요구사항 동봉 (Nguyen-Stehlé 2009 식 `O(d log B) bits`).

### A-2. Appendix A.1 Lines 18-22 (Lemma 3 증명 (1)) — Pohst rational model에서 성립 X

**위치**: `91proof.tex` Lines 18-22
> "(1) Lines 2–5. In line 2, computing `µ_{βj}` and computing `B_β` need to compute inner product of two vectors so the size during this computation is at most `max ‖a_i‖²`."

inner product `<b_β, b_j*>` 자체는 `b_j*`가 rational이면 결과도 rational. 페이퍼는 이걸 "정수처럼 size ≤ ‖a‖²"라 다룸.

**정확한 분석**:
- L2 model (G 정수 + µ float) 가정 시: `<b_β, b_j*>`는 별도로 계산 안 함, `G_{β,j}` 정수 ≤ ‖a‖² ✓
- Pohst rational model (µ, b* 모두 ibq_t) 가정 시: `b_j*` 분모는 `det(B_1..B_{j-1})` 누적으로 ‖a‖^(2(j-1))까지. 분자 inner product는 더 큼. **‖a‖² 초과**

**권장**: 증명 시작에 "We adopt L2 model (KLKL25 Algorithm 13): G is integer Gram, µ/r/B are FLOAT" 명시. Pohst rational 표현은 별도 Lemma로 size bound 다시 증명 (또는 비추천).

### A-3. §3.2 Algorithm 2 (CompactIdealMultiplication) Lemma 4 증명 — dependent removal 분석 누락 [최우선]

**위치**: `03Ideal.tex` Lines 177-183 (Lemma 4 증명 Step 2)
> "By Lemma 3, during the execution of MLLL, the maximum size is at most `max ‖α_i β_j‖_∞² ≤ max nrd(α_i β_j)`"

**문제**:
- Algorithm 2 line 6: `M ← (α_i β_j)_{1≤i,j≤4}` = 4×16 행렬 (16 generators, 12 dependent)
- Algorithm 2 line 7: `(b_1, b_2, b_3, b_4) ← MLLL(M)` — dependent 12개 zero화 + 4 basis 추출
- Lemma 3 (mlll-bound)는 Algorithm 1 input dimension `g = ρ` (= output rank) 가정으로 bound 도출
- **dependent removal (Algorithm 1 line 12 "if b_m = 0 then go to line 24" 경로) 의 µ/B 갱신 분석이 Appendix A.1 어디에도 없음**

**실험 증거**: 어제 LVL1 sign timeout에서 quat_lattice_add → quat_mlll → compute_gs_single → ibq_reduce → ibz_gcd **무한 loop**. 진단 결과 dependent 12 generator를 zero로 못 만듦 (red_iter 25만까지 m=2에서 안 종료). `quat_lattice_add` (= Algorithm 2의 더 단순 버전)에서 이미 발생.

**권장**:
1. Lemma 3을 `g ≥ ρ` (redundant input) 경우로 일반화. dependent removal 단계의 µ/B/h 갱신 size bound 추가 증명
2. 또는 Algorithm 2 line 6 전에 LLL preprocessing (KLKL25 Algorithm 13) 명시 — 입력 16 generators 중 4개씩 LLL-reduce 후 MLLL 호출하는 경로
3. Algorithm 1 의 line 24-26 (b=0 처리)이 무한 loop 안 되는 종료 조건 증명 (Lovász 조건 위배 횟수 bound)

**우선순위 1 — 어제 무한 loop의 직접 원인이며 페이퍼 핵심 contribution(Algorithm 2)의 정확성에 직결.**

### A-4. abstract / §5 IBZ_LIMBS bound — 보조변수 buffer 누락

**위치**: 페이퍼 abstract (또는 §5 buffer 분석)이 IBZ_LIMBS 28 / 42 / 56 limbs claim (L1 / L3 / L5)

**문제**:
- Lemma 4 bound `(64p²/π⁴)·r_1²r_2²·nrd(I_1)·nrd(I_2)`는 **페이퍼 모델 정수 객체 (b, h, G)만** 카운트
- 실 구현 fixed-precision integer arithmetic은 정수 객체 + **µ 표현 보조변수** (Pohst → ibq 분자/분모, Cohen → d/Λ, L2 → float)를 같은 ibz_t buffer로 사용
- abstract bound는 이 보조변수 누락

**측정 (어제, 2026-05-15)**:
| IBZ_LIMBS | LVL1 sign 결과 |
|---|---|
| 28 (페이퍼 v2 의도) | LLL preprocessing 추가해도 항상 무한 loop |
| 36 (~2304 bit) | swap oscillation (red_iter 25만까지 m=2에서 안 종료) |
| 56 | 일부 시드 0.099s 정상 / 일부 시드 안 끝 |
| 64 | 5분 안 keygen 안 끝 (시드 의존) |
| 110 | 안전하지만 mlll 분 단위 |

**권장**: 두 가지 중 선택
1. abstract IBZ_LIMBS bound 정정 — 56 / 85 / 110 (Cohen GSO 기준 이론 상한)
2. abstract "IBZ_LIMBS 28"의 카운트 정의 명시 — "G entries만 카운트, µ float buffer는 별도" (KLKL25 GMP-free 정신과 맞으려면 이 길은 불가, mpfr 도입 필수)

---

## [B] KLKL25 페이퍼 오류 / 누락

### B-1. Algorithm 14 SizeReduce Lemma 22 line 8 — "ideal of O_0" 한정 조건이 propagation 시 누락

**위치**: KLKL25 page 42, Lemma 22 증명
> "since `|µ_{k,i}| = |r_{k,i}|/|r_{i,i}| ≤ G` **when the lattice is an ideal of O_0**, the maximum size of integers does not grow by [18]"

**문제**:
- "ideal of O_0" 가정 명시. 일반 lattice에는 µ bound 성립 X
- compact-SQIsign Algorithm 2의 product lattice `r_1 r_2 (I_1 I_2)`는 **left O_1-ideal × left O_2-ideal**. O_0 ideal이 아닐 수 있음 (일반 maximal order)
- compact-SQIsign이 KLKL25 Lemma 22 적용 시 이 한정 조건이 만족되는지 **별도 증명 필요한데 없음**

**권장**: Lemma 22를 일반 lattice로 일반화 또는 compact-SQIsign 측에서 product lattice의 O_0 ideal 동치성 증명 추가.

### B-2. FLOAT precision 자체 분석 없음

**위치**: KLKL25 Algorithm 13 / Algorithm 14 전체

**문제**:
- "FLOAT" type 정의 명시 X (precision 몇 bit?)
- 본문은 `[17][18]` (Nguyen-Stehlé 2009 L2 원본 추정) 인용으로 우회
- compact-SQIsign이 KLKL25 cite할 때 이 precision 요구사항이 implicit
- **5월 8일 dpe 53-bit가 부족했던 무한 loop의 본질은 페이퍼 어디에도 명시 X**

**Nguyen-Stehlé 2009 식**: precision = `O(d · log B) bits`, d=4, log B ≈ ‖a‖² → L1 ≈ 2080 bits, L3 ≈ 3128 bits, L5 ≈ 4104 bits

**권장**: §4 또는 Algorithm 13 caption에 "FLOAT precision = `(d + ε)·log_2 max_i ‖b_i‖²` bits" 같은 명시. dpe (53-bit)는 통상 부족, mpfr 또는 자체 fixed-bit float 필요.

### B-3. Lemma 22 line 11/14 분석 — 갱신 중간값 무시

**위치**: KLKL25 page 42, Lemma 22 line 11/14 분석
> "since each entry of the Gram matrix is the inner product of two basis vectors, it is at most the maximum length of the size-reduced basis vectors"

**문제**:
- `G_{k,j} ← G_{k,j} − X·G_{i,j}` 갱신은 일시적으로 **`X·G_{i,j}` 중간 곱셈**
- X = ⌊µ_{k,i}⌉의 bound 명시 X. line 8에서 `|µ| ≤ G`라 했으니 `|X| ≤ |µ| + 0.5 ≤ G + 0.5`
- 그러면 `X·G_{i,j} = G·G = G²` → bound 한 단계 위
- Lemma 22가 line 11/14의 갱신 중간값을 무시

**권장**: 중간값 bound `|X|·max(G) ≤ G²` 명시 + IBZ 카운트 시 이 ε 반영.

---

## 통합 권장 (compact-SQIsign 측 fix)

| 우선순위 | 항목 | 작업 |
|---|---|---|
| **1** | A-3 (dependent removal 분석) | Lemma 3을 `g ≥ ρ` 일반화 + Algorithm 2 line 6 전에 LLL preprocessing 명시 |
| **2** | A-1 (model 결정) | KLKL25 L2 그대로 채택 명시 — 단 GMP-free 위배(아래 mpfr 분석) |
| **3** | A-4 (abstract bound 정정) | 56/85/110 또는 카운트 정의 명시 |
| **4** | B-1, B-2, B-3 | KLKL25 측에 직접 보강 요청 |

---

## 구현 결정: Cohen integer GSO + IBZ_LIMBS 56

### mpfr 가능성 분석 (왜 안 되는가)

| | integer-only | GMP-free | 정확 동작 |
|---|---|---|---|
| L2 + dpe (현재 D2 / mlll.c) | ✓ | ✓ | ✗ (53-bit 부족, 5월 8일 무한 loop) |
| L2 + mpfr | ✗ (float type) | **✗ (mpfr는 GMP base library)** | ✓ |
| Cohen integer GSO | **✓** | **✓** | **✓** |

KLKL25 contribution 두 가지 = (i) integer-only + (ii) GMP-free. mpfr 도입은 양쪽 모두 깨뜨림.
→ **Cohen integer GSO + IBZ_LIMBS 56 (L1) / 85 (L3) / 110 (L5)**가 GMP-free 유지하는 유일한 길.

### 권장 IBZ_LIMBS

| Level | ‖a‖² | 이론 d_4 ≤ ‖a‖^8 | 실측 (the-sqisign 2026-04-19, Cohen) | 권장 IBZ_LIMBS |
|---|---|---|---|---|
| L1 | 520 bits | 2080 bits = 33 limbs | 3543 bits = 56 limbs | **56** |
| L3 | 782 bits | 3128 bits = 49 limbs | 5389 bits = 85 limbs | **85** |
| L5 | 1026 bits | 4104 bits = 65 limbs | 7102 bits = 111 limbs | **110** |

### 구현 변경

`src/quaternion/ref/generic/lll/mlll.c`:
- 현재: ibq_t µ[N][N], ibq_t B[N], compute_gs_single에서 ibq_mul/ibq_reduce
- 변경: ibz_t d[N+1], ibz_t Lambda[N][N] (Cohen 2.6.7 표현)
- size_reduce: `t = round(Λ_{m,l} / d_l)` 정수 나눗셈 (round-half-to-even)
- swap: Cohen Algorithm 2.6.7 swap update 식 (정수 곱셈/뺄셈만)
- Lovász 조건: `4 d_{m-2} d_m < (3 d_{m-1}² − Λ_{m,m-1}²) · (...)` 정수 부등식

`src/quaternion/ref/generic/include/intbig.h`:
- IBZ_LIMBS_lvl1: 110 → **56**
- IBZ_LIMBS_lvl3: 168 → **85**
- IBZ_LIMBS_lvl5: 222 → **110**

### 검증 계획

1. mlll.c 단위 테스트 (mlll_tests.c) PASS — Cohen 변환 후 동일 LLL-reduced basis 출력 확인
2. ctest signature_lvl1 100 random seed → 무한 loop 율 0%
3. KAT round-trip
4. L3 / L5 동일 절차

---

(끝. 1저자 공유 후 fix 방향 합의 → 구현 진행)
