# Nightly Summary — compact-SQIsign 검토 (2026-05-12)

> 진행: 2026-05-11 22:00 ~ 2026-05-12 (자율 모드 진행 중), Sol 자율 R&D

## ⭐ 핵심 결과 (사용자 깨움 시점에 보세요)

**PR 8 root cause 진단 + LVL1 fix 검증 완료**:
- KLKL25 baseline 의 `precompute_quaternion_data` 변환 도구 버그 — GMP `mpz_t` designator (`._mp_size=-1, ._mp_d={0x1}`) 를 raw `uint64_t[IBZ_LIMBS]` 로 변환 시 **음수 sign extension 누락**. -1 이 첫 limb 만 0xFF..FF, 나머지 0 → 정수 의미 +(2^64-1).
- LVL1 26곳, LVL3 28곳, LVL5 33곳 음수가 모두 손상.
- `_precomp_convert.py` 작성 → the-sqisign 원본 GMP designator 형식 → 정확한 7040/10752/14208-bit two's complement 으로 자동 재생성.
- **LVL1 `sqisign_test_id2iso_lvl1` PASS** (이전 즉시 SEGFAULT/timeout) — root cause fix 확정.
- **LVL3 `sqisign_test_id2iso_lvl3` 도 PASS** (07:01 확인, 84분 진행 후 "All tests passed!") — 다중 레벨 효과 검증.
- LVL5 id2iso: 직접 binary 진행 54분+ 째 (LVL3 의 산술 비율로 ~3시간 예상, 08:44 한계 안에 못 끝날 수도)

**ctest 진전**: 18/36 → **19/36 PASS** (+1, LVL1 id2iso 만 ctest 1200s timeout 안에 들어감). 직접 binary run 으로 **LVL1+LVL3 id2iso PASS** 추가 검증 완료.

**나머지 17 fail 은 PR8 외 별도 이슈** (signature/threadsafety/nistapi/KAT/SELFTEST):
- signature 의 `quat_alg_make_primitive` 가 STANDARD entry [0] (음수 손상 없는 곳) 안에 들어가야 할 큰 음수 element 가 안 들어감
- KLKL25 baseline 의 산술 모듈 또는 sign.c 의 mathematical mistake — PR8 fix 로 해결 안 됨, 1저자/munsanwon2 영역

**다음 권장 작업** (사용자 결정):
- (1) PR 작성: `_precomp_convert.py` + 변환 precomp + REPORT 묶음으로 1저자 review 요청
- (2) signature 별도 이슈 진단을 KLKL25 maintainer 협의 (PR8 fix 적용 후 잔여 이슈)
- (3) 또는 페이퍼 갭 A–F 패치 우선

---

## 산출물 (이 폴더 안)

| 파일 | 내용 | 용도 |
|---|---|---|
| `REPORT_TO_FIRST_AUTHOR_DRAFT.md` | 종합 보고서 (갭 A–F + WSL 실측 + 운영 권고) | 1저자(Won Kim) 카톡 공유 — review 후 push |
| `PAPER_PATCH_A_RandomIdealGivenNorm_bound.md` | 04Sampling.tex line 257–277 직접 패치 (statement 4N²→4N³, proof 재작성) | 갭 A+B 정정 |
| `PAPER_PATCH_E_Implementation_section.md` | 94imp.tex 본문 초안 (Build/Pass/Fail/Performance) | 갭 E 보완 |
| `_NIGHTLY_SUMMARY_2026-05-12.md` | 본 파일 | 야간 작업 인덱스 |
| `_bundle_extract/` | fix_bundle.zip 압축 해제 폴더 | 작업용, 정리 가능 |
| `_loop.sh`, `_gdb.sh`, `_classify.sh`, `_release_test.sh`, `_bench.sh` | WSL 측정 스크립트 | 작업용, 정리 가능 |

페이퍼 zip: `~/.claude/inputs/Compact_Quaternion_Algorithms_for_SQIsign.zip` (2026-05-11 14:59 작성, 사용자가 전달한 최종본)

## 검증된 사실 (정량)

### 빌드
- WSL Ubuntu 24.04 + GCC 13.3.0 + CMake 3.28.3 + libgmp-dev 6.3.0
- `cmake -DSQISIGN_BUILD_TYPE=ref .. && make -j$(nproc)`: 100% 성공 (warning 없음)
- fix_bundle.zip 의 두 파일 적용 후 (intbig.h + flags.cmake)

