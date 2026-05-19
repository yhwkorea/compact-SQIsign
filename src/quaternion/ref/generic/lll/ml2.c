/* ML2 — Modified L2 algorithm (NS09 Figure 9), generic d-rank input.
 *
 * Implements:
 *   - Algorithm \ref{alg:ML2}                (03Ideal.tex)
 *   - Algorithm \ref{alg:lazy-size-reduction} (91proof.tex)
 *
 * Model (paper Issue 13):
 *   - exact ibz_t for vectors b_i and integer Gram matrix G
 *   - 53-bit float (dpe_t) for Cholesky r, GSO mu, intermediate s
 *
 * Indexing: paper is 1-indexed, this file is 0-indexed.
 *   - paper b_1..b_d        ->  b[0..d-1]
 *   - paper r,mu             ->  r[i][j], mu[i][j] for j <= i, both 0-indexed
 *   - paper zeta in [0..d-1] ->  zeta in [0..d-1]  (same semantics: skip count)
 *   - paper kappa in [1..d]  ->  kappa in [0..d-1]
 *
 * delta_bar/eta_bar use repo defaults DELTABAR=0.995, ETABAR=0.505 (Schnorr-Euchner).
 * Paper uses 0.875/0.525; the repo defaults LLL-reduce a strict superset of bases
 * the paper does and converge faster.
 */

#include <quaternion.h>
#include "lll_internals.h"
#include "internal.h"
#include "dpe.h"

#ifndef ML2_MAX_D
#define ML2_MAX_D 8
#endif

#define SYM_LOWER(M, i, j) ((i) >= (j) ? &(M)[i][j] : &(M)[j][i])

/* paper Issue 17 / Gap H: track max bit observed in ML2 internals to compare
 * against paper Theorem (Improved Sign bound 2^21*p^7 ~ 1757 bit at lvl1). */
static int _ml2_peak_bit = 0;
static inline void _ml2_track(const ibz_t *x) {
    int b = ibz_bitsize(x);
    if (b > _ml2_peak_bit) _ml2_peak_bit = b;
}

/* ========== state ========== */

typedef struct {
    int d;
    int zeta;
    int aborted; /* 1 if main_loop hit oscillation/iter cap */
    ibz_vec_4_t b[ML2_MAX_D];
    ibz_t G[ML2_MAX_D][ML2_MAX_D]; /* lower triangular, G[i][j] for j <= i */
    dpe_t r[ML2_MAX_D][ML2_MAX_D];
    dpe_t mu[ML2_MAX_D][ML2_MAX_D];
    dpe_t s[ML2_MAX_D + 1];
    dpe_t delta_bar, eta_bar;
} ml2_state_t;

static void
ml2_state_init(ml2_state_t *st, int d)
{
    st->d = d;
    st->zeta = 0;
    st->aborted = 0;
    for (int i = 0; i < d; i++) {
        ibz_vec_4_init(&st->b[i]);
        for (int j = 0; j < d; j++) {
            ibz_init(&st->G[i][j]);
            dpe_init(st->r[i][j]);
            dpe_init(st->mu[i][j]);
        }
    }
    for (int i = 0; i <= d; i++)
        dpe_init(st->s[i]);
    dpe_init(st->delta_bar);
    dpe_init(st->eta_bar);
    dpe_set_d(st->delta_bar, DELTABAR);
    dpe_set_d(st->eta_bar, ETABAR);
}

static void
ml2_state_finalize(ml2_state_t *st)
{
    int d = st->d;
    for (int i = 0; i < d; i++) {
        ibz_vec_4_finalize(&st->b[i]);
        for (int j = 0; j < d; j++) {
            ibz_finalize(&st->G[i][j]);
            dpe_clear(st->r[i][j]);
            dpe_clear(st->mu[i][j]);
        }
    }
    for (int i = 0; i <= d; i++)
        dpe_clear(st->s[i]);
    dpe_clear(st->delta_bar);
    dpe_clear(st->eta_bar);
}

