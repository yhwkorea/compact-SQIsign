# LVL1 ctest 결과 — FAIL (3/12)

작성: 2026-05-13 23:15 시작 / 2026-05-14 00:31 종료 (76분 22초)
환경: WSL2 / `/mnt/c/Users/htelr/projects/compact-SQIsign/build`
빌드: `cmake -DSQISIGN_BUILD_TYPE=broadwell -DCMAKE_BUILD_TYPE=Release ..` + `make -j` (exit 0)
실행: `ulimit -s unlimited && ctest --output-on-failure -R lvl1`

## 결과
| # | 테스트 | 결과 |
|---|--------|------|
| (9) | 기타 LVL1 unit/module tests | PASS |
| 23 | `sqisign_test_signature_lvl1` | **Timeout 1500s** |
| 24 | `sqisign_test_threadsafety_lvl1` | **Timeout 1500s** (sign hang 후속) |
| 29 | `sqisign_test_nistapi_lvl1` | PASS 275.14s |
| 32 | `sqisign_lvl1_KAT` | **Failed 23.46s** — `ERROR: pk is different from <../../KAT/PQCsignKAT_353_SQIsign_lvl1.rsp>` |
| 33 | `sqisign_lvl1_SELFTEST` | PASS 320.69s |

총: 9 PASS / 3 FAIL = 75%. 실제 ctest exit code 8.

## 의미 있는 신호
- **`sqisign_lvl1_SELFTEST` PASS (320s)** — keygen/sign/verify round-trip 자체는 동작
- **KAT FAIL** — pk 값이 KAT rsp와 불일치. precomp 변경이 deterministic으로 pk 영향
- **signature_lvl1 Timeout** — 별도 테스트의 sign 경로에서 hang (SELFTEST와 다른 입력/플로우)

## 원인 가설 (확정 아님)
1. **KAT 재생성 필요**
   - PR8 fix가 precomp constant 값을 바꿔서 deterministic 결정 흐름이 약간 다르게 흘러감
   - KAT (`PQCsignKAT_353_SQIsign_lvl1.rsp`)은 fix 적용 전에 만들어진 것이라 pk 비교 시 mismatch
   - 다만 spec 호환성 측면에서 KAT mismatch는 **PR8 fix가 잘못된 게 아니라 KAT가 stale** 일 수도 있음. 1저자/팀과 정합 확인 필요
2. **signature timeout — 별개 이슈**
   - SELFTEST는 random seed로 keygen+sign+verify가 돌아감 (320s) → 알고리즘 자체는 OK
   - signature_lvl1 테스트는 더 강한 입력 set 또는 KAT seed 사용 → 어느 edge case 입력에서 sign 내부 loop 무한 진행 가능성
   - PR8과 별개 이슈일 수도, 또는 precomp 값 변경이 특정 입력에서 사이클 길이를 비정상적으로 늘렸을 수도

## 다음 액션 (사용자 결정용)
1. `sqisign_lvl1_KAT`이 stale인지 vs PR8 fix가 wrong인지 — 1저자/팀 확인
2. `sqisign_test_signature_lvl1` 의 입력/seed 확인해서 어느 단계에서 hang하는지 instrumentation (printf or gdb attach)
3. PR8 fix를 LVL1만 적용해서 (LVL3/5 quaternion_data.c는 revert) signature/KAT 차이가 일관되는지 확인 — fix 정합성 검증

## 처리 상태
- **push 안 함** (사용자 지시: LVL1 e2e FAIL 시 push 보류)
- **벤치 측정 안 함** (LVL1 PASS 조건 미충족)
- **LVL3/LVL5 ctest 안 함** (LVL1 PASS 조건 미충족)
- working tree modified 상태 그대로 유지 — 다음 세션에서 결정 따라 진행

## 로그 파일
- `_ctest_lvl1_tail_2026-05-13.log` — 마지막 40여 줄 (전체 로그는 /tmp 휘발로 손실, ctest 진행 도중 WSL2 /tmp tmpfs cleanup 추정)
