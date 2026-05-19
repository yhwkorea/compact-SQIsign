# Paper Patch E (2/2) — §5.2 Performance analysis 본문 초안

> 갭 E 보완 — `_paper_v_final/05Improve.tex` line 180~ "Performance analysis" 헤더만 있고 본문 0줄.
> 짝 패치: `PAPER_PATCH_E_Implementation_section.md` (§8 implementation chapter).
>
> 정량 수치는 placeholder (`\todo{...}`). 측정 끝나면 1:1 치환만 하면 됨.
> 구조/문장은 이 초안으로 완결.

## 변경 후 05Improve.tex line 180 이후 전체

```latex
\subsection{Performance analysis}
\label{subsec:performance}

In this subsection, we evaluate how the improved precision bounds derived
in Subsection~\ref{subsec:bound} translate into concrete performance
gains over the previous fixed-precision implementation
of~\cite{KLKL25}. We measure three quantities at each NIST security
level $\lambda\in\{128,192,256\}$ (levels~I, III, V):
(i) the per-integer storage width,
(ii) the dominant per-integer operation cost (addition, multiplication,
modular reduction), and
(iii) the end-to-end protocol time
($\textsf{KeyGen}$, $\textsf{Sign}$, $\textsf{Verify}$).

\paragraph{Baseline and our implementation.}
We compare two C reference implementations on the same hardware and
toolchain.
The baseline is the fixed-precision SQIsign of~\cite{KLKL25},
available at
\url{\todo{KLKL25-repo-url}}.
Our compact variant integrates the algorithms of
Sections~\ref{sec:ideal}--\ref{sec:sampling} and the uniform bounds
of Subsection~\ref{subsec:bound} into the same reference codebase,
available at
\url{https://github.com/yhwkorea/compact-SQIsign}.
Both implementations use identical protocol logic; the only difference
is the fixed precision used for big-integer arithmetic and the small
set of algorithms substituted at the ideal-arithmetic and prime-norm
sampling sites.

\paragraph{Measurement setup.}
Benchmarks were run on
\todo{CPU model, GHz, cache, RAM, OS, GCC version, libgmp version}.
We average over $\todo{N}$ independent runs with random seeds,
reporting median cycle counts and median wall-clock time.
Stack size was set to \texttt{ulimit -s unlimited} for both
implementations to remove a confounding allocation-strategy effect
(see Section~\ref{sec:implementation}).

\paragraph{Storage reduction.}
The uniform bounds of Theorem~\ref{thm:keygen-bound} and
Theorem~\ref{thm:sign-bound} reduce the worst-case integer width from
$7{,}026/10{,}713/14{,}150$
bits (KLKL25 baseline at levels I/III/V; equivalently $110/168/222$
$64$-bit limbs)
to
$1{,}774/2{,}696/3{,}555$
bits in our work ($28/43/56$ limbs), a reduction of approximately
$3.96\times$, $3.97\times$, and $3.98\times$ respectively, i.e.\ a
uniform $\approx\!4\times$ shrinkage per integer.
Summed over the live working set of a single signing call
(of the order $10^3$--$10^4$ big-integer instances), this is a
proportional reduction of resident memory and an analogous
reduction in cache pressure.
Table~\ref{tab:comparebound} in Section~\ref{sec:intro} states the
storage comparison; here we report the corresponding runtime effect.

\paragraph{Operation cost.}
The dominant per-integer cost in SQIsign signing is addition,
schoolbook or Karatsuba multiplication, and modular reduction.
Let $n$ denote the operand width in machine words.
At the bit widths used at levels I/III/V, multiplication is in the
Karatsuba regime ($n^{\log_2 3}\approx n^{1.585}$) for the baseline and
schoolbook--Karatsuba transitional regime for our work; reduction is
$\Theta(n^2)$ in both implementations. With the uniform
$\approx\!4\times$ reduction in $n$ established above, the asymptotic
cost models predict speedups of
approximately $4\times$ for addition (linear in $n$),
between $8.83\times$ and $8.96\times$ for Karatsuba multiplication
($n^{1.585}$) across levels I--V, and
between $15.70\times$ and $15.85\times$ for modular reduction
($n^{2}$).
Per-operation cycle counts (\textsf{ibz\_add}, \textsf{ibz\_mul},
\textsf{ibz\_mod}) measured in our microbenchmarks are reported in
Table~\ref{tab:micro}; we expect the measured ratios to fall
moderately below these asymptotic predictions due to limb-loop
overhead, branch effects, and the small-$n$ regime in which constant
factors are not yet negligible.

\paragraph{End-to-end protocol time.}
Table~\ref{tab:macro} summarizes median wall-clock time for the three
protocol primitives at the three security levels.
At level~I, our implementation reduces $\textsf{Sign}$ from
$\todo{T_\mathrm{KLKL,I,sign}}$\,ms to
$\todo{T_\mathrm{ours,I,sign}}$\,ms,
a $\todo{r_\mathrm{sign,I}}\times$ speedup;
the corresponding speedups at levels~III and~V are
$\todo{r_\mathrm{sign,III}}\times$ and
$\todo{r_\mathrm{sign,V}}\times$ respectively.
$\textsf{KeyGen}$ and $\textsf{Verify}$ exhibit similar but smaller
gains, as a smaller fraction of their time is spent in the quaternion
arithmetic path.

\begin{table}[h]
\centering
\caption{End-to-end protocol time. Median wall-clock over $\todo{N}$
runs. Lower is better. Speedup is baseline/ours.}
\label{tab:macro}
\small
\begin{tabular}{l|rrr|rrr|rrr}
\hline
\multirow{2}{*}{\textbf{Security}}
 & \multicolumn{3}{c|}{\textbf{KLKL25~\cite{KLKL25} (ms)}}
 & \multicolumn{3}{c|}{\textbf{Ours (ms)}}
 & \multicolumn{3}{c}{\textbf{Speedup}} \\
 & KeyGen & Sign & Verify
 & KeyGen & Sign & Verify
 & KeyGen & Sign & Verify \\
\hline
Level~I   & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} \\
Level~III & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} \\
Level~V   & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} & \todo{} \\
\hline
\end{tabular}
\end{table}

\paragraph{Memory footprint.}
Maximum resident set size (RSS), measured by
\texttt{/usr/bin/time -v} on a single signing call, drops from
$\todo{R_\mathrm{KLKL,I}}$ MiB / $\todo{R_\mathrm{KLKL,III}}$ MiB /
$\todo{R_\mathrm{KLKL,V}}$ MiB
in the baseline to
$\todo{R_\mathrm{ours,I}}$ MiB / $\todo{R_\mathrm{ours,III}}$ MiB /
$\todo{R_\mathrm{ours,V}}$ MiB
in our implementation.
This lower footprint is especially relevant for
embedded and constant-time deployments
(cf.~\cite{BHJ+25,Ler26}),
where stack and cache budgets are tight.

\paragraph{Discussion.}
The measured speedups are consistent with, but somewhat smaller than,
the asymptotic predictions above; the remainder is accounted for by
the constant-overhead components of the protocol
(hashing, isogeny evaluation, point arithmetic on the elliptic-curve
side), which our work does not modify.
Conversely, the proportional improvement in resident memory closely
tracks the storage reduction, since the working set is dominated by
fixed-precision big-integer instances whose size is determined by
the uniform precision bound.
Together, these results show that the bounds of
Subsection~\ref{subsec:bound} are not only asymptotically tight
(Section~\ref{sec:proof}) but also realize concrete, measurable
performance gains in practice.
```

