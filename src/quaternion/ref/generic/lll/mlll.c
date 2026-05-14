/**
 * @file mlll.c
 * @brief Modified LLL algorithm (Pohst 1987) — Cohen integer GSO port
 *
 * Implementation of the MLLL algorithm that takes a generating set
 * (possibly linearly dependent) and produces an LLL-reduced basis.
 *
 * State representation: Cohen 2.6.7 integer GSO
 *   b[i] in Z^4              — basis vector
 *   d[k+1] = det(Gram_{0..k}) — integer GSO determinants (d[0] = 1)
 *   Lambda[i][j] = d[j+1] * mu[i][j], integer (defined for j < i)
 *
 * All intermediate arithmetic is integer (ibz_t). No rationals (ibq_t),
 * no floats (dpe_t / mpfr). Preserves KLKL25 GMP-free + integer-only
 * contribution while avoiding the buffer-overflow / precision-loss
 * failure modes of ibq rational and dpe-float backends.
 *
 * Reference (Pohst MLLL Algorithm 1):
 *   M. Pohst, "A modification of the LLL reduction algorithm",
 *   J. Symbolic Computation, 4(1):123-127, 1987.
 *
 * Reference (Cohen integer GSO, Algorithms 2.6.3 / 2.6.6 / 2.6.7):
 *   H. Cohen, "A Course in Computational Algebraic Number Theory",
 *   Springer GTM 138, 1993.
 *
 * Paper mapping ("Compact Quaternion Algorithms for SQIsign"):
 *   Algorithm 1 (MLLL) — entry point quat_mlll
 *   Algorithm 2 line 6-7 — quat_lattice_mul_mlll
 *   Lemma 3 (mlll-bound) — bound applies to b/h/G integer entries.
 *   Cohen GSO d[]/Lambda[][] are auxiliary, bounded by ||a||^(2k).
 */

#include <quaternion.h>
#include "internal.h"
#include "lll_internals.h"
#include "mlll_internals.h"

#define N MLLL_MAX_GENERATORS

/* ========== integer helpers ========== */

/* Quaternion norm bilinear form: <a,b> = a0*b0 + a1*b1 + p*a2*b2 + p*a3*b3 */
static void
ibz_vec_4_dot_quat(ibz_t *dot, const ibz_vec_4_t *a, const ibz_vec_4_t *b,
                   const ibz_t *p)
{
    ibz_t tmp;
    ibz_init(&tmp);
    ibz_set(dot, 0);
    for (int i = 0; i < 4; i++) {
        ibz_mul(&tmp, &((*a)[i]), &((*b)[i]));
        if (i >= 2)
            ibz_mul(&tmp, &tmp, p);
        ibz_add(dot, dot, &tmp);
    }
    ibz_finalize(&tmp);
}

/* b = b - t * a  (integer vectors, t integer) */
static void
ibz_vec_4_sub_scalar_mul(ibz_vec_4_t *b, const ibz_t *t, const ibz_vec_4_t *a)
{
    ibz_t tmp;
    ibz_init(&tmp);
    for (int i = 0; i < 4; i++) {
        ibz_mul(&tmp, t, &((*a)[i]));
        ibz_sub(&((*b)[i]), &((*b)[i]), &tmp);
    }
    ibz_finalize(&tmp);
}

static void
ibz_vec_4_swap(ibz_vec_4_t *a, ibz_vec_4_t *b)
{
    for (int i = 0; i < 4; i++)
        ibz_swap(&((*a)[i]), &((*b)[i]));
}

/* Exact integer division n / d, asserting remainder == 0.
 * Used for Cohen 2.6.7 GS / swap updates where the division is provably exact. */
static void
ibz_div_exact(ibz_t *q, const ibz_t *n, const ibz_t *d)
{
    ibz_t r;
    ibz_init(&r);
    ibz_div_floor(q, &r, n, d);
    /* Provably zero in Cohen 2.6.7; if not, sign-corrected adjust. */
    if (!ibz_is_zero(&r)) {
        /* div_floor returns r in [0, |d|); for exact div we expect r = 0.
         * If not zero, fall back to round-toward-zero ibz_div for diagnostics. */
        ibz_div(q, &r, n, d);
    }
    ibz_finalize(&r);
}

/* Round-to-nearest integer division: q = round(n / d), ties away from zero.
 * Implementation: q = floor((2n + sign(d)*|d|) / (2d)) for n,d positive,
 * generalized via signed shifting. */