/* Recompute the integer Gram matrix from scratch: G[i][j] = <b_i, b_j> for j <= i */
static void
ml2_recompute_gram(ml2_state_t *st)
{
    ibz_t tmp;
    ibz_init(&tmp);
    for (int i = 0; i < st->d; i++) {
        for (int j = 0; j <= i; j++) {
            ibz_set(&st->G[i][j], 0);
            for (int k = 0; k < 4; k++) {
                ibz_mul(&tmp, &st->b[i][k], &st->b[j][k]);
                _ml2_track(&tmp);
                ibz_add(&st->G[i][j], &st->G[i][j], &tmp);
            }
            _ml2_track(&st->G[i][j]);
        }
    }
    ibz_finalize(&tmp);
}

/* ========== CFA (steps 2-7 of NS09 Figure 2) ==========
 * Compute r[kappa][j], mu[kappa][j] for zeta < j < kappa, and
 * s[zeta..kappa] (partial sums of <b_kappa, b_kappa> minus mu^2*r diagonals).
 *
 * r[i][j] = <b_i, b_j*> ; mu[i][j] = r[i][j] / r[j][j]
 * s[k] = G[kappa][kappa] - sum_{j=zeta..k-1} mu[kappa][j]^2 * r[j][j]
 */
static void
ml2_cfa_kappa(ml2_state_t *st, int kappa)
{
    dpe_t tmp;
    dpe_init(tmp);

    int z = st->zeta;
    for (int j = z; j < kappa; j++) {
        dpe_set_z(st->r[kappa][j], &st->G[kappa][j]);
        for (int k = z; k < j; k++) {
            dpe_mul(tmp, st->r[kappa][k], st->mu[j][k]);
            dpe_sub(st->r[kappa][j], st->r[kappa][j], tmp);
        }
        dpe_div(st->mu[kappa][j], st->r[kappa][j], st->r[j][j]);
    }

    /* s[z] = G[kappa][kappa]; s[k+1] = s[k] - mu[kappa][k] * r[kappa][k] */
    dpe_set_z(st->s[z], &st->G[kappa][kappa]);
    for (int k = z; k < kappa; k++) {
        dpe_mul(tmp, st->mu[kappa][k], st->r[kappa][k]);
        dpe_sub(st->s[k + 1], st->s[k], tmp);
    }
    /* r[kappa][kappa] := s[kappa] */
    dpe_set(st->r[kappa][kappa], st->s[kappa]);

    dpe_clear(tmp);
}

/* ========== LazySizeReduce (paper Algorithm 14) ==========
 * Repeatedly: CFA -> if all |mu[kappa][j]| <= eta_bar, return;
 * else for i = kappa-1 down to zeta: X_i = round(mu[kappa][i]); update mu;
 * then b[kappa] -= sum X_i * b[i] and update G; loop.
 */
