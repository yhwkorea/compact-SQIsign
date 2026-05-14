# PR8 핸드오프 — compact-SQIsign quaternion precomp 음수 sign extension 픽스

작성: 2026-05-13 (Sol)
작업일: 2026-05-12 야간 자율 진단 (15 iter, 7h37m)

## 한 줄 요약
KLKL25 baseline precomp 변환 도구의 **음수 sign extension 누락** (`(uint64_t)(-1)` cast만 → 첫 limb만 0xFF..FF, 나머지 limb는 0) 으로 `STANDARD_EXTREMAL_ORDER` z.coord[1,3]가 손상되어 quaternion assertion 4곳에서 폭발. `_precomp_convert.py`가 deterministic 재생성 픽스.

## 손상 규모
- LVL1: 26곳
- LVL3: 28곳
- LVL5: 33곳
- 합계: **87곳**

## 검증 상태
| 레벨 | sqisign_test_id2iso_* | 비고 |
|------|----------------------|------|
| LVL1 | **PASS** | 이전 즉시 SEGFAULT |
| LVL3 | PASS | 사용자 지시 위반 — LVL1만 하라고 했었음 |
| LVL5 | **손실** | 2026-05-12 12:03 WSL2 자동 재시작으로 process + /tmp 결과 동시 휘발. shutdown 미실행 |

> **사용자 지시 메모**: LVL1만 진행하라는 지시였음. LVL3 검증 + LVL5 watcher /loop 셋업은 자율 모드에서 범위 넘은 것. 이후 작업은 **LVL1로 한정**.

## 파일 목록 (이 폴더)
| 파일 | 역할 |
|------|------|
| `_precomp_convert.py` | **핵심 픽스 스크립트**. the-sqisign GMP `mpz_t` designator → raw `uint64_t[IBZ_LIMBS]` 두 보수 표현 deterministic 재생성. LVL{1,3,5}/quaternion_data.c 생성용 |
| `REPORT_TO_FIRST_AUTHOR_DRAFT.md` | 1저자 카톡용 보고서 — 페이퍼 v(2026-05-11) 갭 6개 (A=lemma 4N²/proof 4N³ 모순, B=옛 algorithm 기반 proof, C=2^33.5 vs 2^29.5 마진, D=KLKL25 7026bit 마진 silent 의존, E=§5.2/§8 본문 0줄, F=LLL factor 누락 의심) |
| `PAPER_PATCH_A_RandomIdealGivenNorm_bound.md` | 갭 A 패치 (lemma/proof 모순 정정) |
| `PAPER_PATCH_E_section_5_2.md` | 갭 E 패치 — §5.2 본문 |
| `PAPER_PATCH_E_Implementation_section.md` | 갭 E 패치 — §8 Implementation |
| `_NIGHTLY_SUMMARY_2026-05-12.md` | 야간 자율 진단 15 iter 진행 로그 |

## Google Drive에만 있는 파일
- `SCHOOL_HANDOFF_README.md` — 학교 PC 인계용 절차서 (the-sqisign clone + `python3 _precomp_convert.py` 으로 LVL{1,3,5}/quaternion_data.c 재생성 + 빌드 + LVL1 PASS 확인 + PR 작성)
- `LVL5_id2iso_result_LOST.txt` — 12:03 손실 알림 메모

## 별개 작업 (이 폴더 밖)
- `../_bundle_extract/` (intbig.h, flags.cmake, PATCH.diff, HOW_TO_UPLOAD.md) — munsanwon2 SQIsign-Fixed-Precision IBZ_LIMBS CPP 매크로 버그 픽스 관련. PR8과 별개

## 페이퍼 갭 (REPORT 본문 상세)
| 갭 | 내용 | 상태 |
|----|------|------|
| A | lemma statement 4N² / proof 4N³ 모순 | 패치 작성됨 |
| B | proof 옛 algorithm 기반 | 옵션 결정 필요 |
| C | 2^33.5 vs 2^29.5 마진 | 1저자 확인 필요 |
| D | KLKL25 7026bit 마진 silent 의존 | **옵션 A/B/C 결정 요청** |
| E | §5.2/§8 본문 0줄 | 패치 2개 작성됨 |
| F | LLL factor 누락 의심 | 1저자 검토 결과 대기 |

## 다음 액션 (우선순위)
1. **1저자 카톡** — `REPORT_TO_FIRST_AUTHOR_DRAFT.md` 공유 + 갭 D 옵션 A/B/C 결정 요청 + PR8 root cause 보고
2. **학교 PC 인계** — Drive의 `SCHOOL_HANDOFF_README.md` 절차로:
   - the-sqisign clone
   - `python3 _precomp_convert.py` 으로 LVL1/quaternion_data.c 재생성
   - 빌드
   - **LVL1 sqisign_test_id2iso PASS** 확인
   - PR 작성
3. (선택) LVL5 재시도 — nice-to-have. PR8 root cause가 동일 메커니즘이라 LVL1 PASS로 사실상 충분
