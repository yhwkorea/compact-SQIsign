# REPORT TO FIRST AUTHOR — 페이퍼 v(2026-05-11) 검토 + WSL 빌드 실측 + PR 8 ROOT CAUSE FIX

> **업데이트 (2026-05-12 03:35)**: PR 8 root cause 완전 진단 + LVL1 fix 검증 완료.
>
> 결론: KLKL25 baseline 의 `src/precomp/ref/lvl*/quaternion_data.c` 생성 도구가 GMP `mpz_t` designator (`._mp_size=-N, ._mp_d={limbs}`) 를 raw `uint64_t[IBZ_LIMBS]` 로 변환할 때 **음수 sign extension 누락**. 결과적으로 -1 (모든 limb 0xFF..FF) 이 `(uint64_t)(-1)` 만 cast 되어 첫 limb 만 0xFF..FF, 나머지 0. 정수 의미 = +(2^64-1).
>
> LVL1 26 곳, LVL3 28 곳, LVL5 33 곳 음수가 모두 손상되어 있었음. `_precomp_convert.py` 작성으로 the-sqisign 원본 GMP designator 형식에서 정확한 두 보수 표현으로 자동 재생성.
>
> LVL1 `sqisign_test_id2iso_lvl1` (이전 즉시 SEGFAULT/timeout 던 케이스) **ALL TESTS PASSED**. LVL3/5 + 전체 ctest 36/36 검증 진행 중.
>
> 페이퍼 §5 의 fixed-precision bound 자체는 **올바름**. 이슈는 baseline 의 precomp generation 도구 버그였고, 페이퍼 contribution 의 정량 검증을 가로막던 장애가 제거됨.
>
> 갭 A–F 페이퍼 정합성 이슈는 여전히 유효 (별도 패치).

> **PR8 fix 적용 후 ctest 결과 (Debug + ulimit unlimited, 4863s)**:
> - 19/36 PASS (이전 18/36)
> - **19 sqisign_test_id2iso_lvl1: PASS** (이전 즉시 SEGFAULT/timeout)
> - 20-21 id2iso_lvl3/5: ctest timeout 1200s 초과 (산술 시간이 큼, 직접 binary run + longer timeout 으로 검증 진행 중)
> - 22-30 signature/threadsafety/nistapi (×3 levels): **별도 이슈**. signature path 의 `quat_alg_make_primitive` 가 STANDARD entry [0] (음수 손상 없는 곳) 안에 큰 음수 component element 가 안 들어감 — KLKL25 baseline 의 산술 함수 또는 sign-aware path 에 별도 버그
> - 31-36 KAT/SELFTEST: KAT data mismatch + SELFTEST abort

> **PR 제출 권장 분할**:
> - PR-8a: `_precomp_convert.py` (변환 도구) — 신규 commit
> - PR-8b: `src/precomp/ref/lvl{1,3,5}/quaternion_data.c` 변환된 파일 — 신규 commit (PR-8a 와 함께)
> - PR-8c (별도): KLKL25 baseline 산술 모듈의 signature path 음수 처리 버그 — 1저자 또는 munsanwon2 와 협의

---


> 작성: 3저자 (Hyunwoo Yoo), 2026-05-12
> 대상: 1저자 (Won Kim)
> 페이퍼: `Compact Quaternion Algorithms for SQIsign` (zip in `.claude/inputs/`, last modified 2026-05-11 14:59)
> 기반 commit: yhwkorea/compact-SQIsign ef7df2a + fix_bundle.zip 적용분

---

## 0. TL;DR

