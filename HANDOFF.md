# compact-SQIsign HANDOFF — 다음 세션 진입점

마지막 갱신: 2026-05-19 PM 2회차 (main HEAD `f4ef487`).

## 한 줄 상태

lvl1 28-limb sign+verify는 **seed별로 다름 — seed=1, 2, 5 PASS; seed=3, 4, 6 HANG**. paper §5.2 "1774 bit / 28 limbs" 주장은 **부분 검증**됨. 남은 hang은 `quat_lattice_mul` 안 HNF mod core의 silent overflow (~2000 bit transient > 1792 cap). 110-limb은 모든 seed 정상.

다음 세션 핵심 과제: 28-limb 안전한 `quat_lattice_mul` 본정공 — modular CRT 또는 paper-strict ML2 변형 (직접 ML2(d=16) 교체는 110-limb regression 일으킴, 검증된 사실).

---

## 0. 5분 onboard

```bash
# 1) clone + build (default 110-limb)
git clone https://github.com/yhwkorea/compact-SQIsign.git && cd compact-SQIsign
mkdir build && cd build
cmake -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) sqisign_test_signature_lvl1
ulimit -s unlimited
./src/signature/ref/lvl1/test/sqisign_test_signature_lvl1 --iterations=1 --seed=1
# expect: "All tests passed!" in ~2 min

# 2) 28-limb paper-claim verification (lvl1만)
cd ..
cp src/precomp/ref/lvl1/quaternion_data.c src/precomp/ref/lvl1/quaternion_data.c.bak110
python3 _PR8_handoff/_precomp_resize.py \
  src/precomp/ref/lvl1/quaternion_data.c.bak110 \
  src/precomp/ref/lvl1/quaternion_data.c 110 28
sed -i 's|#define IBZ_LIMBS_lvl1 110.*|#define IBZ_LIMBS_lvl1 28|' \
  src/quaternion/ref/generic/include/intbig.h
( cd build && make -j$(nproc) sqisign_test_signature_lvl1 )
./build/src/signature/ref/lvl1/test/sqisign_test_signature_lvl1 --iterations=1 --seed=1 2> /tmp/sig.stderr
# expect: PASS in ~33s; max bit trace shows ML2 #0 peak = 1520 < 1757 (paper bound)
grep peak_bit_seen /tmp/sig.stderr
```

WSL2 환경에서 HTTPS clone이 hang하면 `git -c http.version=HTTP/1.1 clone ...` 사용. push도 동일 (gh CLI v2.4.0은 `gh auth token` 미지원 — `~/.config/gh/hosts.yml`의 oauth_token 직접 추출).

---

## 1. 해결된 항목 (full timeline)

### PR 8 — precomp two's-complement sign-extension (commit `0443506`/`30ca8e1`, 2026-05-14)
KLKL25 precomp 생성기가 GMP `mpz_t` → raw `uint64_t[IBZ_LIMBS]` 변환에서 음수 sign extension 누락. `-1`이 `{0xFF...FF, 0, 0, ...}` (= $+(2^{64}-1)$)으로 저장됨. lvl1/3/5에서 26+28+33 = 87개 상수 손상.
- Fix: `_PR8_handoff/_precomp_convert.py` 가 two's complement 정정 재생성.
- 검증: `sqisign_test_id2iso_lvl1`/`lvl3` PASS.

### paper Issue 1~13 + LatticeDual GCD (commit `71fca30`, 2026-05-19 AM)
- **`lattice.c quat_lattice_dual_without_hnf`**: paper LatticeDual Lines 7-8 GCD normalization 추가. dual.basis 3598 → 62 bit. **결정적 fix.**
- **`mlll.c quat_lattice_add_mlll`**: Cohen integer GSO `quat_mlll` → paper MLLL = NS09 ML2 (`quat_ml2`).
- **`ideal.c quat_lideal_inter`**: sign.c:81 chain도 intersect_mlll 라우팅 (Issue 8 wire 완성).
- **`normeq.c quat_sampling_random_ideal_O0_given_norm`**: paper Issue 5 mod N (β 샘플 + γβ mod N).
- **`ml2.c`**: oscillation detection 제거 + iter cap 65536 (Issue 13 답변).