### ctest Debug (-DCMAKE_BUILD_TYPE 미지정, NDEBUG OFF)
- 1-18 PASS (산술 6모듈 × LVL1/3/5)
- 19-36 FAIL 18건, ulimit -s 8192 기본값에서 SEGFAULT 패턴
- ulimit -s unlimited 풀면 SEGFAULT 가 quaternion assertion fail 로 분류됨

### ctest Release (-DCMAKE_BUILD_TYPE=Release, NDEBUG ON) + ulimit -s unlimited
- 1-18 PASS (동일)
- 19-22 모두 600s Timeout (id2iso × 3 + signature_lvl1) — 즉 무한 루프
- 23-36 미진행 (수동 종료)
- 결론: NDEBUG 가 잘못된 quaternion element 를 silently 통과시켜 후속 sampling/rejection 무한 시도

### Benchmark (Debug, ulimit unlimited)
- `apps/benchmark_lvl1` → `algebra.c:188 quat_alg_make_primitive` 즉시 abort
- `apps/benchmark_lvl3` → 180s timeout
- `apps/benchmark_lvl5` → `ideal.c:11 quat_lideal_norm` 즉시 abort
- 결론: 페이퍼 §5.2 / §8 의 정량 데이터를 생성할 통로 자체가 막힘

## 발견된 페이퍼 정합성 갭 (6개)

| 갭 | 위치 | 유형 | 영향 |
|---|---|---|---|
| A | 04Sampling.tex L259 | Statement vs proof 모순 (4N² vs 4N³) | 결론 살아남지만 bookkeeping 잘못 |
| B | 04Sampling.tex L264–270 | Proof 본문이 옛 algorithm (Cornacchia 기반) 으로 작성 | proof 재작성 필요 |
| C | 04Sampling.tex L330 vs L343 | Statement(2^33.5) vs proof(2^29.5) 마진 16배 | 정합성 자체는 OK, 좁히면 §5 더 타이트 |
| D | 05Improve.tex L48 | "for other lines follow KLKL25" outsource silent 의존 | 1774 bit 안에 들어간다는 보장 명시화 필요 |
| E | 94imp.tex 1줄 + §5.2 비어 있음 | Contribution 2 ("Performance enhancement") 의 본문 부재 | §8 본문 작성 필요 (`PAPER_PATCH_E` 참조) |
| F | 03Ideal.tex L154–160 | idealmult-bound proof 가 LLL factor 누락 의심 | 1저자 검토 후 정정 |

## 새로 발견된 운영 권고

1. **NDEBUG 켜지 마라** — production 에서 fixed-precision SQIsign 운영 시 assertion 유지 필수. NDEBUG 켜면 silent corruption + 무한 루프
2. **ulimit -s unlimited 필요** — Linux 기본 8MB 로는 protocol 테스트 진입 자체가 안 됨. heap-based 리팩토링 고려
3. **`quat_represent_integer` 의 입력 사이즈** — unit test 는 작은 random M 으로 PASS, dim2id2iso 는 `n1*n2 ≈ p²` 로 FAIL. baseline 의 어떤 invariant 가 큰 입력에서 깨지는지 PR 8 진단 필요

## PR 분할 갱신 (5월8일수정.md TODO 보강)

| PR | 내용 | 우선순위 |
|---|---|---|
| PR 1 | (기존) GCD 변종 알고리즘 + 테스트 | 중 |
| PR 2 | (기존) 4×4 det 변종 + 테스트 | 중 |
| PR 3 | (기존) MLLL 본체 | 중 |
| PR 4 | (기존) macro alias 라우팅 | 중 |
| PR 5 | (기존) `lll_applications_mlll_gram.c` | 중 |
| PR 6 | 페이퍼 갭 A–F 정정 patch 묶음 | 고 |
| PR 7 | §8 Implementation 본문 (`PAPER_PATCH_E`) | 고 |
| **PR 8** | **quaternion assertion 의 fixed-precision 정합성 진단 + fix** | **블로커** |

PR 8 이 사실상 페이퍼 contribution 2 ("Performance enhancement") 의 정량 검증 가능성을 결정.

## 즉시 1저자 결정 필요 항목

1. **갭 D 의 옵션 A/B/C 선택**
   - A: §5 보강 (보조 산술 explicit lemma)
   - B: Table 1 footnote 로 implementation-realized bound 정정 (2240/3400/4500 bit)
   - C: claim 을 "MLLL bottleneck 한정" 으로 명확화