static void
ml2_lazy_size_reduce(ml2_state_t *st, int kappa)
{
    dpe_t tmp_f, X_f;
    ibz_t X_z, tmp_z;
    dpe_init(tmp_f);
    dpe_init(X_f);
    ibz_init(&X_z);
    ibz_init(&tmp_z);

    int z = st->zeta;

    /* paper Algorithm LazySizeReduce: loop until max |mu_kappa,j| <= eta_bar.
     * Paper guarantees size-monotonic convergence (Issue 13 author reply).
     * Large cap to let paper's monotonic argument dominate, not artificial abort. */
    for (int iter = 0; iter < 65536; iter++) {
        ml2_cfa_kappa(st, kappa);

        /* check if all |mu[kappa][j]| <= eta_bar */
        int need_reduce = 0;
        for (int j = z; j < kappa; j++) {
            if (dpe_cmp_d(st->mu[kappa][j], ETABAR) > 0 ||
                dpe_cmp_d(st->mu[kappa][j], -ETABAR) < 0) {
                need_reduce = 1;
                break;
            }
        }
        if (!need_reduce)
            goto done;

        /* for i = kappa-1 down to z: X_i = round(mu[kappa][i]); update mu[kappa][j]
         * for j < i; then b[kappa] -= X_i * b[i] and update G accordingly. */
        for (int i = kappa - 1; i >= z; i--) {
            dpe_set(X_f, st->mu[kappa][i]);
            dpe_round(X_f, X_f);
            dpe_get_z(&X_z, X_f);
            if (ibz_is_zero(&X_z))
                continue;

            /* mu[kappa][j] -= X_i * mu[i][j] for j = z..i-1 (Fp arithmetic) */
            for (int j = z; j < i; j++) {
                dpe_mul(tmp_f, X_f, st->mu[i][j]);
                dpe_sub(st->mu[kappa][j], st->mu[kappa][j], tmp_f);
            }

            /* b[kappa] -= X_i * b[i]  (exact integer vector update) */
            for (int c = 0; c < 4; c++) {
                ibz_mul(&tmp_z, &X_z, &st->b[i][c]);
                ibz_sub(&st->b[kappa][c], &st->b[kappa][c], &tmp_z);
            }

            /* Update lower-triangular G accordingly:
             *   <b_k - X*b_i, b_k - X*b_i> = <b_k,b_k> - 2X<b_k,b_i> + X^2<b_i,b_i>
             * Do it in two halves so each entry is updated exactly once:
             *   G[kappa][kappa] -= X * G[kappa][i]                 (first half)
             *   G[kappa][j]     -= X * G[i][j]   for j = z..d-1    (cross terms)
             * After the cross-term loop G[kappa][i] has changed too, and
             *   G[kappa][kappa] -= X * G[kappa][i] (new value)     (second half)
             * gives the full quadratic update.
             */
            ibz_mul(&tmp_z, &X_z, SYM_LOWER(st->G, kappa, i));
            ibz_sub(&st->G[kappa][kappa], &st->G[kappa][kappa], &tmp_z);
            for (int j = z; j < st->d; j++) {
                if (j == kappa)
                    continue;
                ibz_mul(&tmp_z, &X_z, SYM_LOWER(st->G, i, j));
                ibz_t *target = SYM_LOWER(st->G, kappa, j);
                ibz_sub(target, target, &tmp_z);
            }
            ibz_mul(&tmp_z, &X_z, SYM_LOWER(st->G, kappa, i));
            ibz_sub(&st->G[kappa][kappa], &st->G[kappa][kappa], &tmp_z);
        }
    }

done:
    dpe_clear(tmp_f);
    dpe_clear(X_f);
    ibz_finalize(&X_z);
    ibz_finalize(&tmp_z);
}

/* Insert b[from] right before b[to], 0 <= to <= from < d.
 * After: b'[to] = b[from], b'[to+1..from] = old b[to..from-1].
 * Rebuilds Gram matrix from scratch (caller-friendly; cost O(d^2) ibz_mul).
 */
static void
ml2_insert_before(ml2_state_t *st, int from, int to)
{
    if (from == to)
        return;
    /* rotate b */
    ibz_vec_4_t tmp;
    ibz_vec_4_init(&tmp);
    for (int c = 0; c < 4; c++)
        ibz_copy(&tmp[c], &st->b[from][c]);
    for (int i = from; i > to; i--)
        for (int c = 0; c < 4; c++)
            ibz_copy(&st->b[i][c], &st->b[i - 1][c]);
    for (int c = 0; c < 4; c++)
        ibz_copy(&st->b[to][c], &tmp[c]);
    ibz_vec_4_finalize(&tmp);
    /* simplest: recompute G. Could be optimized to permutation update. */
    ml2_recompute_gram(st);
}

/* ========== ML2 main loop (paper Algorithm 13, 0-indexed) ==========
 * Init: r[0][0] = G[0][0]; kappa = 1; zeta = 0.
 * Loop: while kappa < d
 *   LazySizeReduce(kappa)
 *   kappa_prime = kappa
 *   while kappa >= zeta + 1 && delta_bar * r[kappa-1][kappa-1] >= s[kappa-1] : kappa--
 *   copy mu, r row from kappa_prime into kappa, r[kappa][kappa] = s[kappa]
 *   insert b[kappa_prime] before b[kappa], rebuild G
 *   if b[kappa] is zero: zeta++
 *   kappa = max(zeta + 1, kappa + 1)
 */