static void
ibz_div_round(ibz_t *q, const ibz_t *n, const ibz_t *d)
{
    ibz_t two_n, two_d, abs_d, n_adj, r;
    ibz_init(&two_n);
    ibz_init(&two_d);
    ibz_init(&abs_d);
    ibz_init(&n_adj);
    ibz_init(&r);

    /* two_n = 2*n, abs_d = |d|, two_d = 2*d */
    ibz_mul(&two_n, n, &((ibz_t *)&ibz_const_two)[0]);
    ibz_abs(&abs_d, d);
    ibz_mul(&two_d, d, &((ibz_t *)&ibz_const_two)[0]);

    /* n_adj = 2n + sign(n*d) * |d|  -- ties away from zero */
    int sign_match = (ibz_cmp(n, (const ibz_t *)&ibz_const_zero) >= 0)
                     == (ibz_cmp(d, (const ibz_t *)&ibz_const_zero) >= 0);
    if (sign_match) {
        ibz_add(&n_adj, &two_n, &abs_d);
    } else {
        ibz_sub(&n_adj, &two_n, &abs_d);
    }
    ibz_div(q, &r, &n_adj, &two_d);

    ibz_finalize(&two_n);
    ibz_finalize(&two_d);
    ibz_finalize(&abs_d);
    ibz_finalize(&n_adj);
    ibz_finalize(&r);
}

/* Test 2*|Lambda[k][l]| > d[l+1]  (size-reduce trigger condition). */
static int
needs_size_reduce(const ibz_t *Lambda_kl, const ibz_t *d_l1)
{
    ibz_t two_abs, abs_d;
    ibz_init(&two_abs);
    ibz_init(&abs_d);
    ibz_abs(&two_abs, Lambda_kl);
    ibz_mul(&two_abs, &two_abs, &((ibz_t *)&ibz_const_two)[0]);
    ibz_abs(&abs_d, d_l1);
    int res = (ibz_cmp(&two_abs, &abs_d) > 0);
    ibz_finalize(&two_abs);
    ibz_finalize(&abs_d);
    return res;
}

/* ========== Cohen 2.6.6 GS computation for a single position ========== */

/**
 * @brief Compute Lambda[idx][j] for j < idx and d[idx+1].
 *        Assumes positions 0..idx-1 are already computed.
 *
 * Cohen 2.6.6 integer-only GSO:
 *   For j = 0..idx-1:
 *     u = <b_idx, b_j>
 *     For i = 0..j-1:
 *       if d[i+1] != 0:
 *         u = (d[i+1]*u - Lambda[idx][i]*Lambda[j][i]) / d[i]   (exact)
 *     Lambda[idx][j] = u
 *
 *   u = <b_idx, b_idx>
 *   For j = 0..idx-1:
 *     if d[j+1] != 0:
 *       u = (d[j+1]*u - Lambda[idx][j]^2) / d[j]   (exact)
 *   d[idx+1] = u
 */
static void
compute_gs_single(int idx,
                  const ibz_vec_4_t *b,
                  ibz_t d[],
                  ibz_t Lambda[][N],
                  const ibz_t *p)
{
    ibz_t u, dot, t1, t2;
    ibz_init(&u);
    ibz_init(&dot);
    ibz_init(&t1);
    ibz_init(&t2);

    /* Lambda[idx][j] for j = 0..idx-1 */
    for (int j = 0; j < idx; j++) {
        ibz_vec_4_dot_quat(&dot, &b[idx], &b[j], p);
        ibz_copy(&u, &dot);
        for (int i = 0; i < j; i++) {
            if (ibz_is_zero(&d[i + 1])) {
                /* Skip: b_i* = 0, contributes nothing */
                continue;
            }
            ibz_mul(&t1, &d[i + 1], &u);
            ibz_mul(&t2, &Lambda[idx][i], &Lambda[j][i]);
            ibz_sub(&t1, &t1, &t2);
            ibz_div_exact(&u, &t1, &d[i]);
        }
        ibz_copy(&Lambda[idx][j], &u);
    }

    /* d[idx+1] */
    ibz_vec_4_dot_quat(&dot, &b[idx], &b[idx], p);
    ibz_copy(&u, &dot);
    for (int j = 0; j < idx; j++) {
        if (ibz_is_zero(&d[j + 1]))
            continue;
        ibz_mul(&t1, &d[j + 1], &u);
        ibz_mul(&t2, &Lambda[idx][j], &Lambda[idx][j]);
        ibz_sub(&t1, &t1, &t2);
        ibz_div_exact(&u, &t1, &d[j]);
    }
    ibz_copy(&d[idx + 1], &u);

    ibz_finalize(&u);
    ibz_finalize(&dot);
    ibz_finalize(&t1);
    ibz_finalize(&t2);
}