### paper Issue 8 extension + Issue 9 generalized + Issue 14 (commit `5ff147b`, 2026-05-19 PM) ← **이번 세션의 핵심**
- **`lattice.c quat_lattice_alg_elem_mul`**: 끝에 있던 `quat_lattice_hnf`를 `quat_ml2(d=4)` + `quat_lattice_reduce_denom`으로 교체. HNF의 cofactor expansion이 basis entries를 ~4×log2(K) bit로 키우던 게 R3/R5/R8 saturate의 공통 source였음. ML2 peak 2539 → 1520 bit.
- **`ideal.c quat_lideal_create_with_norm`**: known-N path를 위한 변형 (paper Issue 9). NDEBUG verify (sqrt(lattice_index)==N) 포함. 5 production caller 라우팅:
  - `normeq.c` 라인 378 (paper RandomIdealGivenPrimeNorm)
  - `id2iso.c` `id2iso_kernel_dlogs_to_ideal_even`
  - `sign.c` lideal_com_resp + lideal_resp_two
  - `dim2id2iso.c` quat_represent_integer post-construct
  - `encode_signature.c` sk decode
- **`algebra.c quat_alg_norm_mod` / `quat_alg_mul_mod`**: paper §RandomIdealGivenPrimeNorm modular arithmetic. 각 `ibz_mul` 즉시 mod N → 최대 transient 2·log2(N) (Lemma 4N²).
- **`normeq.c quat_alg_nrd_N2_divides`**: paper while-cond `gcd(nrd, N²) = N` 체크를 `nrd = N·Q + R` 분해로 수행. mod-N² 단계에서 full p·N² nrd 안 만듬.

### sample_response DPE bound (commit `f4ef487`, 2026-05-19 PM 2회차)
- **`lat_ball.c bound_parallelogram_dpe`** 추가. 기존 `quat_lattice_bound_parallelogram`이 호출하던 `ibz_mat_4x4_inv_with_det_as_denom` (gram inv via 4×4 cofactor)이 28-limb에서 silent overflow — det 2579 bit / adj 1935 bit (110-limb 실측 true 값) > 1792-bit cap.
- 대체: dpe (53-bit float + arbitrary exponent, ML2가 이미 사용) 기반 4×4 det + 3×3 (i,i)-cofactor 계산. LLL-of-dual step skip하고 U = identity (axis-aligned bound). LLL-reduced gram에서는 충분히 tight.
- **효과**: 28-limb seed=2 hang fix. seed=3, 4, 6은 다른 hot path에서 hang하므로 **이 fix만으로 해결 안 됨**.

### docs (commit `96a219c`, 2026-05-19)
SIGN_FUNCTION_PLAN.md / README.md / 본 HANDOFF.md 갱신. 측정 결과 반영.

---

## 2. 현재 28-limb seed 커버리지 (lvl1, seed=1..6 × 1 iter)

| seed | 110-limb | 28-limb | 비고 |
|------|----------|---------|------|
| 1    | PASS 126s | PASS 35s | |
| 2    | PASS 136s | PASS 47s | `f4ef487` DPE fix로 통과 |
| **3** | **HANG** | **HANG** | 110/28 무관 — 별개 (`quat_lideal_lideal_mul_reduced` 안 LLL/HNF 영역 의심) |
| **4** | PASS 142s | **HANG** | 28-limb regression. dim2id2iso → `quat_lideal_conjugate_without_hnf` → `quat_lideal_right_transporter` → `quat_lattice_mul` 안 `ibz_mat_4xn_hnf_mod_core`에서 2×hnfmod transient (~2000 bit) > 1792 cap silent overflow |
| 5    | PASS 140s | PASS 69s | |
| **6** | PASS | **HANG** | seed=4와 동일 패턴 |

3/6만 28-limb 통과. paper §5.2 "1774 bit / 28 limbs" 주장 **부분 검증**.

### seed=1 측정 peaks (참고, 통과 케이스)

| trace | peak bit | paper bound | 마진 |
|---|---|---|---|
| ML2 #0 (keygen) | **1520** | 1757 (Sign Line 24, 2²¹p⁷) | 237 |
| ML2 #1 (commit) | 1516 | 1757 | 241 |
| ML2 #2 (response) | 1411 | 1757 | 346 |
| ALGNORM | 648 | — (post-LLL) | — |
| IMLLL L14 | 324 | 1626 (Line 14, 2¹³⁸p⁶) | 1302 |
| sample_response radius | 522 | 1757 (Line 24) | 1235 |
| sampling (mod N path) | 512 | 1014 (Lemma 4N²) | 502 |
| LMUL post_mul | 250 | — | — |