2. **PR 8 책임 분담** — KLKL25 baseline 측 (1저자 또는 KLKL25 저자 직접) vs 3저자 분석 계속
3. **PR 6 직접 패치 권한** — main 직접 vs fix/paper-consistency 브랜치

## 다음 세션 진입 시 체크리스트

- [ ] 1저자 카톡 — REPORT_TO_FIRST_AUTHOR_DRAFT.md 공유
- [ ] PR 8 진단을 위한 instrumentation 코드 (gdb watchpoint or printf) 추가
- [ ] Performance evaluation 측정 (PR 8 통과 후) — apps/benchmark_lvl{1,3,5} cycle count + max RSS
- [ ] §5 Thm 5.1 line 4/8/9/10 의 KLKL25 의존성 1저자 검토 결과 반영
- [ ] 갭 F (LLL factor) 1저자 검토 결과 반영

## 메모리 갱신 (이 세션에서 한 것)

- `~/.claude/projects/C--Users-htelr/memory/followups.md` 갱신 (compact-SQIsign 섹션 신설)
- `~/.claude/projects/C--Users-htelr/memory/verification_log.md` 3 건 append
  - 2026-05-12 ctest Debug 18/36 PARTIAL
  - 2026-05-12 페이퍼 v(2026-05-11) 정합성 FAIL (갭 6개)
  - 2026-05-12 Release ctest hang 가설 PASS (NDEBUG → 무한 루프)

---

## Iter 1 [00:50–01:00] — PR8 instrumentation 첫 dump

### 한 것
- `normeq.c` line 215-218 영역에 PR8 dump 코드 추가 (실패 직전 모든 ibz 값 printf)
- WSL 빌드 (incremental): 성공
- `sqisign_test_id2iso_lvl1` 실행 → 즉시 PR8 dump 트립

### 발견 (가장 중요)
실패 트리거가 **q=5, non_diag=1, standard_order=0** — 즉 표준 q=1 case 가 아니라 **비표준 extremal order** 분기. 이건 큰 단서.

수치 dump:
```
p (251 bit) = 2261564242916331941866620800950935700259179388000792266395655937654553313279
q = 5
n_gamma (357 bit) ≈ 2^357.4
adjusted_n_gamma (359 bit) = 4 * n_gamma  (non_diag 분기에서 4배)
Cornacchia 출력 (x, y, z, t):
  x bitsize ≈ 180,  y ≈ 178,  z ≈ 53,  t ≈ 49
  검증: x² + 5y² + p(z² + 5t²) = adjusted_n_gamma ✓ (line 198 통과)
gamma (assembled by quat_order_elem_create):
  coord[0] bitsize ≈ 302  (←x 와 비교해 ~120 bit 증가)
  coord[1] bitsize ≈ 366  (←y 와 비교해 ~188 bit 증가)
  coord[2] bitsize ≈ 234
  coord[3] bitsize ≈ 230
  denom = 정확히 2^124
quat_alg_norm 결과:
  numerator bitsize = 484
  denom = 1 (post-reduction)
```

### 분석
- Cornacchia 등식은 `x² + qy² + p(z² + qt²) = adjusted_n_gamma` 으로 정확
- 그러나 `quat_alg_norm(gamma)` = 2^484 ≠ adjusted_n_gamma = 2^359
- 차이 ~2^125 (≈ bitsize(denom))
- 가설:
  - (H1) 비표준 extremal order 의 `z`, `t` basis 원소가 "trd(z\bar t) = 0" (cross term zero) 가정을 만족 못함 → `nrd(gamma) = x² + y²nrd(z) + ...` 식이 깨짐
  - (H2) `z·t + t·z = 0` (anti-commute) 가정이 깨짐 → 같은 이유로 cross term 잔존
  - (H3) `quat_alg_mul` 이 큰 operand 에서 wrap/precision 오류 (less likely — Debug 빌드는 GMP 무한 정밀)
  - (H4) `quat_order_elem_create` 가 비표준 order 에 대해 잘못된 formula 사용

### 다음 iteration 계획
- 비표준 extremal order 의 `z`, `t` 값 dump (init 직후) — 가설 (H1), (H2) 검증
- `quat_order_elem_create` 안에 단계별 intermediate value print → 가설 (H4) 검증
- 또는 STANDARD_EXTREMAL_ORDER 초기화 코드 reading — z, t 의 expected math 확인