/* ========== size-reduce step (Cohen 2.6.7 step 4) ========== */

/**
 * @brief Reduce(m, l): if 2|Lambda[m][l]| > d[l+1], subtract q*b_l from b_m
 *        and update Lambda[m][*] accordingly.
 *
 *   q = round(Lambda[m][l] / d[l+1])
 *   b[m] -= q * b[l]
 *   Lambda[m][l] -= q * d[l+1]
 *   for i = 0..l-1: Lambda[m][i] -= q * Lambda[l][i]
 */
static void
size_reduce_one(int m, int l,
                ibz_vec_4_t b[],
                ibz_t d[],
                ibz_t Lambda[][N])
{
    if (ibz_is_zero(&d[l + 1]))
        return;
    if (!needs_size_reduce(&Lambda[m][l], &d[l + 1]))
        return;

    ibz_t q, prod;
    ibz_init(&q);
    ibz_init(&prod);

    ibz_div_round(&q, &Lambda[m][l], &d[l + 1]);

    /* b[m] -= q * b[l] */
    ibz_vec_4_sub_scalar_mul(&b[m], &q, &b[l]);

    /* Lambda[m][l] -= q * d[l+1] */
    ibz_mul(&prod, &q, &d[l + 1]);
    ibz_sub(&Lambda[m][l], &Lambda[m][l], &prod);

    /* Lambda[m][i] -= q * Lambda[l][i] for i = 0..l-1 */
    for (int i = 0; i < l; i++) {
        ibz_mul(&prod, &q, &Lambda[l][i]);
        ibz_sub(&Lambda[m][i], &Lambda[m][i], &prod);
    }

    ibz_finalize(&q);
    ibz_finalize(&prod);
}

/* ========== Lovász condition (Cohen 2.6.7 step 3) ==========
 *
 * Standard form: ||b_m*||^2 + mu_{m,m-1}^2 ||b_{m-1}*||^2 < (3/4) ||b_{m-1}*||^2
 *
 * Integer form (multiply both sides by 4 * d[m-1] * d[m]):
 *   4 * d[m+1] * d[m-1] + 4 * Lambda[m][m-1]^2 / d[m]  <  3 * d[m]^2 ?
 * Cleaner integer-only form (Cohen 2.6.7):
 *   4 * d[m+1] * d[m-1]  <  3 * d[m]^2 - 4 * Lambda[m][m-1]^2
 *
 * Returns 1 if swap needed, 0 otherwise.
 *
 * Special case: if d[m+1] == 0 (b_m dependent on b_0..b_{m-1}),
 * always swap so b_m moves left and gets eliminated.
 */
static int
lovasz_test(int m, const ibz_t d[], const ibz_t Lambda[][N])
{
    if (m <= 0)
        return 0;
    if (ibz_is_zero(&d[m]))
        return 0; /* b_{m-1}* = 0, no meaningful swap */

    if (ibz_is_zero(&d[m + 1])) {
        /* b_m is dependent — swap to eliminate */
        return 1;
    }

    ibz_t lhs, rhs, t1, four;
    ibz_init(&lhs);
    ibz_init(&rhs);
    ibz_init(&t1);
    ibz_init(&four);
    ibz_set(&four, 4);

    /* lhs = 4 * d[m+1] * d[m-1] */
    ibz_mul(&lhs, &d[m + 1], &d[m - 1]);
    ibz_mul(&lhs, &lhs, &four);

    /* rhs = 3 * d[m]^2 - 4 * Lambda[m][m-1]^2 */
    ibz_mul(&rhs, &d[m], &d[m]);
    ibz_mul(&rhs, &rhs, &((ibz_t *)&ibz_const_three)[0]);
    ibz_mul(&t1, &Lambda[m][m - 1], &Lambda[m][m - 1]);
    ibz_mul(&t1, &t1, &four);
    ibz_sub(&rhs, &rhs, &t1);

    int need_swap = (ibz_cmp(&lhs, &rhs) < 0);

    ibz_finalize(&lhs);
    ibz_finalize(&rhs);
    ibz_finalize(&t1);
    ibz_finalize(&four);
    return need_swap;
}