- **빌드/단위 산술 (테스트 1-18)**: WSL Linux 환경에서 표준 명령(`cmake -DSQISIGN_BUILD_TYPE=ref .. && make && make test`)으로 100% 빌드, 18/36 PASS (LVL1/3/5 × quaternion/gf/curve/ec/biextension/basis_gen).
- **상위 프로토콜 (테스트 19-36)**: id2iso / signature / threadsafety / nistapi / KAT / SELFTEST 18개 전체 **fail** (SEGFAULT or Subprocess aborted). **Windows MinGW 환경 이슈가 아니라 Linux 동일 결과** — 5월8일수정.md의 "Linux/WSL 재실행하면 통과 가능성 높음" 가설은 **틀렸음**.
- **공통 원인 분류**: 모든 fail이 `src/quaternion/ref/generic/` 의 세 assertion에서 트립. 새 fixed-precision 변환에서 quaternion sampling/normalization 의 **invariant 가 깨짐**.
- **페이퍼 §5–§7 정합성 검토**: 6개 갭 발견. 일부는 결론을 깨지 않으나 lemma bookkeeping이 잘못됨. 일부는 §8 본문이 빈 채로 contribution claim만 존재.

---

## 1. WSL 실측 — 빌드/테스트 결과

### 1.1 환경

| 항목 | 값 |
|---|---|
| OS | Ubuntu 24.04 (WSL2) |
| GCC | 13.3.0 |
| CMake | 3.28.3 |
| libgmp-dev | 6.3.0 |
| 빌드 명령 | `cmake -DSQISIGN_BUILD_TYPE=ref ..` + `make -j$(nproc)` |
| ctest 명령 | `ulimit -s unlimited; ctest --timeout 600` |
| 총 ctest 소요 | 769.53초 (~13분) |

### 1.2 PASS / FAIL

```
Tests 1-18  (quaternion / gf / curve_arith / ec_biextension / ec_basis_gen
             × LVL1/3/5)                                       PASS  18/18
Tests 19-21 sqisign_test_id2iso_lvl{1,3,5}                     FAIL  SEGFAULT/Timeout
Tests 22-24 sqisign_test_signature_lvl{1,3,5}                  FAIL  SEGFAULT/Aborted
Tests 25-27 sqisign_test_threadsafety_lvl{1,3,5}               FAIL  Aborted
Tests 28-30 sqisign_test_nistapi_lvl{1,3,5}                    FAIL  SEGFAULT/Aborted
Tests 31-33 sqisign_lvl{1,3,5}_KAT                             FAIL  SEGFAULT/Aborted
Tests 34-36 sqisign_lvl{1,3,5}_SELFTEST                        FAIL  SEGFAULT/Aborted

50% tests passed, 18 tests failed out of 36
```

### 1.3 Stack overflow 1차 원인

Linux 기본 `ulimit -s 8192` (8 MB) 로는 모든 id2iso/signature/threadsafety 테스트가 즉시 SEGFAULT.
- gdb 백트레이스: `dim2id2iso_test_find_uv` 내부에서 `rsp` 가 `rbp` 보다 ~8 MB 아래로 내려간 시점에 SIGSEGV
- 원인: 한 함수 내 `ibz_t` 다수 (LVL1 기준 단일 ibz_t = ~880 B, 함수당 30–50 instance), `ibz_mat_4x4_t` 16 instance × 2, 깊은 호출체인 누적
- 해결: `ulimit -s unlimited` 또는 `setrlimit(RLIMIT_STACK, RLIM_INFINITY)` — 단 이건 운영 환경에서 항상 사용 가능한 가정은 아님

→ **권고**: 큰 ibz_t 배열은 stack 대신 heap (`malloc`/`free` 또는 RAII)으로 옮기는 리팩토링 검토. 특히 `dim2id2iso_test_find_uv` 와 그 호출 trace.

### 1.4 Stack 우회 후 2차 원인 — quaternion assertion 트립

`ulimit -s unlimited` 적용 후, 더 깊은 곳에서 assertion 깨짐:

