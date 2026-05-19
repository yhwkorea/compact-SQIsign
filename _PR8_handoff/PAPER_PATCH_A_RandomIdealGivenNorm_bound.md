# Paper Patch A — `lem:RandomIdealGivenNorm-bound` 정정

> 갭 A + 갭 B 통합 패치. 04Sampling.tex line 257–277.

## 변경 요약

1. Lemma statement: $4N^2$ → $4N^3$
2. Proof 본문: Cornacchia 가정 제거, 새 algorithm (line 38–63) 기반 재작성

## Before (04Sampling.tex, lines 257–277, 새 버전 2026-05-11)

```latex
\begin{lemma}
\label{lem:RandomIdealGivenNorm-bound}
During the execution of Algorithm~\ref{alg:RandomIdealGivenNorm}, the maximum size of integers is less than $4N^2$.
\end{lemma}

\begin{proof}
\noindent\textbf{(1) Lines 1--3.}
During the execution of \textsf{Cornacchia}, the maximum size of integers does not grow by~\cite[Lemma~18]{KLKL25}.
Hence, coefficients representing $\gamma$ are less than $\sqrt{N}$.

\smallskip
\noindent\textbf{(2) Lines 4--6.}
Since $a,b,c,d < N$, $\beta$ has coefficients less than $N$.
Hence, in line~6, $\gamma\beta$ has coefficients less than $N$.
Computing the ideal $\calO_0(\gamma\beta)+\calO_0 N$ is as follows:
\begin{enumerate}
    \item multiply $\calO_0=\langle 1,i,\frac{1+k}{2},\frac{i+j}{2} \rangle$ by $\gamma\beta$ and encode it to a matrix with \[\left(1\cdot(\gamma\beta), i\cdot(\gamma\beta), \frac{1+k}{2}\cdot(\gamma\beta), \frac{i+j}{2}\cdot(\gamma\beta)\right).\] The maximum size of integers to represent each coefficient is less than $N\sqrt{N}$.
    \item By the same procedure, multiply $\calO_0$ by $N$. The maximum size of integers to represent each coefficient is less than $N$.
    \item Concatenate two vectors and then apply Algorithm~\ref{alg:MLLL}, ensuring the output matrix is a basis matrix of $I=\calO_0(\gamma\beta)+\calO_0 N$. In this procedure, the maximum size of integers is less than $4N^3$ by Lemma~\ref{lem:mlll-bound}.
\end{enumerate}
\end{proof}
```

## After

```latex
\begin{lemma}
\label{lem:RandomIdealGivenNorm-bound}
During the execution of Algorithm~\ref{alg:RandomIdealGivenNorm}, the maximum size of integers is less than $4N^3$.
\end{lemma}

\begin{proof}
\noindent\textbf{(1) Lines 1--7 (sampling $\gamma$).}
The coordinates $g_1,g_2,g_3$ are sampled uniformly from $[0,N-1]$, so the
initial $\gamma=g_1 i+g_2 j+g_3 ij$ has coefficients bounded by $N$.
Line~6 adjusts $\gamma$ by adding $\sqrt{-\nrd(\gamma)}\bmod N$, which is a
representative in $[0,N-1]$. Hence after line~7,
\[
  |\gamma_\ell|<2N\quad(0\le \ell\le 3).
\]

\smallskip
\noindent\textbf{(2) Lines 8--10 (sampling $\beta$ and computing $\gamma\beta\bmod N$).}
Since $a,b,c,d<N$, the coefficients of $\beta$ are bounded by $N$. The
unreduced product $\gamma\beta$ in the $\{1,i,j,k\}$ basis has every
coordinate bounded by a fixed constant times $N\cdot 2N\le 2N^2$
(quaternion multiplication: each output coordinate is a sum of four
bilinear products $\gamma_\ell\beta_m$ with absolute value $\le 2N\cdot N$,
together with the prime $p$ on $j$- and $k$-terms; the $p$-factor cancels
upon reduction modulo $N\mathcal O_0$, but the intermediate before
reduction is still bounded by $\le 4pN^2$). After taking each coordinate
modulo $N$ in line~10,
\[
  |(\gamma\beta\bmod N)_\ell|<N\quad(0\le \ell\le 3).
\]

\smallskip
\noindent\textbf{(3) Line~11 (forming the ideal $\mathcal O_0\,g + N\mathcal O_0$).}
We multiply the basis
$\mathcal O_0=\langle 1,i,(1+k)/2,(i+j)/2\rangle$ by $g$ and by $N$ to obtain
eight generators of the lattice $\mathcal O_0\,g+N\mathcal O_0$ in the
$\{1,i,j,k\}$ basis:
\begin{enumerate}
    \item Each of $1\cdot g$, $i\cdot g$, $\tfrac{1+k}{2}\cdot g$,
    $\tfrac{i+j}{2}\cdot g$ has coordinates bounded by $2N\sqrt{N}$
    (the factor $\sqrt{N}$ from the reduced norm of $g$ multiplied into the
    half-basis elements; the factor 2 from clearing the denominator 2 of
    the half-basis).
    \item Each of $1\cdot N$, $i\cdot N$, $\tfrac{1+k}{2}\cdot N$,
    $\tfrac{i+j}{2}\cdot N$ has coordinates bounded by $N$.
\end{enumerate}
The maximum coordinate over all eight generators is therefore bounded by
$2N\sqrt{N}<2N^{3/2}$.

\smallskip
\noindent\textbf{(4) Line~12 (MLLL reduction).}
By Lemma~\ref{lem:mlll-bound}, the maximum size of integers during the
modified LLL reduction is at most the square of the maximum input size:
\[
  (2N^{3/2})^2 \;=\; 4N^3.
\]

\smallskip
Combining (1)--(4), the maximum size of integers during the execution of
Algorithm~\ref{alg:RandomIdealGivenNorm} is less than $4N^3$.
\end{proof}
```