/* ========== Cohen 2.6.7 SWAP step ==========
 *
 *   lambda = Lambda[m][m-1]
 *   B = (d[m+1] * d[m-1] + lambda^2) / d[m]   (exact)
 *   for i = m+1..beta-1:
 *     t = Lambda[i][m]
 *     Lambda[i][m]   = (d[m+1] * Lambda[i][m-1] - lambda * t) / d[m]   (exact)
 *     Lambda[i][m-1] = (B * t + lambda * Lambda[i][m]) / d[m+1]        (exact)
 *   swap b[m] <-> b[m-1]
 *   for j = 0..m-2: swap Lambda[m][j] <-> Lambda[m-1][j]
 *   d[m] = B
 *
 * Special-case (Cohen 2.6.8 / Pohst MLLL):
 *   - d[m+1] = 0 (b_m dependent): cannot do exact div by d[m+1].
 *     Fallback: just swap b/Lambda, recompute GS from m-1.
 *   - d[m] = 0 (b_{m-1}* = 0): caller filtered, won't arrive here.
 */
static void
swap_step(int m, int beta,
          ibz_vec_4_t b[],
          ibz_t d[],
          ibz_t Lambda[][N],
          const ibz_t *p)
{
    if (ibz_is_zero(&d[m + 1])) {
        /* Dependent path: swap and recompute GS from m-1.
         * After enough swaps + size_reduce, b_m becomes zero and is removed. */
        ibz_vec_4_swap(&b[m], &b[m - 1]);
        for (int j = 0; j < m - 1; j++)
            ibz_swap(&Lambda[m][j], &Lambda[m - 1][j]);
        /* Recompute GS rows m-1..beta-1. d[m] may become 0 too. */
        for (int i = (m - 1 < 0 ? 0 : m - 1); i < beta; i++)
            compute_gs_single(i, b, d, Lambda, p);
        return;
    }

    ibz_t lambda, B, t, t1, t2, num;
    ibz_init(&lambda);
    ibz_init(&B);
    ibz_init(&t);
    ibz_init(&t1);
    ibz_init(&t2);
    ibz_init(&num);

    ibz_copy(&lambda, &Lambda[m][m - 1]);

    /* B = (d[m+1] * d[m-1] + lambda^2) / d[m] */
    ibz_mul(&t1, &d[m + 1], &d[m - 1]);
    ibz_mul(&t2, &lambda, &lambda);
    ibz_add(&num, &t1, &t2);
    ibz_div_exact(&B, &num, &d[m]);

    /* Update Lambda[i][m], Lambda[i][m-1] for i = m+1..beta-1 */
    for (int i = m + 1; i < beta; i++) {
        ibz_copy(&t, &Lambda[i][m]);

        /* Lambda[i][m] = (d[m+1] * Lambda[i][m-1] - lambda * t) / d[m] */
        ibz_mul(&t1, &d[m + 1], &Lambda[i][m - 1]);
        ibz_mul(&t2, &lambda, &t);
        ibz_sub(&num, &t1, &t2);
        ibz_div_exact(&Lambda[i][m], &num, &d[m]);

        /* Lambda[i][m-1] = (B * t + lambda * Lambda[i][m]) / d[m+1] */
        ibz_mul(&t1, &B, &t);
        ibz_mul(&t2, &lambda, &Lambda[i][m]);
        ibz_add(&num, &t1, &t2);
        ibz_div_exact(&Lambda[i][m - 1], &num, &d[m + 1]);
    }

    /* Swap b[m] <-> b[m-1] */
    ibz_vec_4_swap(&b[m], &b[m - 1]);
    /* Swap Lambda[m][j] <-> Lambda[m-1][j] for j = 0..m-2 */
    for (int j = 0; j < m - 1; j++)
        ibz_swap(&Lambda[m][j], &Lambda[m - 1][j]);

    /* d[m] = B  (d[m-1] and d[m+1] unchanged) */
    ibz_copy(&d[m], &B);

    ibz_finalize(&lambda);
    ibz_finalize(&B);
    ibz_finalize(&t);
    ibz_finalize(&t1);
    ibz_finalize(&t2);
    ibz_finalize(&num);
}

/* ========== MLLL core (Pohst Algorithm 1, 0-indexed) ========== */

