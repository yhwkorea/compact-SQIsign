# Contributing — improvement + push workflow

이 fork에서 변경 작업을 하고 GitHub `main`에 푸시하는 표준 절차.

## 1. 작업 시작

```bash
# Linux/WSL
git clone https://github.com/yhwkorea/compact-SQIsign.git
cd compact-SQIsign
```

WSL2에서 `git clone`이 hang하면 (HTTP/2 stream not closed 등):
```bash
git -c http.version=HTTP/1.1 clone https://github.com/yhwkorea/compact-SQIsign.git
```

`gh` CLI v2.4.0 (Ubuntu 22.04 jammy package)는 `gh auth token` subcommand 없음. 토큰 직접 꺼내려면:
```bash
TOKEN=$(awk '/oauth_token:/{print $2}' ~/.config/gh/hosts.yml)
```

## 2. 빌드 + 테스트

`README.md` § "Build" / "Test" / "Paper-bound verification" 그대로.

변경 종류별 권장 sanity:

| 변경 범위 | 최소 테스트 |
|---|---|
| 단일 함수 수정 | `make -j$(nproc) sqisign_test_signature_lvl1 && ulimit -s unlimited && ./build/.../sqisign_test_signature_lvl1 --iterations=1 --seed=1` |
| paper bound 영향 | 위 + 28-limb verification (README 섹션 그대로) + trace grep (`grep peak_bit_seen /tmp/sig.stderr`) |
| 빌드 시스템 / CMake | `ctest --timeout 600` 풀 패스 |
| 새 fix 추가 | `BUGS_AND_DECISIONS.md`에 결정 항목 한 줄 추가 |

## 3. 측정 데이터로 fix 검증

bound 관련 fix는 측정 비교가 필수. 패턴:

```bash
# fix 적용 전
./build/.../sqisign_test_signature_lvl1 --iterations=1 --seed=1 2> /tmp/before.err
# fix 적용 후
make -j$(nproc) && ./build/.../sqisign_test_signature_lvl1 --iterations=1 --seed=1 2> /tmp/after.err

# peak 비교
for f in before after; do
  echo "=== $f ==="
  grep -E 'peak_bit_seen|\[ALGNORM\]|\[IMLLL\] L14 done|\[LATTICE-ADD\]' /tmp/$f.err \
    | sed -E 's/.*=([0-9]+).*/\1/' | sort -n | tail -3
done
```

paper bound 초과 (1757 bit at lvl1, 2666 bit at lvl3, 3505 bit at lvl5) 발견되면 해당 hot path가 새 fix 대상.

## 4. 커밋 메시지 컨벤션

```
type(scope): one-line summary

여러 줄 본문:
- 무엇을 바꿨는가 (코드 측면)
- 왜 바꿨는가 (paper Issue / Gap / 측정 결과)
- 측정 비교 (before → after bit, 시간, EXIT 코드)
- 어디 파일 어떤 함수
- caveat / 후속 작업

Co-Authored-By: ...
```

`type`은 conventional commits 따름: `feat`, `fix`, `perf`, `docs`, `chore`, `refactor`, `test`.
`scope`은 영향 범위: `paper`, `bound`, `mlll`, `ml2`, `sampling`, `id2iso`, `sign`, `verify`, `precomp`.

이번 fork의 좋은 예: commit `5ff147b` (paper Issue 8 ext + Issue 9 generalized + Issue 14, 측정 테이블 포함).

## 5. 푸시

```bash
git -c http.version=HTTP/1.1 push origin main
```

또는 remote URL에 토큰 박아두기:
```bash
git remote set-url origin "https://yhwkorea:${TOKEN}@github.com/yhwkorea/compact-SQIsign.git"
git -c http.version=HTTP/1.1 push origin main
```

## 6. 문서 갱신

코드 변경과 함께 다음 docs도 같이 갱신 (다음 세션이 GitHub만 보고 이어갈 수 있도록):

| 변경 종류 | 같이 갱신할 파일 |
|---|---|
| Paper bound에 영향 | `HANDOFF.md` 측정 테이블, `SIGN_FUNCTION_PLAN.md` R-tag 행 |
| 새 paper Issue 적용 | `PAPER_BOUND_PLAN.md` 표, `BUGS_AND_DECISIONS.md` 결정 추가 |
| 빌드/실행법 변경 | `README.md` § "Paper-bound verification" |
| 새 stale 항목 발견 | `_PR8_handoff/INDEX.md` |
| paper-side 신규 발견 | `PAPER_ERRATA_2026-05-15_KO.md` 또는 Drive `REPORT_TO_FIRST_AUTHOR_DRAFT.md` |

작은 doc-only 변경은 `docs:` 타입으로 별도 commit (코드 commit과 분리).

## 7. fix 추가 패턴 (paper Issue 적용 시)

이번 fork의 패턴:

1. **paper 본문 인용**: 어느 §, 어느 algorithm line, 어느 lemma인지 commit message에 인용
2. **측정으로 증명**: before/after peak bit 동일 시드로 비교
3. **trace 라인 추가**: 새 hot path는 한 줄 fprintf로 bit 사이즈 노출 (`[NEW-TAG] field=%d`)
4. **공통 source 의심**: 한 fix가 여러 R-tag를 동시 해결하면 (R3/R5/R8처럼) 표면 증상이 아니라 공통 source임. 메모리에 ref 남기기
5. **NDEBUG verify**: 우회 fix는 NDEBUG-guarded sanity (예: `quat_lideal_create_with_norm`의 sqrt(lattice_index)==N) 포함

## 8. 정리해두기

stale 파일 (예: `_NIGHTLY_SUMMARY_*.md`, 일회성 로그)은 작업 종료 후 즉시 `git rm`. 다음 세션 진입점 (`HANDOFF.md`)에 노이즈 안 보이게 유지.

새 세션이 GitHub만 보고 이어갈 수 있는지 셀프 체크:
- `HANDOFF.md` 한 번 보고 5분 안에 빌드/실행/검증 가능한가?
- 다음 작업 후보가 우선순위 매겨 적혀 있는가?
- 마지막 commit의 측정값이 `HANDOFF.md` 측정 테이블과 일치하는가?