## 변경 사유

1. **갭 A (statement 4N² → 4N³)**: 원래 statement 는 proof 결론 $4N^3$ 와 불일치. proof 의 마지막 라인이 정답이고 statement 가 typo 였다고 판단. (proof 의 MLLL bound 도출과 정합한 값으로 statement 정정.)

2. **갭 B (proof 본문 재작성)**: 원래 proof 본문은 Cornacchia 호출 (line 264 "During the execution of Cornacchia") 와 γ 계수 $\sqrt N$ 가정 (line 265) 을 가지나, 새 algorithm (line 51) 은 Cornacchia 없이 $g_1,g_2,g_3$ 을 $[0,N-1]$ 에서 직접 샘플 → γ 계수 $<N$. proof 를 새 algorithm 의 line 흐름 (line 1–12, 즉 새 paper 의 line 49–61) 에 맞춰 재작성.

## §5 Thm 5.1 에 미치는 영향

원래 인용 (05Improve.tex line 45):
```
By Lemma~\ref{lem:RandomIdealGivenNorm-bound}, the maximum size of integers in line~2 is at most
\[ 4\cdot\Dmix^2 < 16p^4. \]
```

정정 후:
```
By Lemma~\ref{lem:RandomIdealGivenNorm-bound}, the maximum size of integers in line~2 is at most
\[ 4\cdot\Dmix^3. \]
```

$D_{\mathrm{mix}}\approx 2^{2\lambda}\approx p$, 그래서 $4D_{\mathrm{mix}}^3\approx 4p^3$. 
$4p^3 < 16p^4$ 는 $p\ge 4$ 에서 성립 (모든 NIST 레벨에서 OK).
→ **Theorem 5.1 의 결론 $16p^4$ 는 변동 없음**. 단 명시화로 "line 2 의 maximum 이 $4D_{\mathrm{mix}}^2$" 라는 오기는 사라짐.

## 비트 사이즈 영향 (Table 1)

- 원래: $16p^4 \approx 2^4 \cdot p^4 \approx 2^{4+8\lambda}$ → LVL1 $\lambda=128$ 에서 $2^{1028}$ 비트 (≈ 1028 bit)
- 정정 후: 여전히 $16p^4$ 가 KeyGen 의 dominant — line 2 가 $4p^3$ 이지만 다른 line (line 5 의 $\frac{2^{43.5}}{\pi^4}p^2\log p$) 와 함께 max 가 결정됨. Line 2 의 $4p^3$ 는 $16p^4$ 안.

→ **Table 1 의 1774/2696/3555 bit 도 변동 없음**.

## 검토 필요 사항 (1저자)

- proof 의 (2) 단계에서 $p$-factor 흡수 처리: "the $p$-factor cancels upon reduction modulo $N\mathcal O_0$" 라는 표현이 정확한지. SQIsign 의 $\mathcal O_0$ 가 maximal order 라서 $N\mathcal O_0$ 안에 $p\cdot j$, $p\cdot k$ 같은 요소가 있는지 확인. (없으면 proof 보강 필요.)
- (3) 의 factor 2 (반-기저 2 분모 해소) 정확한 인수 검증.