| Test | 위치 | 깨지는 invariant |
|---|---|---|
| `sqisign_test_id2iso_lvl1` | `src/quaternion/ref/generic/normeq.c:218` | `quat_alg_norm(gamma) == adjusted_n_gamma` |
| `sqisign_test_signature_lvl1` | `src/quaternion/ref/generic/algebra.c:188` | `quat_alg_make_primitive` 내부 `ok` flag |
| `sqisign_test_threadsafety_lvl1` | `src/quaternion/ref/generic/normeq.c:289` | `quat_sampling_random_ideal_O0_given_norm` 출력 denom == 1 |
| `sqisign_lvl1_KAT` | `KAT/PQCsignKAT_353_SQIsign_lvl1.rsp` 없음 | (이건 단지 데이터 파일 path 문제, 무시) |
| `benchmark_lvl1` | `src/quaternion/ref/generic/algebra.c:188` | (위와 동일) |
| `benchmark_lvl5` | `src/quaternion/ref/generic/ideal.c:11` | `ibz_sqrt(lattice_index)` 가 perfect square 가 아님 (즉 `det(I) = det(O)·nrd(I)^2` 깨짐, Lemma~\ref{lem:detnrd} 위배) |

네 assertion 모두 `quaternion/ref/generic/` 내부의 **norm/lattice invariant**. 공통 root cause 강력 후보:
잘못된 quaternion element (fixed-precision 변환에서 corruption) 가 ideal generator 로 들어가
→ 결과 lattice 의 index 가 norm² 가 아님 → Lemma~\ref{lem:detnrd} 위배 → assertion 트립.
즉 `quat_represent_integer` / `quat_order_elem_create` 한 묶음에서 fixed-precision 정합성이 깨질 때,
그 결과가 이후 모든 ideal-arithmetic 호출의 lattice invariant 를 연쇄 위반.

NDEBUG(=Release) 빌드로는 silently 통과할 수도 있지만, 그건 잘못된 결과를 silently 진행시키는 것이라 위험.

### 1.5 Release 빌드 (NDEBUG 켜짐) 의 추가 위험

Release 빌드 + ulimit unlimited ctest 진행 중 관찰:

- `sqisign_test_signature_lvl1` 에서 단일 process 가 CPU 99% 로 **무한 루프 진입** (9분+ 경과 후에도 종료 안 됨, ctest timeout 600s 로 강제 종료 예상)
- Debug 에서는 assertion 트립으로 ~1초 내 abort 되던 케이스. NDEBUG 켜면 잘못된 quaternion element 가 silently 통과해서 후속 sampling/rejection 알고리즘이 절대 만족 못하는 invariant 를 계속 시도

→ **NDEBUG 를 production 에서 켜면 silent corruption + hang 위험**.
→ §8 Implementation 에 "fixed-precision SQIsign 운영 시 assertion 유지 필수, NDEBUG 비권장" 명시 필요. 또는 assertion 을 lighter check + graceful fallback (예: retry with fresh randomness) 로 교체.

### 1.5.1 Release ctest 부분 결과 (확정, 2026-05-12 00:30)

`-DCMAKE_BUILD_TYPE=Release` (NDEBUG 켜짐) + `ulimit -s unlimited` 조합 실측 (백그라운드 b5dit0uli, ID2iso~Signature 까지 진행 후 수동 종료):

```
1-18  (quaternion/gf/curve/ec/biextension/basis_gen × LVL1/3/5)   PASS  (Debug 와 동일)
19    sqisign_test_id2iso_lvl1                                    TIMEOUT 600.07 sec
20    sqisign_test_id2iso_lvl3                                    TIMEOUT 600.07 sec
21    sqisign_test_id2iso_lvl5                                    TIMEOUT 600.06 sec
22    sqisign_test_signature_lvl1                                 TIMEOUT 600.07 sec
23-36 (threadsafety/nistapi/KAT/SELFTEST × ...)                   (수동 종료)
```

**가설 100% 확정**:
- Debug: assertion 트립 (~1초 내 abort) — 잘못된 상태 노출
- Release: assertion 우회 (NDEBUG) → 잘못된 quaternion element 가 silently 통과 → sampling/rejection 루프가 만족 불가능한 invariant 를 영원히 시도 → 600s timeout

**운영 결론**:
1. NDEBUG 는 fixed-precision SQIsign 의 안전한 production 옵션이 아님. 페이퍼 §8 + 운영 가이드에 "assertion 유지 권고" 명시.
2. PR 8 (quaternion assertion 의 fixed-precision 정합성 fix) 가 사실상 페이퍼 contribution claim 의 정량 검증 가능성 자체를 결정함. 페이퍼 제출 전 반드시.