28-limb cap 1792 bit 대비 **272 bit 마진** (이 seed에서). 다른 seed에선 `quat_lattice_mul` 안 hnfmod path에서 cap 위반.

---

## 3. 다음 작업 후보 (우선순위 순)

### 3.1 ★ ★ ★ `quat_lattice_mul` HNF mod core 28-limb safe replacement (top priority)

**문제**: lvl1 28-limb seed=4, 6 hang의 root cause. `quat_lideal_conjugate_without_hnf → quat_lideal_right_order → quat_lideal_right_transporter → quat_lattice_mul`에서:
```c
ibz_mat_4xn_hnf_mod_core(&(res->basis), 16, generators, &hnfmod);
```
hnfmod = `norm1² · norm2² · denom1⁴ · denom2⁴`. inv.denom이 ~125 bit (= lideal.denom·lideal.norm)이라 denom1⁴ = ~500 bit. hnfmod 총합 ~ 1000 bit. HNF mod core 내부 2·hnfmod transient ~ 2000 bit > **1792-bit cap → silent truncation**.

**측정**: `[D2I2I] before conjugate_without_hnf` → `[RT] before inv_lattice ... post inv_lattice inv.basis_max=63 denom=125` → `quat_lattice_mul` 호출 → **never returns**.

**시도 (실패)**: `ml2.c`에 `ML2_MAX_D = 16` bump + `quat_lattice_mul`의 HNF call을 `quat_ml2(d=16)`로 교체. 28-limb seed=1..6 모두 PASS시켰지만 **110-limb seed=5 regression (hang)**. ML2(d=16)이 paper-strict equivalent 아님 — 큰 d에서 dpe precision/iteration trade-off가 HNF와 다른 수렴 특성 보임.

**진짜 fix 후보**:
- **(a) Modular CRT** `ibz_mat_4xn_hnf_mod_core` 변형 — 각 modular 연산 < 64 bit, 28-limb 안전. CRT 누적 결과만 28-limb 안에 들어오게 설계. 100-200줄.
- **(b) Bareiss 또는 fraction-free 변형** — 4×4가 아닌 4×16 케이스에 적용. paper 본문엔 없는 알고리즘이라 fidelity 약함.
- **(c) Pre-reduce inv.denom 더 공격적으로** — `quat_lideal_inverse_lattice_without_hnf`에 reduce_denom 추가는 이미 시도했으나 효과 미미 (gcd가 1이라 줄지 않음). 다른 사전 reduce 시도해볼 수 있음.

작업 시작점: `src/quaternion/ref/generic/lattice.c:320` (HNF mod core 호출). 측정 도구는 lat_ball.c에 이미 박은 `[BOUND-DPE]` 패턴을 hnf_mod_core 입력/출력에 동일 적용.

### 3.2 seed=3 별개 hang (110/28 둘 다)

seed=3은 110-limb에서도 hang. 28-limb 작업과 독립. dim2id2iso의 lideal_lideal_mul_reduced 영역에서 멈춤 (`[LMUL] post_LLL` 후 진전 없음). 별도 디버그 패스 필요.

### 3.3 lvl3 / lvl5 paper-claim 검증

paper §5.2 budget: lvl3 2696 bit = **43 limbs**, lvl5 3555 bit = **56 limbs** (default 168/222).

```bash
# lvl3
python3 _PR8_handoff/_precomp_resize.py \
  src/precomp/ref/lvl3/quaternion_data.c src/precomp/ref/lvl3/quaternion_data.c 168 43
sed -i 's|#define IBZ_LIMBS_lvl3 168.*|#define IBZ_LIMBS_lvl3 43|' \
  src/quaternion/ref/generic/include/intbig.h
( cd build && make -j$(nproc) sqisign_test_signature_lvl3 )
./build/src/signature/ref/lvl3/test/sqisign_test_signature_lvl3 --iterations=1 --seed=1
```
주의: 3.1이 lvl1에서도 막혔으므로 lvl3/5는 더 큰 hnfmod 때문에 같은/더 큰 overflow path 있을 가능성 높음. 3.1 해결 후 시도하는 게 효율적.