## Iter 2 [01:15–01:30] — quat_order_elem_create 단계 trace → root cause 핀포인트

### 한 것
- `quat_order_elem_create` 안에 7-step trace printf 추가 (`_pr8_trace_order_elem_create` flag)
- fail dump 직전에 flag 켜고 elem create 재호출해서 단계별 dump 받음
- WSL 빌드, `sqisign_test_id2iso_lvl1` 실행

### 핵심 발견 — order->z 의 값이 corrupt
```
order->q = 5
order->z.coord[0] = 0
order->z.coord[1] = 18446744073709551615  ← 2^64 - 1 = 0xFFFFFFFFFFFFFFFF
order->z.coord[2] = 0
order->z.coord[3] = 18446744073709551615  ← 같은 값
order->z.denom = 21267647932558653966460912964485513216 = 2^124
order->t.coord[2] = 1, order->t.denom = 1  (즉 t = j, OK)
```

### 수학 검증
- `q = 5` 이어야 함 (페이퍼/구현 가정). 즉 `nrd(z) = q = 5`
- 만약 z = (-i - k) / 2^124 이면 nrd(z) = (0 + 1 + p·0 + p·1) / 2^248 = (1+p)/2^248
- **p+1 = 2261564242916331941866620800950935700259179388000792266395655937654553313280**
- **5 · 2^248 = 2261564242916331941866620800950935700259179388000792266395655937654553313280** ← 정확히 일치!
- 즉 의도된 값은 z.coord[1] = -1, z.coord[3] = -1 → nrd(z) = (1+p)/2^248 = 5 = q ✓

### 그러나 실제 저장
- z.coord[1] = 2^64 - 1 (display 상으로). 가정: ibz_print 가 음수의 two's-complement limb 를 unsigned 로 print 한 것이 아니라, **실제로 그 unsigned 값으로 저장되어 있음** (quat_alg_mul 가 그렇게 취급)
- 그 가정 검증:
  - nrd(z) with z=((2^64-1)·i+(2^64-1)·k)/2^124 = (2^64-1)²·(1+p)/2^248 = (2^64-1)² · 5
  - ≈ 2^128 · 5 ≈ 2^130
  - y² · nrd(z) ≈ 2^356 · 2^130 = **2^486**
  - 실제 dump 의 computed norm bitsize = 487 ✓ **거의 정확 매치**!

### 결론
**이게 4 assertion 모두의 공통 root cause**:
- normeq.c:218 (norm 불일치) — 직접 결과
- algebra.c:188 (make_primitive ok) — corrupt gamma 가 order 안에 안 들어감
- normeq.c:289 (random_ideal_O0 denom!=1) — corrupt z 가 gen 계산에 영향
- ideal.c:11 (lideal_norm sqrt 실패) — corrupt z 로 만든 ideal lattice 의 index 가 perfect square 안 됨

→ 페이퍼 §5 의 fixed-precision bound 자체는 OK 일 수 있음. 문제는 **KLKL25 baseline 의 `STANDARD_EXTREMAL_ORDER` z init 코드 (특히 q≠1 인 비표준 case)가 음수 (-1) 를 잘못 저장**.

### 다음 iteration 계획
- `STANDARD_EXTREMAL_ORDER` 가 어디서 q=5, z=(-1)·i+(-1)·k 로 init 되는지 찾기
- LVL1 precomp 파일 (`src/precomp/ref/lvl1/quaternion_data.c`) 의 z 초기값 확인
- 또는 `quat_lattice_O0_set_extremal` 비표준 분기 코드 찾기
- 음수 ibz 초기화의 정확한 API 사용법 확인 (혹시 `ibz_set(&x, -1)` 를 직접 호출하지 못해서 다른 패턴으로 우회한 게 잘못된 거 아닌가)


## Iter 3 [01:45–02:15] — Precomp 파일 음수 손상 추적, root cause 완전 확정

### 한 것
- `EXTREMAL_ORDERS` 정의 (`src/precomp/ref/lvl1/quaternion_data.c`) 읽기
- `STANDARD_EXTREMAL_ORDER = EXTREMAL_ORDERS[0]` 확인 — 그러나 [0] 은 q=1 case. dump 의 q=5 는 alternate
- 7 entry 중 q 값: [0]=1, [1]=5, [2]=17, [3]=37, [4]=41, [5]=53, [6]=?
- **entry [1] (q=5) 의 z**:
  - line 99: `z.denom = {0x0, 0x1000000000000000}` = 0x1000000000000000 × 2^64 = **2^124** ✓
  - line 103: `z.coord[1] = {0xffffffffffffffff}` ← 첫 limb 만, 나머지 109 limb = 0
  - line 107: `z.coord[3] = {0xffffffffffffffff}` ← 동일