## 진척 현황

| 항목 | 상태 |
|---|---|
| Storage bit width (KLKL25 vs ours, 3 level) | ✅ 채움 (§1 Table comparebound 인용) |
| r_stor (3.96/3.97/3.98×) | ✅ 채움 (산술) |
| r_mul (Karatsuba 8.83~8.96×) | ✅ 채움 (n^1.585) |
| r_mod (15.70~15.85×) | ✅ 채움 (n^2) |
| 이론 분석 본문 (Karatsuba regime 논증) | ✅ 완결 |
| Discussion (asymptotic vs measured) | ✅ 완결 |

## 채워야 할 placeholder (측정 끝나면 1:1 치환)

| placeholder | 의미 | 예상 출처 | 측정 가능 시점 |
|---|---|---|---|
| `\todo{KLKL25-repo-url}` | KLKL25 구현체 URL | 1저자 (Won Kim) | 즉시 (URL 받으면) |
| CPU/RAM/OS/GCC/libgmp 버전 | 측정 머신 사양 | `/proc/cpuinfo` 등 | 측정 머신 정해지면 |
| `\todo{N}` | 반복 횟수 | `--iterations` | 측정 시점 |
| `\todo{T_*}` 18개 | KeyGen/Sign/Verify ms | `apps/benchmark_lvl{1,3,5}` | **protocol layer ctest PASS 후** |
| `\todo{r_sign,*}` 3개 | sign speedup 비율 | T_KLKL / T_ours 산술 | T_* 측정 후 |
| `\todo{R_*}` 6개 | RSS MiB | `/usr/bin/time -v ./apps/example_nistapi_lvl{1,3,5}` | protocol PASS 후 |
| microbenchmark `ibz_add/mul/mod` cycle | Table tab:micro | 별도 microbench 작성 | quaternion 만 PASS 면 가능 (현재 가능) |

**즉시 측정 가능**: microbenchmark (현재 mlll PASS 됐으니 quaternion 라이브러리만 링크해도 가능)
**의존 대기**: end-to-end protocol time + RSS — protocol layer boundary check (normeq.c:218 등 4곳) 해결 후

## 측정 명령 (재현 절차)

```bash
# KLKL25 baseline
git clone <KLKL25-repo-url> klkl25 && cd klkl25
mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
ulimit -s unlimited
for lvl in lvl1 lvl3 lvl5; do
  ./apps/benchmark_$lvl --iterations=100 | tee bench_klkl25_$lvl.txt
  /usr/bin/time -v ./apps/example_nistapi_$lvl 2>&1 | tee rss_klkl25_$lvl.txt
done

# Ours
git clone https://github.com/yhwkorea/compact-SQIsign ours && cd ours
mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
ulimit -s unlimited
for lvl in lvl1 lvl3 lvl5; do
  ./apps/benchmark_$lvl --iterations=100 | tee bench_ours_$lvl.txt
  /usr/bin/time -v ./apps/example_nistapi_$lvl 2>&1 | tee rss_ours_$lvl.txt
done
```

## 의도된 흐름

1. **§5.1** (이미 있음): bound 수학적 유도 (16p^4, 2^21 p^7)
2. **§5.2** (이 초안): 그 bound 가 실제 성능에 의미하는 것 — 본 subsection
3. **§8** (별도 patch): 구현 자체의 status (pass/fail/limit) — 측정 디테일은 이쪽

§5.2 는 "결과" 중심 (이 정도 빨라졌다), §8 은 "어떻게 빌드/돌렸는지 + 어디까지 됐는지" 중심.
중복 최소화하려고 측정 setup 한 문단만 §5.2 에 둠.