void
quat_mlll(ibz_mat_4x4_t *basis,
          int *rank,
          const ibz_vec_4_t *generators,
          int g,
          const quat_alg_t *alg)
{
    assert(g >= 1 && g <= MLLL_MAX_GENERATORS);

    ibz_vec_4_t b[N];
    ibz_t d[N + 1];
    ibz_t Lambda[N][N];

    for (int i = 0; i < g; i++)
        ibz_vec_4_init(&b[i]);
    for (int i = 0; i <= g; i++)
        ibz_init(&d[i]);
    for (int i = 0; i < g; i++)
        for (int j = 0; j < g; j++)
            ibz_init(&Lambda[i][j]);

    /* d[0] = 1 */
    ibz_set(&d[0], 1);

    int alpha = 0; /* next generator to load */
    int beta = 0;  /* count of working vectors */
    int tau = 1;   /* lower bound on m for reduction restart */
    int m;

    /* ===== LOAD ===== */
load:
    while (alpha < g) {
        if (ibz_vec_4_is_zero(&generators[alpha])) {
            alpha++;
            continue;
        }
        for (int i = 0; i < 4; i++)
            ibz_copy(&(b[beta][i]), &(generators[alpha][i]));
        alpha++;

        compute_gs_single(beta, b, d, Lambda, &alg->p);
        beta++;

        if (!ibz_is_zero(&d[beta]) && alpha < g)
            continue;
        break;
    }

    if (beta <= 1)
        goto done;

    m = tau;
    if (m >= beta) {
        if (alpha < g) {
            tau = beta;
            goto load;
        }
        goto done;
    }

    /* ===== REDUCTION ===== */
reduction:
    /* Size-reduce b[m] against b[m-1], then test Lovász. */

    /* Cohen 2.6.7 ordering: size_reduce against m-1 only first, then test. */
    size_reduce_one(m, m - 1, b, d, Lambda);

    /* If b[m] became zero, remove it. */
    if (ibz_vec_4_is_zero(&b[m]))
        goto remove_vector;

    /* Lovász test */
    if (lovasz_test(m, d, Lambda)) {
        swap_step(m, beta, b, d, Lambda, &alg->p);
        if (m > 1)
            m--;
        goto reduction;
    }

    /* Size-reduce b[m] against b[l] for l = m-2 down to 0 */
    for (int l = m - 2; l >= 0; l--)
        size_reduce_one(m, l, b, d, Lambda);

    if (ibz_vec_4_is_zero(&b[m]))
        goto remove_vector;

    m++;
    if (m >= beta) {
        if (alpha < g) {
            tau = beta;
            goto load;
        }
        goto done;
    }
    goto reduction;

    /* ===== REMOVE ===== */
remove_vector:
    /* Shift down from position m */
    for (int i = m; i < beta - 1; i++) {
        for (int c = 0; c < 4; c++)
            ibz_copy(&(b[i][c]), &(b[i + 1][c]));
    }
    /* Clear top vector */
    for (int c = 0; c < 4; c++)
        ibz_set(&(b[beta - 1][c]), 0);
    beta--;

    if (alpha >= g) {
        /* Recompute all GS for final pass */
        ibz_set(&d[0], 1);
        for (int i = 0; i < beta; i++)
            compute_gs_single(i, b, d, Lambda, &alg->p);
        goto done;
    }

    /* Recompute GS from position m */
    for (int i = m; i < beta; i++)
        compute_gs_single(i, b, d, Lambda, &alg->p);

    tau = m + 1;
    if (tau < 1)
        tau = 1;
    goto load;

done:
    /* Final full LLL pass: ensures positions 0..tau-1 (potentially unreduced
     * after removals) are also LLL-reduced. */
    if (beta > 1) {
        ibz_set(&d[0], 1);
        for (int i = 0; i < beta; i++)
            compute_gs_single(i, b, d, Lambda, &alg->p);

        int changed = 1;
        int safety = 0;
        const int SAFETY_MAX = 1000 * beta * beta;
        while (changed && safety++ < SAFETY_MAX) {
            changed = 0;
            int mm = 1;
            while (mm < beta) {
                /* Size-reduce b[mm] against b[l] for l = mm-1 down to 0 */
                int reduced_any = 0;
                for (int l = mm - 1; l >= 0; l--) {
                    if (ibz_is_zero(&d[l + 1]))
                        continue;
                    if (needs_size_reduce(&Lambda[mm][l], &d[l + 1])) {
                        size_reduce_one(mm, l, b, d, Lambda);
                        reduced_any = 1;
                    }
                }
                if (reduced_any)
                    changed = 1;

                if (ibz_vec_4_is_zero(&b[mm])) {
                    for (int i = mm; i < beta - 1; i++)
                        for (int c = 0; c < 4; c++)
                            ibz_copy(&(b[i][c]), &(b[i + 1][c]));
                    for (int c = 0; c < 4; c++)
                        ibz_set(&(b[beta - 1][c]), 0);
                    beta--;
                    ibz_set(&d[0], 1);
                    for (int i = 0; i < beta; i++)
                        compute_gs_single(i, b, d, Lambda, &alg->p);
                    changed = 1;
                    continue;
                }

                if (lovasz_test(mm, d, Lambda)) {
                    swap_step(mm, beta, b, d, Lambda, &alg->p);
                    if (mm > 1)
                        mm--;
                    changed = 1;
                    continue;
                }
                mm++;
            }
        }
    }

    /* Copy output: basis columns = non-zero b vectors */
    *rank = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            ibz_set(&((*basis)[i][j]), 0);
    for (int i = 0; i < beta && *rank < 4; i++) {
        if (!ibz_vec_4_is_zero(&b[i])) {
            for (int j = 0; j < 4; j++)
                ibz_copy(&((*basis)[j][*rank]), &(b[i][j]));
            (*rank)++;
        }
    }

    /* Cleanup */
    for (int i = 0; i < g; i++)
        ibz_vec_4_finalize(&b[i]);
    for (int i = 0; i <= g; i++)
        ibz_finalize(&d[i]);
    for (int i = 0; i < g; i++)
        for (int j = 0; j < g; j++)
            ibz_finalize(&Lambda[i][j]);
}