static void
ml2_main_loop(ml2_state_t *st)
{
    dpe_t delta_r;
    dpe_init(delta_r);

    dpe_set_z(st->r[0][0], &st->G[0][0]);
    int kappa = 1;
    int outer_iter = 0;
    /* paper Issue 13 (author reply): "size-reduce하면 entry 증가 X, size 단조 감소".
     * Paper ML2 (NS09 Fig 9) does not have abort/oscillation detection — convergence
     * guaranteed by Lemma lazy-size-reduction-bound + size-monotonic invariant.
     * Set OUTER_MAX large enough that termination is dominated by paper's convergence
     * theorem, not by our cap. No same-state early-abort. */
    const int OUTER_MAX = 65536;

    while (kappa < st->d) {
        outer_iter++;
        if (outer_iter > OUTER_MAX) {
            static int _over_n = 0;
            if (_over_n++ < 3)
                fprintf(stderr, "[ML2-MAIN-OVER] iter>%d kappa=%d zeta=%d d=%d\n",
                    OUTER_MAX, kappa, st->zeta, st->d);
            fflush(stderr);
            st->aborted = 1;
            break;
        }

        ml2_lazy_size_reduce(st, kappa);

        int kappa_prime = kappa;
        while (kappa >= st->zeta + 1) {
            dpe_mul(delta_r, st->delta_bar, st->r[kappa - 1][kappa - 1]);
            if (dpe_cmp(delta_r, st->s[kappa - 1]) < 0)
                break;
            kappa--;
        }

        if (kappa != kappa_prime) {
            for (int i = st->zeta; i < kappa; i++) {
                dpe_set(st->mu[kappa][i], st->mu[kappa_prime][i]);
                dpe_set(st->r[kappa][i], st->r[kappa_prime][i]);
            }
            dpe_set(st->r[kappa][kappa], st->s[kappa]);
            ml2_insert_before(st, kappa_prime, kappa);
            /* paper-faithful fix (2026-05-17): insertion reorders b[zeta..kappa_prime],
             * so r[j][*], mu[j][*] for j in (kappa, kappa_prime] are stale. Recompute
             * the Cholesky rows for those positions so the next outer iter's Lovász
             * test sees fresh state — otherwise main loop oscillates. */
            for (int j = kappa + 1; j <= kappa_prime; j++) {
                ml2_cfa_kappa(st, j);
            }
        }

        if (ibz_vec_4_is_zero(&st->b[kappa])) {
            /* paper invariant: zero vectors accumulate at positions 0..zeta-1.
             * Output is b[zeta..d-1]. Swap the just-detected zero into b[zeta]
             * and recompute G so CFA's j range (zeta+1..kappa-1) skips it. */
            if (kappa != st->zeta) {
                for (int c = 0; c < 4; c++)
                    ibz_swap(&st->b[kappa][c], &st->b[st->zeta][c]);
                ml2_recompute_gram(st);
            }
            st->zeta++;
        }

        int next = kappa + 1;
        if (st->zeta + 1 > next)
            next = st->zeta + 1;
        kappa = next;
    }

    dpe_clear(delta_r);
}

/* ========== entry point ==========
 * Reduce d input generators. On return, the LLL-reduced basis is in
 * st.b[zeta..d-1] (rank = d - zeta). Copies up to `out_capacity` of those
 * into `output`. Returns the rank, or -1 on argument error.
 */