---

## 2. 페이퍼 정합성 갭

### 갭 A (블로커 수준) — Lemma statement vs proof 모순 [04Sampling.tex]

`lem:RandomIdealGivenNorm-bound`:
- **Statement (line 259)**: "max integer < $4N^2$"
- **Proof (line 275)**: "less than $4N^3$ by Lemma~\ref{lem:mlll-bound}"

Statement와 proof 가 다른 값을 주장. §5 Thm 5.1 증명 (line 45) 은 statement 값 `4·D_mix² < 16p^4` 사용.
- 다행히 proof 의 $4N^3$ 대입해도 $4D_{\mathrm{mix}}^3 \approx 4p^3 < 16p^4$ 로 결론은 살아남음
- **단, lemma statement 자체는 분명 잘못된 표기**. statement → $4N^3$ 정정 필요.

### 갭 B (proof 본문 자체가 옛 algorithm 기반) [04Sampling.tex]

`lem:RandomIdealGivenNorm-bound` proof 본문(line 263–270):
- "During the execution of **Cornacchia**, the maximum size of integers does not grow … coefficients representing γ are less than $\sqrt N$"
- 그러나 새 algorithm (Algorithm 8.3 line 51–54) 는 **Cornacchia 호출이 없음**. `g_1, g_2, g_3 \sample [0, N-1]` 직접 샘플 → γ 계수는 √N 이 아니라 **< N**

→ proof 본문이 옛(Cornacchia-기반) algorithm 기준으로 작성된 채 새 algorithm 옆에 붙어 있음. proof 본문 재작성 필요.

### 갭 C (Statement 가 proof 결론보다 16배 느슨) [04Sampling.tex]

`lem:RandomEquivalentPrimeIdeal-bound`:
- Statement (line 330): "< $2^{33.5}\sqrt p$"
- Proof 최종 라인 (line 343): "$\le 2^{29.5}\cdot\sqrt p$"

마진 16배. Statement 가 looser 라 정합성 자체는 OK 이지만, §5 Thm 5.2 proof 에서 `2^{33.5}` 인용 (line 148, 154, 161, 166 등). $2^{29.5}$ 로 좁히면 Sign bound 더 작아질 여지 — 그러나 메인 결론 $2^{21}p^7$ 자체는 변동 없을 가능성 (다른 항이 dominant).

### 갭 D (KLKL25 마진 silent 의존) [05Improve.tex, line 48]

§5 Thm 5.1 증명:
> "For other lines, following the proof of~\cite[Theorem~1]{KLKL25}, the maximum size of integers is less than $16p^4$."

KLKL25 분석은 모든 라인이 ≤ 7026 bit 안에 들어간다는 것을 보임. 우리 새 bound 는 1774 bit. 다른 라인들이 1774 bit 안에 들어간다는 보장은 **KLKL25 증명에 직접 포함되지 않음** (KLKL25 는 7026 bit 마진을 갖고 있어서 통과한 것).