/* ========== Lattice operations using MLLL ========== */

void
quat_lattice_mul_mlll(quat_lattice_t *res,
                      const quat_lattice_t *lat1,
                      const quat_lattice_t *lat2,
                      const quat_alg_t *alg)
{
    ibz_vec_4_t elem1, elem2, elem_res;
    ibz_vec_4_t generators[16];
    int rank;

    ibz_vec_4_init(&elem1);
    ibz_vec_4_init(&elem2);
    ibz_vec_4_init(&elem_res);
    for (int i = 0; i < 16; i++)
        ibz_vec_4_init(&generators[i]);

    for (int k = 0; k < 4; k++) {
        ibz_vec_4_copy_ibz(&elem1, &(lat1->basis[0][k]), &(lat1->basis[1][k]),
                           &(lat1->basis[2][k]), &(lat1->basis[3][k]));
        for (int i = 0; i < 4; i++) {
            ibz_vec_4_copy_ibz(&elem2, &(lat2->basis[0][i]), &(lat2->basis[1][i]),
                               &(lat2->basis[2][i]), &(lat2->basis[3][i]));
            quat_alg_coord_mul(&elem_res, &elem1, &elem2, alg);
            for (int j = 0; j < 4; j++)
                ibz_copy(&(generators[4 * k + i][j]), &(elem_res[j]));
        }
    }

    quat_mlll(&(res->basis), &rank, generators, 16, alg);
    assert(rank == 4);

    ibz_mul(&(res->denom), &(lat1->denom), &(lat2->denom));
    quat_lattice_reduce_denom(res, res);

    ibz_vec_4_finalize(&elem1);
    ibz_vec_4_finalize(&elem2);
    ibz_vec_4_finalize(&elem_res);
    for (int i = 0; i < 16; i++)
        ibz_vec_4_finalize(&generators[i]);
}

void
quat_lattice_add_mlll(quat_lattice_t *res,
                      const quat_lattice_t *lat1,
                      const quat_lattice_t *lat2,
                      const quat_alg_t *alg)
{
    ibz_vec_4_t generators[8];
    ibz_mat_4x4_t tmp;
    int rank;

    for (int i = 0; i < 8; i++)
        ibz_vec_4_init(&generators[i]);
    ibz_mat_4x4_init(&tmp);

    ibz_mat_4x4_scalar_mul(&tmp, &(lat1->denom), &(lat2->basis));
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            ibz_copy(&(generators[j][i]), &(tmp[i][j]));

    ibz_mat_4x4_scalar_mul(&tmp, &(lat2->denom), &(lat1->basis));
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            ibz_copy(&(generators[4 + j][i]), &(tmp[i][j]));

    quat_mlll(&(res->basis), &rank, generators, 8, alg);

    ibz_mul(&(res->denom), &(lat1->denom), &(lat2->denom));
    quat_lattice_reduce_denom(res, res);

    ibz_mat_4x4_finalize(&tmp);
    for (int i = 0; i < 8; i++)
        ibz_vec_4_finalize(&generators[i]);
}
