# Phase 2 Trial — sign.c Hot Path MLLL 통합 (보류)

작성: 2026-05-15 08:56
대상: compact-SQIsign main (commit 81bd710 + 908c93d 위)
상태: **trial only, commit X (sign.c는 HNF 그대로)**. 학교 컴퓨터 인계용.

---

## 1. 시도 내용

`src/signature/ref/lvlx/sign.c:85` 의
```c
quat_lattice_intersect(&lattice_hom_chall_to_com, &lideal_chall_secret.lattice, &lat_commit);
```
를
```c
quat_lattice_intersect_mlll(&lattice_hom_chall_to_com, &lideal_chall_secret.lattice, &lat_commit, &QUATALG_PINFTY);
```
로 변경 — Cohen 정수 GSO MLLL alternate (commit 908c93d의 wrapper) 호출.

---

## 2. 측정 결과

### 환경
- WSL Ubuntu, GCC 14 (compact-SQIsign 표준 빌드)
- IBZ_LIMBS_lvl1 = 110 (KLKL25 baseline)
- `ulimit -s unlimited` (KLKL25 baseline의 dim2id2iso.c:574 stack overflow 회피)

### Build matrix
| Build | sign.c integration | sign 1 iter 시간 |
|---|---|---|
| `build_debug/` (Debug, NDEBUG OFF) | OFF (baseline) | **2m26s + abort** algebra.c:188 quat_alg_make_primitive Assertion 'ok' failed (sk-dependent invariant; PR9 영역) |
| `build_debug/` (Debug, NDEBUG OFF) | ON (Cohen MLLL) | **10분 timeout**, `keygen done` 까지 |
| `build/` (Release, NDEBUG ON, broadwell) | ON (Cohen MLLL) | **5분 timeout**, `keygen → commit → challenge_ideal_signature` 까지, `compute_response_quat` 에서 hang |
| `build/` (Release, NDEBUG ON, broadwell) | OFF (baseline) | **5분 안에 6+ sign retry 완주** (commit → challenge → response → backtracking 패턴 6+회 반복) |

### 핵심 비교 (Release)
- baseline HNF: 1 sign retry ≈ **~50초** (5분에 6+회)
- Cohen MLLL integration: 1 sign 시도 ≈ **5분+ 미완**
- **Cohen MLLL이 baseline 6배+ 느림**

---

## 3. Root Cause 분석

### 3.1 Cohen 정수 GSO 비용 구조
`mlll.c` 의 Cohen 2.6.7 표현:
- `d[k]` = det of Gram_{0..k-1} ≤ `‖a‖^(2k)`
- `Lambda[i][j]` = `d[j+1] · μ_{i,j}`
- swap_step의 exact division: `B = (d[m+1]·d[m-1] + λ²) / d[m]`
- size_reduce의 round division: `q = round(Lambda[m][l] / d[l+1])`
- compute_gs_single: `u = (d[i+1]·u − Lambda[idx][i]·Lambda[j][i]) / d[i]` 이중 루프

`d[k]` 가 `‖a‖^(2k)` 까지 (k=4면 `‖a‖^8`) 자라므로 매 `ibz_mul` 의 cost가
`O(IBZ_LIMBS²) = O(110²) = O(12100)` limb 곱셈. swap_step + size_reduce + GS 한 사이클당 수십 ~ 수백 회 곱셈.

### 3.2 Algorithm 2 시나리오 — quat_lattice_intersect의 inner add
`quat_lattice_intersect_mlll` 안의 `quat_lattice_add_mlll` 호출 path:
- 입력: 8 generators (4 from dual1 + 4 from dual2)
- dependent: 최대 4개 (rank 4 lattice이라 4 indep + 4 dep)
- 각 dependent 처리에 **swap + size_reduce + GS 재계산 (compute_gs_single from i=m-1 to beta-1)**
- final pass의 `while (changed && safety++ < SAFETY_MAX)` (=`1000·beta²`) 루프

### 3.3 추정 hot spot (프로파일러 없이)
1. **compute_gs_single 의 이중 루프** — j 루프 안에 i 루프, 각 step `ibz_mul + ibz_mul + ibz_sub + ibz_div_floor`. j=4, i=4면 16회 / GS 1회. swap 후 GS 재계산이 dependent 1개당 (beta − m + 1) 회 호출 → 한 swap에 4×16 = 64회 ibz_mul.
2. **swap_step 의 exact division** — i 루프 (m+1..beta-1)에 4 division/iter, dependent 처리 시 다수.
3. **final pass while 루프** — safety_max = 1000·64 = 64000 반복 가능. dependent 처리 후 GS 재계산이 매번 호출되면 폭증.

