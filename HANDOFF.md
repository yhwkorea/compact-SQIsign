# compact-SQIsign HANDOFF (2026-05-19 09:38 갱신)

## 2026-05-19 세션 결과: paper bound 통과 + sign+verify EXIT=0

상세 버그/결정 기록: `BUGS_AND_DECISIONS.md`.

핵심 fix 5개 (commit `71fca30`):
1. **`lattice.c quat_lattice_dual_without_hnf`**: paper LatticeDual Lines 7-8 GCD normalization 추가 (`quat_lattice_reduce_denom(dual, dual)` 한 줄). dual.basis **3598 → 62 bit**. 이게 결정적 fix.
2. **`mlll.c quat_lattice_add_mlll`**: `quat_mlll` (Cohen integer GSO) → `quat_ml2` (paper의 MLLL = NS09 ML2)로 교체.
3. **`ideal.c quat_lideal_inter`**: sign.c:81 chain도 `quat_lattice_intersect_mlll`로 wire (paper Issue 8 누락 완성).
4. **`normeq.c quat_sampling_random_ideal_O0_given_norm`**: paper Issue 5 mod N 본격 적용 (β 샘플 + γβ mod N).
5. **`ml2.c`**: oscillation detection 제거 + iter cap 65536 (paper Issue 13 답변 검증).

측정 (Lvl1 110 limbs, sign 2 round EXIT=0):
- `intersect_mlll` output: **255-320 bit** vs paper Line 14 bound 1626 bit ✓
- `sample_response` radius: 520 bit vs paper Line 24 bound 1757 bit ✓
- 이전 세션의 "paper-strict MLLL lvl1 unusable" 결론 **뒤집힘**.

다음 진입점 (`BUGS_AND_DECISIONS.md` "미해결" section 참조):
- debug fprintf trace 정리 (NDEBUG guard 또는 제거)
- IBZ_LIMBS_lvl1=28 (paper claim) 시도
- paper Issue 9, 11, 12 미적용 검토

---

## 2026-05-18 03:55 (이전 세션)

## 1. 즉시 시작
```bash
# 현재 진행 중인 측정 (b8ikbiot4) 결과 확인
wsl bash -c 'cat /tmp/sig37.exit; grep "BD-CHSEC\|BD-INTER\|MLLL-OSC\|PR8-BP " /tmp/sig37.err'
```
- BD-CHSEC.denom < 770bit + BD-INTER < 1626bit → **reduce_denom fix 성공, paper bound 안 들어감**
- BD-CHSEC.denom = 770bit 변화 없음 → reduce_denom 효과 없음 (basis와 denom coprime) → Path A 필요

## 2. 컨텍스트
- `~/.claude/projects/C--Users-htelr/memory/project_compact_sqisign.md` 최신 section
- `~/.claude/projects/C--Users-htelr/memory/verification_log.md` 2026-05-18 entries
- `REPORT_TO_FIRST_AUTHOR_2026-05-18.md` 1저자 보고 초안

## 3. 현재 코드 상태 (uncommitted)

### `intbig.h`: `IBZ_LIMBS_lvl1 = 110` (KLKL25 baseline 복원, reduce_denom fix 테스트 중)

### `lattice.c quat_lattice_intersect`: 끝에 `quat_lattice_reduce_denom(res, res)` 추가
- paper Algorithm 3 dual-add-dual chain의 denom 누적 약분 시도

### `mlll.c quat_lattice_intersect_mlll`: paper Algorithm 3 line 5-6 + 14 LLL wire + step trace
- line 11은 ml2-fallback (paper-strict는 cascade overflow로 lvl1 unusable)

### `sign.c`: hot path `quat_lattice_intersect_mlll` routing + BD-CHSEC/LATCM/INTER size trace

### `lat_ball.c`: BD-SB-ENTRY/BD-GRAM/PR8-BP/BALL-BOUND/RAND/GUARD trace

## 4. 진행 중 fix path

**Path B (reduce_denom)**: b8ikbiot4 측정 결과 대기 (5분)
- 효과 있으면 → BD-INTER paper bound 도달 가능성
- 효과 없으면 → Path A (새 ideal 정의) 필요, 한 session 범위 초과

## 5. 1저자 보고 초안 상태
`REPORT_TO_FIRST_AUTHOR_2026-05-18.md` 완성 — 다음 갱신 시점:
- b8ikbiot4 결과 따라 reduce_denom 효과 추가
- 또는 Path A 권장 (paper측 ideal 표현 명시 + 코드측 새 데이터 구조)

## 6. 측정 데이터 위치
- `/tmp/sig15.err` ~ `/tmp/sig37.err` (모든 측정 보존)
- `/tmp/sig18.err` paper-strict 30분 stuck
- `/tmp/sig19.err` swap oscillation 16.1M iter
- `/tmp/sig30~34.err` IBZ_LIMBS overflow 진단
- `/tmp/sig35.err` 400 limbs 144k iter cascade 미종료
- `/tmp/sig37.err` reduce_denom 측정 진행 중 (b8ikbiot4)

관련: [[reference-klkl25]], [[project-sqisign-fp-bench]]
