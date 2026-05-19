# Paper source — "Compact Quaternion Algorithms for SQIsign"

이 디렉토리는 paper 원본 TeX입니다. 본 구현 (`5ff147b` 이후)의 측정 결과 +
코드 변경 근거가 paper 본문의 어느 라인/Algorithm/Lemma에 대응되는지 추적할
때 직접 인용하기 위해 in-tree로 둠.

## 빌드 (PDF)

```bash
cd paper
latexmk -pdf main.tex
# 또는
pdflatex main && bibtex main && pdflatex main && pdflatex main
```

## 본 구현이 참조하는 section / algorithm

| 코드 위치 | paper 본문 |
|---|---|
| `quat_alg_norm_mod`, `quat_alg_mul_mod`, `quat_alg_nrd_N2_divides` (algebra.c / normeq.c) | `04Sampling.tex` § RandomIdealGivenPrimeNorm (lines 27-31, Algorithm Alg 4) + `lem:nrd-mod` + `lem:RandomIdealGivenNorm-bound` |
| `quat_lattice_alg_elem_mul` (lattice.c) HNF → ML2(d=4) | `03Ideal.tex` § (paper "MLLL = ML2 (NS09 Fig 9)") |
| `quat_lattice_intersect_mlll` (lll/mlll.c) | `03Ideal.tex` Algorithm CompactLatticeIntersection + `91proof.tex` Algorithm LatticeDual + Lemma `lem:dual-bound` |
| `quat_lideal_lideal_mul_reduced` (lll/lll_applications.c) | `93idtoiso.tex` Algorithm SuitableIdeals + Algorithm IdealToIsogeny |
| `quat_lideal_create_with_norm` (ideal.c, paper Issue 9) | `93idtoiso.tex` proof body ("$J_t$ norm reuse from KLKL25") |
| Theorem 2 bound 측정 비교 (`SIGN_FUNCTION_PLAN.md` 표) | `05Improve.tex` Theorem "Improved Sign bound" (Lines 13/14/24) + Theorem "Improved KeyGen bound" |

## 라이선스 / 저작권

이 paper의 저자 (Won Kim, Changmin Lee, Hyunwoo Yoo) 동의 하에 source 공유. 외부 배포 / 출판 시 저자에게 확인 요망.

## 후속 paper 작업 (open)

- Gap A: `04Sampling.tex` line ~257 `4N²` ↔ proof `4N³` typo
- Gap B: 같은 proof body의 Cornacchia citation (현재 알고리즘은 direct sampling)
- Gap C: `04Sampling.tex` `lem:RandomEquivalentPrimeIdeal-bound` `2^{33.5}√p` vs proof `2^{29.5}√p`
- Gap E: `92exp.tex` / `94imp.tex` 본문 보강 (현재 거의 비어 있음)
- Gap F: `03Ideal.tex` `lem:idealmult-bound` 증명 $2^{1.5}$ 누락

Drafts: `_PR8_handoff/PAPER_PATCH_A_*.md`, `_PR8_handoff/PAPER_PATCH_E_*.md` (PR 8 시점, 일부 outdated). `_PR8_handoff/REPORT_TO_FIRST_AUTHOR_DRAFT.md`는 `5ff147b` 이후 다시 써야 함 (Gap D / Gap H 옛 framing 무효).
