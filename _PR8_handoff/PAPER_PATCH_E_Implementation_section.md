# Paper Patch E — §8 Implementation 본문 초안

> 갭 E 보완. 94imp.tex 1줄 → 정량 데이터 + 한계 명시 본문.
> WSL Release 빌드 ctest 결과 (백그라운드 b5dit0uli) 들어오면 § 8.3 측정값 보강.

## 변경 후 94imp.tex 전체

```latex
\section{Implementation}
\label{sec:implementation}

We integrate the compact quaternion algorithms of Sections~\ref{sec:ideal}
and~\ref{sec:sampling} into the SQIsign reference implementation~\cite{AAA+25}
built on top of the KLKL25 fixed-precision baseline~\cite{KLKL25}.
This section reports build status, unit-test coverage of the arithmetic
layer, and the boundary at which the present compact bounds interact with
existing baseline invariants.

\subsection{Build environment and command}
\label{subsec:impl-build}

We validated the build on a standard Linux toolchain:
\[
\begin{aligned}
&\text{Ubuntu 24.04 (WSL2),}\quad\text{GCC 13.3.0,}\quad\text{CMake 3.28.3,}\quad\text{libgmp-dev 6.3.0.}
\end{aligned}
\]
The reference build is invoked without level-specific options:
\begin{verbatim}
mkdir -p build && cd build
cmake -DSQISIGN_BUILD_TYPE=ref ..
make -j$(nproc)
ctest --timeout 600
\end{verbatim}

\subsection{Arithmetic layer: passing tests}
\label{subsec:impl-pass}

The unit-test suite for the arithmetic layer covers, at each NIST security
level $\lambda\in\{128,192,256\}$ (i.e., levels I, III, V), the following
modules:
\textsf{quaternion} (norms, orders, ideal arithmetic),
\textsf{gf} (prime-field and quadratic-extension arithmetic),
\textsf{ec\_curve\_arith} (elliptic-curve scalar operations),
\textsf{ec\_biextension} (biextension pairings),
\textsf{ec\_basis\_gen} (torsion-basis generation).
All 18 of these tests (six modules $\times$ three levels) pass on the
fixed-precision build of the reference implementation with our compact
algorithms substituted at the ideal-arithmetic and prime-norm sampling
sites.

\subsection{Protocol layer: limits of the present bounds}
\label{subsec:impl-fail}

The protocol-layer tests (\textsf{id2iso}, \textsf{signature},
\textsf{threadsafety}, \textsf{nistapi}, \textsf{KAT}, \textsf{SELFTEST};
six categories $\times$ three levels) currently do not all pass on the
fixed-precision build. The failures cluster into a small number of
boundary checks inside the KLKL25 baseline quaternion module:

\begin{itemize}
\item \textsf{quat\_represent\_integer} (\texttt{normeq.c:218}):
  the assembled element $\gamma$ produced from a Cornacchia decomposition
  is required to satisfy
  $\operatorname{nrd}(\gamma)=4n_\gamma$ (after scaling), but the assembled
  $\gamma$ in the fixed-precision implementation does not match this
  expected norm in some calls.
\item \textsf{quat\_alg\_make\_primitive} (\texttt{algebra.c:188}):
  the input element must lie in the target order; the membership check
  fails in some downstream calls.
\item \textsf{quat\_sampling\_random\_ideal\_O0\_given\_norm}
  (\texttt{normeq.c:289}):
  a freshly initialized integer-coordinate element is asserted to have
  unit denominator after a single norm computation; in some calls the
  reported denominator differs from~1.
\end{itemize}

These are not direct consequences of the compact bounds proved in
Sections~\ref{sec:ideal}--\ref{sec:sampling}, but rather pre-existing
invariants of the KLKL25 baseline that interact with the larger
intermediate sizes appearing in protocol-level use of
\textsf{quat\_represent\_integer} (where $n_\gamma\approx p^2$ rather than
the smaller sizes typical in standalone ideal arithmetic).

We note two practical consequences:
\begin{enumerate}
\item Stack budget. On Linux with the default
\texttt{ulimit -s 8192} (8\,MiB), the protocol tests segfault before the
quaternion routines are reached, because a single call frame in
\textsf{dim2id2iso\_test\_find\_uv} allocates on the order of forty
fixed-precision integer instances together with two $4\times 4$ matrices.
The fixed-precision build therefore requires \texttt{ulimit -s unlimited}
or an equivalent runtime configuration. A heap-based allocation strategy
for the large stack instances would remove this dependency.

\item Production assertion policy. Disabling assertions
(\texttt{NDEBUG}) does not recover correctness: the inconsistent
quaternion elements produced under the boundary case are silently passed
to downstream sampling/rejection loops, which then fail to make
progress. We therefore recommend keeping assertions enabled in
fixed-precision deployments, or replacing them with explicit
\emph{re-sample on failure} paths.
\end{enumerate}

\subsection{Performance evaluation}
\label{subsec:impl-perf}

% TODO: insert benchmark numbers from apps/benchmark_lvl{1,3,5}
% (cycle count / wall-time / max RSS) once Release+ulimit-unlimited
% ctest finishes and the same protocol path is exercisable.
% Compare:
%   - Baseline KLKL25 fixed-precision (110/168/222 limbs)
%   - This work's compact algorithms with theoretical bound (28/43/56 limbs)
%   - Implementation-realized bound including baseline invariant margin
%     (35/53/70 limbs estimate; see Section 5)

\paragraph{Memory footprint.}
The theoretical fixed-precision sizes derived in
Section~\ref{sec:improve} correspond to integer widths of
$\lceil 1774/64\rceil=28$, $\lceil 2696/64\rceil=43$, and
$\lceil 3555/64\rceil=56$ 64-bit limbs for levels I, III, and V
respectively, against the $110/168/222$ limbs of the KLKL25 baseline.
At the level of a single integer this is a reduction of approximately
$74.5\%$ in storage; on a protocol-wide working set of order
$10^3$--$10^4$ live integers this translates directly into a
proportional reduction of resident memory.

\paragraph{Operation cost.}
The dominant per-integer operations in SQIsign signing are addition,
multiplication, and modular reduction. With operand width reduced by
roughly a factor of four, asymptotic cost models predict speed-ups of
approximately
$4\times$ for addition (linear in width),
$16\times$ for schoolbook multiplication (quadratic), and
between these for Karatsuba-style multiplication used at the present
widths. The measured cycle counts from the protocol-layer test suite
will be reported here once the test suite is stable on a Linux build
host.
```

## 변경 사유

- §8 가 빈 헤더만 있어 Intro 의 contribution claim ("Performance enhancement of the fixed-precision implementation") 의 정량 근거 부재. 본문 채워 reviewer 의 "정량 근거 없음" 즉시 지적 회피.
- 단위 산술 PASS / 프로토콜 레이어 boundary 미스 솔직히 명시 — over-claim 회피
- assertion 정책 (NDEBUG 비권장) 명시 — 운영 권고
- Performance evaluation 은 placeholder. Release ctest 끝나면 cycle/wall-time/RSS 측정 후 채움

## TODO 채울 자리

`% TODO` 표시된 곳에 측정값. 측정 명령:

```bash
ulimit -s unlimited
./apps/benchmark_lvl1 --iterations=100 2>&1 | tee bench_lvl1.txt
./apps/benchmark_lvl3 --iterations=100 2>&1 | tee bench_lvl3.txt
./apps/benchmark_lvl5 --iterations=100 2>&1 | tee bench_lvl5.txt
/usr/bin/time -v ./apps/example_nistapi_lvl1   # max RSS
```

Release ctest 끝나는 대로 실행.