int
quat_ml2(ibz_vec_4_t *output,
         int out_capacity,
         const ibz_vec_4_t *input,
         int d,
         const quat_alg_t *alg)
{
    (void)alg;
    static int _ml2_n = 0;
    int N = _ml2_n++;
    int trace = (N < 3);
    if (trace) {
        fprintf(stderr, "[ML2 #%d] entry d=%d out_cap=%d input[0][0..3] limb0: %016lx %016lx %016lx %016lx\n",
            N, d, out_capacity,
            (unsigned long)input[0][0][0], (unsigned long)input[0][1][0],
            (unsigned long)input[0][2][0], (unsigned long)input[0][3][0]);
        fflush(stderr);
    }
    if (d < 1 || d > ML2_MAX_D || out_capacity < 0) {
        if (trace) fprintf(stderr, "[ML2 #%d] ARG ERROR\n", N);
        return -1;
    }

    ml2_state_t st;
    ml2_state_init(&st, d);
    int local_peak_start = _ml2_peak_bit;
    _ml2_peak_bit = 0;
    int input_peak = 0;
    for (int i = 0; i < d; i++)
        for (int c = 0; c < 4; c++) {
            ibz_copy(&st.b[i][c], &input[i][c]);
            int bb = ibz_bitsize(&st.b[i][c]);
            if (bb > input_peak) input_peak = bb;
            _ml2_track(&st.b[i][c]);
        }
    if (trace) {
        fprintf(stderr, "[ML2 #%d] input_peak=%d (max bit among 8x4 input entries)\n", N, input_peak);
        fflush(stderr);
    }
    ml2_recompute_gram(&st);
    if (trace) {
        fprintf(stderr, "[ML2 #%d] post_gram peak_so_far=%d\n", N, _ml2_peak_bit);
        fflush(stderr);
    }
    if (trace) { fprintf(stderr, "[ML2 #%d] gram done G[0][0] limb0..3: %016lx %016lx %016lx %016lx\n", N,
        (unsigned long)st.G[0][0][0], (unsigned long)st.G[0][0][1],
        (unsigned long)st.G[0][0][2], (unsigned long)st.G[0][0][3]); fflush(stderr); }

    ml2_main_loop(&st);
    if (trace) { fprintf(stderr, "[ML2 #%d] main_loop done zeta=%d aborted=%d\n", N, st.zeta, st.aborted); fflush(stderr); }

    int rho;
    int n;
    if (st.aborted) {
        /* Signal to caller: oscillation/iter-cap. Return -1 so caller can
         * fallback to HNF (or another rank-preserving reducer). The partial
         * LLL state has no guarantee that the first 4 vectors are linearly
         * independent, and the input generators are only a sublattice basis
         * (not the union lattice), so neither is safe to return blindly. */
        ml2_state_finalize(&st);
        if (trace) { fprintf(stderr, "[ML2 #%d] exit abort signal -1\n", N); fflush(stderr); }
        (void)output;
        (void)out_capacity;
        return -1;
    }
    /* Normal path: in-place zero handling kept b[zeta..d-1] nonzero. */
    int rho_nz = 0;
    for (int i = st.zeta; i < d; i++) {
        if (!ibz_vec_4_is_zero(&st.b[i])) {
            if (rho_nz != i - (int)st.zeta) {
                for (int c = 0; c < 4; c++)
                    ibz_swap(&st.b[st.zeta + rho_nz][c], &st.b[i][c]);
            }
            rho_nz++;
        }
    }
    rho = rho_nz;
    n = rho < out_capacity ? rho : out_capacity;
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 4; c++)
            ibz_copy(&output[i][c], &st.b[st.zeta + i][c]);

    /* Track final b and G size for peak report */
    for (int i = 0; i < d; i++)
        for (int c = 0; c < 4; c++)
            _ml2_track(&st.b[i][c]);
    for (int i = 0; i < d; i++)
        for (int j = 0; j <= i; j++)
            _ml2_track(&st.G[i][j]);

    if (trace) {
        fprintf(stderr, "[ML2 #%d] peak_bit_seen=%d (paper sign bound 2^21*p^7 ~ 1757 bit at lvl1)\n",
            N, _ml2_peak_bit);
        fflush(stderr);
    }
    (void)local_peak_start;

    ml2_state_finalize(&st);
    if (trace) { fprintf(stderr, "[ML2 #%d] exit rho=%d copied n=%d\n", N, rho, n); fflush(stderr); }
    return rho;
}