### 3.4 baseline (HNF) 와의 본질적 차이
HNF는 정수 행렬 elementary row op만 사용 — 매 step `ibz_mul / ibz_mod`. 행렬 entry는 modulus `m` 안 (Lemma 2 bound). Cohen GSO 의 d[k] 는 **modulus 밖** 자유롭게 자람 → ibz_mul cost 더 큼.

KLKL25 baseline은 HNF on `Z/mZ` 라서 모든 entry `< m^2`. 우리 Cohen GSO 는 `d[k] · u` 같은 항이 `‖a‖^(8) · ‖a‖² = ‖a‖^10` 까지도 일시적 발생 가능.

---

## 4. 최적화 후보 (학교 컴퓨터에서 시도 가능)

### 4.1 Lazy GS (단기)
- swap 시 전체 GS 재계산 대신 변경된 row만 update (Cohen 2.6.7 step 6 이미 변경된 i loop이 그것)
- **현재 mlll.c 의 swap_step 정상 path는 OK**. dependent path (`d[m+1]=0`) 만 `compute_gs_single from i=m-1 to beta-1` 전체 재계산
- 개선: dependent path 도 incremental update — Cohen 2.6.8 식 그대로

### 4.2 Final pass safety_max 축소 (단기)
- 현재 `1000 * beta * beta` = 64000 (beta=8). 너무 큼
- 정상 LLL 수렴 횟수는 `O(beta² · log B)` ≈ 수백
- 개선: `100 * beta * log_2(IBZ_BITS)` 정도

### 4.3 Profiling 우선 (중기)
- `gprof` 또는 `perf record` 로 hot spot 확정
- 추정 1순위: compute_gs_single 의 ibz_div_floor (exact division 분석 후 `ibz_divexact` 새 함수 도입 여지)

### 4.4 표현 변경 (장기, 페이퍼와 가까운 길)
- KLKL25 L2 model 그대로 — G integer + μ FLOAT (mpfr precision = ‖a‖²)
- mpfr는 GMP 의존 → KLKL25 GMP-free 깨짐
- 자체 fixed-bit float 구현 (mantissa = ‖a‖² bits, GMP-free) — 큰 작업

### 4.5 hot path 우회 (단기 우회책)
- sign.c:85 의 quat_lattice_intersect 만 MLLL 갈아끼우지 말고
- 더 작은 입력의 lattice ops (예: ideal.c:91 의 quat_lattice_add — 8 gen 단순)부터 시도
- ideal.c 호출처는 alg 인자 있으니 quat_lattice_add_mlll 직접 호출 가능

---

## 5. 학교 컴퓨터 작업 인계

### 5.1 git pull
```bash
git pull origin main
# main에 있는 commit:
#   81bd710 feat(mlll): port Pohst MLLL to Cohen integer GSO
#   908c93d feat(mlll): add quat_lattice_intersect_mlll wrapper
```

### 5.2 sign.c hot path 통합 시험
sign.c:85 한 줄 변경 (이 문서 §1 참조). 빌드 + 측정.

### 5.3 baseline 비교
sign.c HNF 그대로 + 같은 timeout 같은 seed. **반드시 ulimit -s unlimited**.

### 5.4 PAPER_ERRATA 와 본 문서 함께 참조
- `PAPER_ERRATA_2026-05-15_KO.md` — 페이퍼 자체 오류 (compact 4건 + KLKL25 3건)
- `PHASE2_TRIAL_2026-05-15_KO.md` — 본 문서 (Phase 2 trial 결과)
- `5월8일수정.md` — 사용자 본인의 baseline build 수정 기록

---

## 6. 결정 트리

```
Phase 2 hot path MLLL 통합 도전
├── (A) 짧은 길: §4.5 우회책 — ideal.c quat_lattice_add부터 (작은 입력)
├── (B) 중간: §4.1 + §4.2 + §4.3 (lazy GS + safety_max + profiling)
├── (C) 긴 길: §4.4 표현 변경 (mpfr 또는 자체 fixed-bit float)
└── (D) 보류: Cohen MLLL 본체만 git에 두고 (현재 상태) Phase 2는 다음 PR
```

권장 순서: D (현 상태 유지) → A (작은 입력으로 동치성 + 시간 측정) → B → C.

---

(끝. 학교 컴퓨터에서 진행 시 본 문서 §4.3 profiling 부터 추천)