- `scripts/precomp/precompute_quaternion_data.sage` + `cformat.py` Ibz._literal 분석
  - Sage 스크립트는 **GMP mpz_t designator 형식** 출력 (`._mp_size = -num_limbs` 로 음수 표현)
  - 실제 precomp 파일은 **raw limb array 형식**
  - **변환 단계가 누구인지 미확인** — Sage 출력 ≠ 실제 파일. 별도 도구가 음수 처리 안 함

### Root cause 완전 확정
KLKL25 fixed-precision baseline (`munsanwon2/SQIsign-Fixed-Precision`) 에서:
- `ibz_t = uint64_t[IBZ_LIMBS]` 로 정의 (intbig.h:46), GMP-free
- 음수는 **7040-bit two's complement** 로 저장 (LVL1 110 limb 전부 0xFF...FF 가 -1)
- 그러나 precomp 파일은 첫 limb 만 0xFFFFFFFFFFFFFFFF, 나머지 0
- → -1 이어야 할 값이 **+(2^64 - 1)** 로 잘못 저장
- → quat_alg_mul 사용 시 huge positive value 처럼 동작
- → nrd(z) ≈ 5·(2^64-1)² ≈ 2^130 (의도 = 5)
- → quat_represent_integer 가 생성한 gamma 의 nrd ≠ adjusted_n_gamma
- → assertion fail (normeq.c:218 등 4곳)

### 다음 iteration 계획
PoC fix: entry [1] z.coord[1, 3] 을 designated initializer 로 모든 limb 채우기
```c
{[0 ... 109] = 0xffffffffffffffff}
```
빌드 + sqisign_test_id2iso_lvl1 실행 → assertion 통과/실패 검증.


## Iter 4 [02:16–02:50] — PoC fix #1 시도, 부분 성공

### 한 것
- precomp 파일 entry [1] z.coord[1, 3] 패치: `{0xffffffffffffffff}` → `{[0 ... 109] = 0xffffffffffffffff}` (designated initializer range)
- 빌드 + sqisign_test_id2iso_lvl1 실행

### 결과
**부분 성공!** 
- line 218 norm assertion **PASS** (이전 fail → 통과)
- 그러나 line 312 `assert(quat_lattice_contains(NULL, params->order->order, gamma))` 새로 fail
- 즉 한 단계 더 진행. z fix 가 root cause 의 일부였음 확정.

### 추가 시도
order basis 의 line 77, 93 `{0xffffffffffffffff}` 도 같은 패턴이라 fix 시도 — 효과 없음 (line 312 동일 fail). revert.

### 결론
- z.coord 패치만 효과적 → norm 계산은 fix 됨
- lattice_contains fail 은 별도 issue:
  - 가설 (A): gamma 가 fix 된 z 로는 mathematically 옳지만 order basis (stored) 가 그 z 와 inconsistent 한 다른 의도된 값
  - 가설 (B): `quat_lattice_contains` 자체가 fixed-precision 에서 wrap (다른 음수 처리 버그)
  - 가설 (C): order basis 의 다른 limb 도 음수인데 single-limb 형식으로 corrupt (line 81 의 `0xff80000000000000` 등)

### 다음 iteration 계획
- `quat_lattice_contains` 함수 reading + 결과 dump (lattice 좌표 계산해서 어느 component 가 non-integer 인지)
- 또는 KLKL25 baseline 의 다른 fork (`munsanwon2/SQIsign-Fixed-Precision`) 에 precomp 가 다르게 들어있는지 확인
- 또는 LVL3, LVL5 도 같은 패턴인지 grep — 보편적 문제인지 entry [1] 특이 문제인지


## Iter 5 [02:50–03:20] — the-sqisign 원본 precomp 비교, 변환 도구 버그 확정

### 한 것
- `C:\Users\htelr\the-sqisign\src\precomp\ref\lvl1\quaternion_data.c` 비교
- 원본은 **GMP `mpz_t` designator 형식**: `{{._mp_alloc=0, ._mp_size=-1, ._mp_d=(mp_limb_t[]){0x1}}}`
  - `._mp_size=-1` 은 sign + limb count
  - `._mp_d={0x1}` 은 abs value
  - 즉 -1 을 정확히 표현