### 3.4 ctest 풀 패스 (28-limb lvl1)
```bash
cd build && ctest --timeout 600 -R '_lvl1$'
```
현재는 seed=1만 sign_lvl1 검증. KAT + 단위 테스트 별도.

### 3.5 Benchmarks (paper Table 1 row)
`apps/benchmark_lvl{1,3,5}`. KLKL25 row는 `munsanwon2/SQIsign-Fixed-Precision`. 비교 조건: 동일 머신 / `taskset core 0` / `-DENABLE_STRICT=OFF`.

### 3.6 Trace fprintf NDEBUG-guard (★ low)
지금 stderr 트레이스 (`[ML2]`, `[ALGNORM]`, `[LMUL]`, `[BOUND-DPE]`, `[BALL]`, `[INV4x4]`, `[BOUND-P]`, `[D2I2I]`, `[RT]`, `[LIDEAL-MUL]`, `[PNRE]`, `[LIDEAL-GEN]`) 항상 발화. 측정 끝나면 `#ifdef BIT_TRACE` 같은 가드로 묶기.

### 3.7 Paper-side polish (★ optional, paper editor 작업)
- Gap A: `4N²` ↔ `4N³` 본문 typo
- Gap B: Cornacchia citation
- Gap C: $2^{33.5}\sqrt p$ vs $2^{29.5}\sqrt p$
- Gap E: §5.2 / §8 본문 보강
- Gap F: idealmult-bound 증명 $2^{1.5}$ 누락
- **Gap L (신규)**: `quat_lattice_sample_from_ball` 안 `bound_parallelogram`이 gram inv (4×4 cofactor) 호출. paper §5.2 Theorem에서 이 path의 transient (det/adj 1935-2579 bit)가 명시 분석 안 됨. 28-limb cap 위반 source — paper bound 자체 부족하거나 paper-측 algorithmic update 필요.
- Drive `REPORT_TO_FIRST_AUTHOR_DRAFT.md` / `PAPER_PATCH_A.md` / `PAPER_PATCH_E.md` — 모두 pre-`5ff147b` 분석. Gap H/D 옛 option A/B/C 무효화. 새 Gap L 추가 필요.

---

## 시도했지만 실패/철회된 접근 (이번 세션 lessons learned)

### ML2(d=16)으로 HNF mod core 교체 (REVERTED)
- 시도: `ML2_MAX_D 8 → 16`, `lattice.c:320`의 `ibz_mat_4xn_hnf_mod_core` 호출을 `quat_ml2(reduced, 4, generators, 16, alg)`로 교체.
- 28-limb 결과: seed=1..6 모두 PASS.
- **110-limb 결과: seed=5 hang (이전엔 140s PASS)** ← regression.
- 분석: ML2(d=16)는 paper-strict equivalent 아닌 듯. dpe precision + iteration 동작이 HNF와 다른 path 만들어 종종 무한 swap. 큰 d에서 더 두드러짐.
- 결론: ML2(d=16) 직접 교체는 보류. 다른 우회로 (3.1 (a)/(c)) 필요.

### `quat_lideal_inverse_lattice_without_hnf`에 reduce_denom 추가 (REVERTED, harmless였지만 효과 없음)
- 시도: `ibz_mul(inv->denom, ..., lideal->norm)` 직후 `quat_lattice_reduce_denom(inv, inv)` 추가.
- 결과: gcd(inv.basis entries, inv.denom) = 1이라 denom 안 줄음. seed=4 hang 변화 없음.
- 결론: 가설은 합리적이지만 데이터상 효과 없음.

---

## 4. 참고 docs

| 파일 | 내용 |
|---|---|
| `README.md` § "Paper-bound verification" | 빌드/실행/trace grep recipe |
| `SIGN_FUNCTION_PLAN.md` | sign 함수별 R-tag + paper Algorithm 매핑 + 측정 |
| `BUGS_AND_DECISIONS.md` | 71fca30 + 5ff147b 결정 기록 |
| `PAPER_BOUND_PLAN.md` | paper Issue 1~13 적용 history (P1~P5 모두 closed) |
| `PAPER_ERRATA_2026-05-15_KO.md` | paper-side Gap A~F 분석 |
| `_PR8_handoff/_precomp_resize.py` | lvl{1,3,5} limb 변환 도구 |
| `_PR8_handoff/_precomp_convert.py` | the-sqisign GMP → fixed-precision 변환 |
