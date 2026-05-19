# PR 8 핸드오프 디렉토리 — 2026-05-19 정리

PR 8 (precomp 음수 sign-extension)은 commit `0443506`/`30ca8e1`로 닫힘. 본 디렉토리는 그 작업의 잔존 자료 + paper-side 작업용 자료가 남아 있음.

## 남은 파일

| 파일 | 역할 | 상태 |
|---|---|---|
| `_precomp_convert.py` | the-sqisign GMP `mpz_t` designator → raw `uint64_t[IBZ_LIMBS]` 두 보수 변환 (PR 8 root fix 본체) | 사용 중 |
| `_precomp_resize.py` | `quaternion_data.c` lvl{1,3,5} limb 변환 도구 (README 28-limb verification에서 사용) | 사용 중 |
| `REPORT_TO_FIRST_AUTHOR_DRAFT.md` | 1저자 보고서 초안 — paper Gap A~F 분석 | **STALE (pre-5ff147b)**: Gap D / Gap H 옛 framing 무효화됨, 다시 써야 함 |
| `PAPER_PATCH_A_RandomIdealGivenNorm_bound.md` | Gap A 패치 (`4N²` vs `4N³`) | paper 측 작업 보류 |
| `PAPER_PATCH_E_section_5_2.md` | Gap E 패치 (§5.2 본문) | paper 측 작업 보류 |
| `PAPER_PATCH_E_Implementation_section.md` | Gap E 패치 (§8 Implementation) | paper 측 작업 보류 |

## 삭제된 일회성 로그

진단 시점의 일회성 산출물이라 작업 완료 후 정리:
- `_NIGHTLY_SUMMARY_2026-05-12.md` — 야간 자율 진단 15 iter 로그
- `_RESULTS_2026-05-14.md` — signature fix 시도 결과
- `_CTEST_FAIL_2026-05-13.md` — first-pass ctest fail report
- `_bench_compact_lvl1_2026-05-14.log` — bench timeout trace
- `_ctest_lvl1_tail_2026-05-13.log` — raw ctest tail

git 히스토리에서 필요시 `git show 30ca8e1:_PR8_handoff/...` 로 복원.

## paper-side 작업 (5ff147b 이후)

`REPORT_TO_FIRST_AUTHOR_DRAFT.md`는 PR 8 시점 (2026-05-13) 기준. 그 후 `71fca30`과 `5ff147b`가 Gap H / Gap D를 구현 측면에서 닫음. 보고서를 다시 쓸 때:
- Gap A/B/C/E/F는 여전히 paper 측 작업 (typo/citation/empty section) — 유효
- Gap D ("KLKL25 7026bit 마진 silent 의존")는 paper 측 framing은 유효하지만, **구현은 1774bit budget을 측정 검증**함 (`SIGN_FUNCTION_PLAN.md` 측정 테이블 인용). 옵션 B/C는 불필요.
- Gap H ("28-limb claim 위반")는 **무효화**. 동일 paper claim이 이번 commit으로 검증됨. 옛 framing 제거 필요.

루트 `HANDOFF.md` / `BUGS_AND_DECISIONS.md` / `PAPER_BOUND_PLAN.md`가 commit `5ff147b` 이후 상태의 단일 진입점. 본 INDEX 참조 우선순위는 후순위.
