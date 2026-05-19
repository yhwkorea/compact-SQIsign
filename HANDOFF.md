# compact-SQIsign HANDOFF — 다음 세션 진입점

마지막 갱신: 2026-05-19 (commit `96a219c`).

## 한 줄 상태

**lvl1 28-limb sign+verify PASS** (33s @ seed=1). paper §5.2 "1774 bit / 28 limbs" 주장 측정 검증 완료. 다음은 lvl3/lvl5 동일 검증과 ctest 풀 패스.

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

### docs (commit `96a219c`, 2026-05-19)
SIGN_FUNCTION_PLAN.md / README.md / 본 HANDOFF.md 갱신. 측정 결과 반영.

---

## 2. 최종 측정 (lvl1, IBZ_LIMBS=28, seed=1)

| trace | peak bit | paper bound | 마진 |
|---|---|---|---|
| ML2 #0 (keygen) | **1520** | 1757 (Sign Line 24, 2²¹p⁷) | 237 |
| ML2 #1 (commit) | 1516 | 1757 | 241 |
| ML2 #2 (response) | 1411 | 1757 | 346 |
| ALGNORM (R5 영역) | 648 | — (post-LLL) | — |
| IMLLL L14 (R3 영역) | 324 | 1626 (Line 14, 2¹³⁸p⁶) | 1302 |
| sample_response radius | 522 | 1757 (Line 24) | 1235 |
| sampling (mod N path) | 512 | 1014 (Lemma 4N²) | 502 |
| LMUL post_mul | 250 | — | — |

28-limb cap 1792 bit 대비 **272 bit 마진**. 모든 paper Theorem 라인 만족.

---

## 3. 다음 작업 후보 (우선순위 순)

### 3.1 lvl3 / lvl5 paper-claim 검증 (★ next)
paper §5.2 budget: lvl3 2696 bit = **43 limbs**, lvl5 3555 bit = **56 limbs** (default 168/222). lvl1 28-limb 검증과 같은 방식:

```bash
# lvl3
python3 _PR8_handoff/_precomp_resize.py \
  src/precomp/ref/lvl3/quaternion_data.c src/precomp/ref/lvl3/quaternion_data.c 168 43
sed -i 's|#define IBZ_LIMBS_lvl3 168.*|#define IBZ_LIMBS_lvl3 43|' \
  src/quaternion/ref/generic/include/intbig.h
( cd build && make -j$(nproc) sqisign_test_signature_lvl3 )
./build/src/signature/ref/lvl3/test/sqisign_test_signature_lvl3 --iterations=1 --seed=1
```
- 통과 시 paper 모든 NIST level 검증 완료.
- 실패 시 ML2/IMLLL/LMUL trace로 saturate 지점 찾고 동일 패턴 fix.

### 3.2 ctest 풀 패스 at 28-limb lvl1
현재는 `sqisign_test_signature_lvl1` 단일 시드만 검증. `ctest`의 KAT 테스트, 다른 단위 테스트 통과 여부 미확인.

```bash
cd build && ctest --timeout 600 -R '_lvl1$'  # at 28-limb config
```

### 3.3 Benchmarks (paper Table 1 row)
`apps/benchmark_lvl{1,3,5}`. KLKL25 row는 `munsanwon2/SQIsign-Fixed-Precision`에서. 비교는 동일 머신 / `taskset core 0` / `-DENABLE_STRICT=OFF`.

### 3.4 Trace fprintf NDEBUG-guard (★ low)
지금 stderr 트레이스 (`[ML2]`, `[ALGNORM]`, `[LMUL]`, ...)가 항상 발화. 측정 끝나면 `#ifdef BIT_TRACE` 같은 가드로 묶기. 다음 메이저 사용자 face 전에.

### 3.5 Paper-side polish (★ optional, paper editor 작업)
- Gap A: `4N²` ↔ `4N³` 본문 typo
- Gap B: Cornacchia citation
- Gap C: $2^{33.5}\sqrt p$ vs $2^{29.5}\sqrt p$
- Gap E: §5.2 / §8 본문 보강
- Gap F: idealmult-bound 증명 $2^{1.5}$ 누락
- Drive `REPORT_TO_FIRST_AUTHOR_DRAFT.md` / `PAPER_PATCH_A.md` / `PAPER_PATCH_E.md` — 모두 pre-`5ff147b` 분석. 다시 쓸 필요 있음 (Gap H/D 옛 option A/B/C 무효화).

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