- compact-SQIsign 은 `{0xffffffffffffffff}` (single limb)
- 두 패턴이 같은 값에 대응 (변환 도구가 `_mp_size=-N` 와 `_mp_d={small_abs}` 를 `(uint64_t)(-small_abs)` 로 cast 한 것)

### 변환 버그 확정
변환 도구 (compact-SQIsign 빌드 setup 단계에서 사용한 것으로 추정) 의 처리:
- 입력: `{._mp_size=-1, ._mp_d=(mp_limb_t[]){0x1}}`
- 변환: `(uint64_t)(-1) = 0xFFFFFFFFFFFFFFFF`
- 출력: `{0xffffffffffffffff}` (single limb 만)

**버그**: `ibz_t = uint64_t[IBZ_LIMBS]` (= 110 limb for LVL1) 에서 -1 의 정확한 two's complement 표현은 **모든 110 limb 가 0xFFFFFFFFFFFFFFFF**. 변환 도구가 첫 limb 만 채우고 나머지는 0 (implicit) 으로 둠 → 결과 값 = +(2^64-1).

### 추가 통계
- the-sqisign 의 entry [1] (q=5) 안 음수 (`_mp_size=-`) 갯수: **7곳**
- compact-SQIsign LVL1 의 single-limb `{0xff..ff}` 갯수: **3곳** (line 77, 93, 661, z 2곳 fix 후)
- z fix 전: **5곳** = z 2곳 + basis 의심 2곳 + entry 다른 곳 1곳 = 5곳
- 차이 7 - 5 = **2곳**: multi-limb 음수일 가능성 (예: `_mp_size = -2, _mp_d = {0x1, 0x2}` 같은 패턴이 다른 형식으로 변환)

### 다음 iteration 계획
**Systematic fix**: the-sqisign 원본을 읽어서 compact-SQIsign 형식으로 자동 변환하는 Python 스크립트 작성.
- 입력: GMP designator 형식 파일
- 출력: raw limb array 형식 + 음수는 IBZ_LIMBS 전체 two's complement
- LVL1/3/5 모두 적용 가능
- 이걸로 precomp 재생성 → 모든 음수 정확히 표현

이게 진짜 fix. 빌드/테스트 후 ctest 36/36 PASS 검증.


## Iter 6 [03:20–03:35] — 자동 변환 스크립트 작성 + LVL1 적용

### 한 것
- `_precomp_convert.py` 작성: the-sqisign GMP designator → compact raw limb array, 음수는 IBZ_LIMBS 만큼 two's complement fill
- LVL1 변환: **352 ibz values (148 양수, 26 음수, 178 zero)** — **26 곳 음수가 fix 대상**
- LVL3 변환: 402 ibz (173 양수, **28 음수**, 201 zero)
- LVL5 변환: 352 ibz (146 양수, **33 음수**, 173 zero)
- LVL1 의 변환된 precomp 를 compact-SQIsign 에 교체 + rebuild

### 현재 상태
- `sqisign_test_id2iso_lvl1` Debug build + ulimit unlimited 실행 중 (PID 348, 5+ 분 진행, CPU 99%)
- **이전 같은 위치에서 assertion fail 즉시 났던 것이 이제 진행 중** — 26 곳 음수 fix 가 가설대로 효과
- 단 진행 끝나는 데 시간 걸림 (110-limb 산술 + alternate orders 등 여러 case loop)
- LVL3, LVL5 는 아직 미적용 — LVL1 결과 확인 후 적용 예정

### 다음 iteration 계획
- LVL1 test 결과 확인 (PASS 또는 다른 fail 또는 timeout)
- PASS 면 LVL3, LVL5 적용 + ctest 36/36 전체 시도
- Fail 면 새 assertion 위치 분석


## Iter 7 [04:14–04:20] — ctest 진행 모니터링

### 한 것
- 백그라운드 ctest 상태 확인 (bof8qjwr1)
- 변환 결과 spot check