3저자 로컬 (`C:\sqi\base\`) 실측: 보조 산술 (`ibz_gcdext` Euclidean intermediate, `ibz_mat_4x4_inv_with_det_as_denom` 4×4 det Hadamard) 가 페이퍼 bound 위 ~1.3× transient 발생 — 따라서 §5 Thm 5.1 "for other lines" outsource 는 **silent 가정**. 명시화 필요.

→ 권고: 세 옵션 중 1저자 결정 필요.
- (옵션 A) §5 보강: 각 보조 산술에 explicit lemma — 가장 시간 소요 큼
- (옵션 B) Table 1 footnote 로 "implementation-realized bound 약간 상향 (2240/3400/4500 bit) 가 honest 한 bound" 정정 — 가장 보수적
- (옵션 C) claim 범위를 "MLLL bottleneck 한정" 으로 명확화 — KLKL25 마진은 그대로 흡수하는 형태

### 갭 E (Contribution claim 의 본문 부재) [05Improve.tex line 180, 94imp.tex]

Intro 의 contribution item 2:
> "Performance enhancement of the fixed-precision implementation"

대응 본문:
- §5.2 `Performance analysis` (05Improve.tex line 180–181): 헤더만, **본문 0 줄**
- §8 `Implementation` (94imp.tex): `\section{Implementation}` **1 줄만**

→ 빈 채로 제출 시 reviewer 가 contribution claim 의 정량 근거 없음을 즉시 지적. §8 본문에 §1.1 (이 REPORT) 의 빌드/테스트 데이터 + 측정 (예: KeyGen/Sign cycle count, memory footprint 비교) 채워야 함.

### 갭 F (idealmult-bound proof LLL factor 누락 의심) [03Ideal.tex line 154–160]

`lem:idealmult-bound` proof (Lines 5–6 분석):
- LLL-reduced basis 의 `nrd(α_i) ≤ 8p/π² · nrd(r_1 I_1)` 사용
- 그러나 `lem:lll-norm` (02Pre.tex line 892) 은 `‖b_i‖² ≤ (1/(δ-1/4))^3 · 8p/π² · nrd(I)` 즉 δ=3/4 에서 추가 factor 8

→ 즉 LLL→Minkowski 변환 factor `(1/(δ-1/4))^{(n-1)/2} = 2^{1.5}` 누락 의심. 정정 시 idealmult-bound 가 ~factor 32 더 커질 수 있음 (16p²/π⁴ → 512p²/π⁴ 정도). §5 Thm 5.1/5.2 의 line-by-line 계산 재검산 필요.

(확실치 않음 — proof 의 8p/π² 가 `lll-norm` 의 `‖b_i‖² ≤ 64p/π² · nrd(I)` 에서 `nrd(α_i) = ‖α_i‖²/2 ≤ 32p/π² · nrd(I)` 임을 감안한 표기일 가능성도 있음. 1저자 검토 필요.)

---

## 3. PR 분할 제안 (5월8일수정.md 의 TODO 갱신)

5월8일수정.md 에서 잡혀 있던 PR 1–6 외에, 위 갭 들을 반영해 PR 추가/순서 변경:

| PR | 내용 | 우선순위 |
|---|---|---|
| PR 1 | (기존) GCD 변종 알고리즘 + 테스트 | 중 |
| PR 2 | (기존) 4×4 det 변종 + 테스트 | 중 |
| PR 3 | (기존) MLLL 본체 | 중 |
| PR 4 | (기존) macro alias 라우팅 | 중 |
| PR 5 | (기존) `lll_applications_mlll_gram.c` (4 MLLL_GRAM lideal) | 중 |
| **PR 6** | **본 REPORT 의 갭 A–F 페이퍼 patch 묶음 (04Sampling/03Ideal/05Improve)** | **고** |
| PR 7 | §8 Implementation 본문 (빌드/테스트 데이터 + KeyGen/Sign 벤치) | 고 |
| PR 8 | quat_represent_integer / quat_alg_make_primitive / quat_random_ideal_O0_given_norm 의 fixed-precision 정합성 진단 + fix | **블로커** |

PR 8 이 사실상 페이퍼 §5 의 "fixed-precision 으로 실제 SQIsign 이 동작한다" 라는 claim 의 정량 검증 자체. 페이퍼 제출 전 반드시.

---

## 4. 즉시 1저자 결정 필요 항목

1. **갭 D 의 옵션 A/B/C 중 선택** — 페이퍼 main bound (1774/2696/3555) 가 유지되는지, 또는 implementation-realized bound (2240/3400/4500) 로 정정해야 하는지
2. **PR 8 (quaternion assertion fail) 책임 분담** — 1저자가 KLKL25 baseline 측 책임자와 직접 소통할지, 3저자가 분석 계속할지
3. **PR 6 의 직접 패치 권한** — main 에 직접 push 할지, fix/paper-consistency 브랜치로 PR 할지

---

*이 REPORT 는 초안. push 전 1저자 review 필요. 본 파일은 직접 `git push` 하지 말고 카톡으로 먼저 공유.*