### 발견
- ctest 진행 중: 1-18 PASS (산술), 19 (id2iso_lvl1) PASS, **20 (id2iso_lvl3) 진행 17:48 째** (timeout 1200s 임박)
- 변환된 precomp 파일 size: 53579 bytes (이전 8228 의 6.5x) — 110-limb 음수 명시 때문, line 수 동일 712
- designated initializer (`[0 ... N] = 0xff..ff` for -1) 갯수: LVL1 5곳, LVL3 4곳, LVL5 3곳 (작은 abs value -1 만)
- 나머지 음수는 |value| > 1 이라 명시적 limb array (each 110 limb)

### PR8 fix 가 검증된 상태
이미 LVL1 sqisign_test_id2iso_lvl1 PASS 로 root cause 확정. 전체 36/36 검증은 시간 김 (~9시간 추정).

### 다음 iteration 계획
- ctest 결과 확인 (timeout 또는 ctest 자체 진행)
- LVL3, LVL5 id2iso 만 sample 검증 (signature/KAT/SELFTEST 는 너무 오래)
- 사용자 깨우면 PR 작업 권한 요청


## Iter 8 [04:43–05:08] — ctest 결과 + 추가 진단

### ctest 결과 (b0f8qjwr1, 4863s 동안 진행)
**19/36 PASS (이전 18/36 → +1)**:
- 1-18 (산술) PASS (변동 없음)
- **19 id2iso_lvl1 PASS** ← PR8 fix 효과 확정
- 20 id2iso_lvl3, 21 id2iso_lvl5: Timeout (1200s, 산술 시간이 LVL3/5 에서 너무 큼 — 추가 시간 주면 PASS 가능성)
- 22-36: signature (×3) + threadsafety (×3) + nistapi (×3) + KAT (×3) + SELFTEST (×3) 모두 fail
  - 대부분 "Subprocess aborted" (assertion fail)
  - 31 KAT_lvl1, 33 KAT_lvl3: "Failed" (data mismatch — assertion 과 다른 종류)

### signature_lvl1 직접 진단
- `algebra.c:188 quat_alg_make_primitive 'ok' assertion` 여전히 fail
- 인스트루멘테이션 dump 결과:
  - x 는 큰 음수 component (coord[0,2] 음수, ~10^150 abs value, bitsize ~500)
  - x 가 들어가야 할 order = STANDARD_EXTREMAL_ORDER (q=1, basis `[(2,0,0,1), (0,2,1,0), (0,0,1,0), (0,0,0,1)]/2`) — **음수 손상 없는 entry [0]**
  - 즉 x 가 STANDARD order 안에 안 들어감
- compute_response_quat_element 직후 호출

### 결론
PR8 fix 가 id2iso 만 영향. signature path 는 **별도의 이슈**:
- (a) signature 의 quaternion 산술 자체 버그 (`ibz_sub`, `ibz_neg`, 또는 다른 음수 처리 함수 가 fixed-precision 에서 잘못 동작)
- (b) 또는 signature path 의 mathematical mistake (다른 order 에서 만든 element 를 STANDARD 안에 있는지 검사)
- (c) KAT data mismatch 는 또 다른 종류 (KAT 파일 자체가 stale 또는 wrong)

→ KLKL25 baseline maintainer (1저자 또는 munsanwon2) 영역. PR8 한계.

### 다음 iteration 계획
- 사용자 깨움 시 종합 보고 (PR8 root cause 진단 완료, 부분 fix 검증, 나머지 별도 이슈)
- 시간 남으면 id2iso_lvl3/5 longer timeout 재시도 (PR8 fix 효과 확정 위해)
- 또는 LVL3/5 id2iso 의 direct binary run 으로 timeout 한도 넘어서까지 진행


## Iter 12 [07:01] — LVL3 id2iso ALL TESTS PASSED 🎉

### 결과
- LVL3 sqisign_test_id2iso_lvl3 직접 binary run, 약 84분 (5:37 시작 ~ 7:01 출력 확인)
- 출력: "Running id2iso_clapotis tests / All tests passed!"
- 즉 LVL3 도 PR8 fix 후 통과

### LVL5 진행 중
- 54:14 째, 진행 중. 출력 silent.
- LVL3 의 산술 비율로 추정 ~3시간 (LVL5 IBZ_LIMBS=222 vs LVL3=168, ~2.3x 산술)
- 08:44 한계 안에 못 끝날 가능성 — 사용자가 직접 결과 확인 가능

### REPORT 업데이트
- NIGHTLY_SUMMARY 헤더 + REPORT_TO_FIRST_AUTHOR_DRAFT.md 모두 LVL3 PASS 반영


## Iter 13 [07:28] — LVL5 진행 + git diff summary

### LVL5 진행
- 1:21:23 째 (시작 06:07), 진행 중. 출력 silent.
- LVL3 가 84분 PASS 였고 LVL5 IBZ_LIMBS 비율 2.3x → ~3.3시간 예상. 08:44 한계 안에 못 끝날 듯.
- 사용자가 깨어나서 직접 결과 확인 가능 (PID 592, output `/tmp/lvl5_id.txt`)

### git diff summary
```
 .cmake/flags.cmake                          |   6 +        (fix_bundle.zip 변경)
 src/precomp/ref/lvl1/quaternion_data.c      |  52 +-       (PR8: 26 음수 fix)
 src/precomp/ref/lvl3/quaternion_data.c      | 582 +-       (PR8: 28 음수 fix)
 src/precomp/ref/lvl5/quaternion_data.c      | 902 +-       (PR8: 33 음수 fix)
 src/quaternion/ref/generic/include/intbig.h |  35 +-       (fix_bundle.zip 변경)
 5 files changed, 149 insertions(+), 1428 deletions(-)
```
(precomp 의 -1428/+149 는 GMP designator 형식 (3개 #elif 블록 × 3개 ibz_t per 값) → raw limb 형식 변환)

### Commit 권장 분할
- **Commit 1**: `fix_bundle.zip` 의 두 파일 (`.cmake/flags.cmake`, `src/quaternion/ref/generic/include/intbig.h`) — 이미 5월8일수정.md 의 계획. message: `fix: build errors with standard cmake/make commands` (HOW_TO_UPLOAD.md 참조)
- **Commit 2**: PR8 root cause fix — `_precomp_convert.py` (신규) + `src/precomp/ref/lvl{1,3,5}/quaternion_data.c` (변환). message: `fix(precomp): correct two's-complement representation of negative ibz values`


## Iter 15 [08:21] — 야간 종료

### LVL5 최종 상황
- 2:14:09 째 진행 중. 출력 silent.
- 08:44 한계 도달 임박. ScheduleWakeup 종료.
- LVL5 binary 는 background 에서 계속 진행 (PID 592). 사용자가 깨어나서 결과 직접 확인 가능:
  ```bash
  wsl -d Ubuntu -- ps -ef | grep sqisign  # 진행 상태
  wsl -d Ubuntu -- cat /tmp/lvl5_id.txt   # 출력
  ```

### 야간 자율 R&D 최종 결과 (15 iter, 7시간 37분)

**확정**:
- PR 8 root cause 진단: KLKL25 baseline precomp 변환 도구의 음수 sign extension 버그
- LVL1 26곳 + LVL3 28곳 + LVL5 33곳 = **총 87곳 음수 fix**
- LVL1 `sqisign_test_id2iso_lvl1` PASS (이전 즉시 SEGFAULT/timeout)
- LVL3 `sqisign_test_id2iso_lvl3` PASS (84분 진행 후)
- LVL5 진행 중

**부분**:
- ctest 19/36 PASS (이전 18/36 → +1, ctest timeout 1200s 에 들어가는 케이스만)
- 직접 binary 로는 LVL1 + LVL3 id2iso 확정 PASS

**별도 이슈 (PR8 외)**:
- signature/threadsafety/nistapi/KAT/SELFTEST × 3 levels 17 fail
- signature 의 `quat_alg_make_primitive` 가 STANDARD entry [0] (음수 손상 없는 곳) 안에 큰 음수 element 가 안 들어감
- KLKL25 baseline 산술 모듈 또는 sign.c 의 별도 mathematical/precision 버그
- PR8 fix 와 무관, 1저자/munsanwon2 영역

### 사용자 깨움 시 권장 다음 작업

1. LVL5 결과 확인 (background process 결과)
2. PR 작성 (사용자 권한):
   - **PR 1**: fix_bundle.zip 의 두 파일 (`.cmake/flags.cmake`, `intbig.h`) — 5월8일수정.md 의 계획대로
   - **PR 2** (PR8): `_precomp_convert.py` 신규 + `src/precomp/ref/lvl{1,3,5}/quaternion_data.c` 변환 + REPORT
3. 1저자 카톡: REPORT_TO_FIRST_AUTHOR_DRAFT.md 공유 + signature 별도 이슈 협의
4. 페이퍼 갭 A–F (선택): PAPER_PATCH_A/E 적용 또는 1저자 검토 요청

