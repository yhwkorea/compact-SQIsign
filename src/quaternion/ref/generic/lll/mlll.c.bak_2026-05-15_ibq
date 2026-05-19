/**
 * @file mlll.c
 * @brief Modified LLL algorithm (Pohst 1987)
 *
 * Implementation of the MLLL algorithm that takes a generating set
 * (possibly linearly dependent) and produces an LLL-reduced basis.
 * Uses exact rational arithmetic (ibq_t) for Gram-Schmidt coefficients.
 *
 * Reference:
 *   M. Pohst, "A modification of the LLL reduction algorithm",
 *   J. Symbolic Computation, 4(1):123-127, 1987.
 *
 * Based on Algorithm 1 from:
 *   "Compact Quaternion Algorithms for SQIsign"
 *   (Kim, Lee, Yoo - Korea University)
 */

#include <quaternion.h>
#include "internal.h"
#include "lll_internals.h"
#include "mlll_internals.h"

/* ---------- helpers ---------- */

/**
 * @brief Compute quaternion norm-form inner product:
 *        a0*b0 + a1*b1 + p*(a2*b2 + a3*b3)
 */
static void
ibz_vec_4_dot_quat(ibz_t *dot, const ibz_vec_4_t *a, const ibz_vec_4_t *b, const ibz_t *p)
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

/**
 * @brief b = b - t * a  (integer vectors, t is integer)
 */
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

/**
 * @brief Round rational to nearest integer: t = round(num/denom)
 */
static void
ibq_round(ibz_t *t, const ibq_t *q)
{
    ibz_t num2, denom2, r;
    ibz_init(&num2);
    ibz_init(&denom2);
    ibz_init(&r);

    /* t = floor((2*num + denom) / (2*denom)) */
    ibz_mul(&num2, &((*q)[0]), &ibz_const_two);
    ibz_add(&num2, &num2, &((*q)[1]));
    ibz_mul(&denom2, &((*q)[1]), &ibz_const_two);

    ibz_div(t, &r, &num2, &denom2);
    /* ibz_div rounds toward zero; adjust to floor for negative */
    if (!ibz_is_zero(&r) &&
        (ibz_cmp(&num2, &ibz_const_zero) < 0) != (ibz_cmp(&denom2, &ibz_const_zero) < 0)) {
        ibz_sub(t, t, &ibz_const_one);
    }

    ibz_finalize(&num2);
    ibz_finalize(&denom2);
    ibz_finalize(&r);
}

/**
 * @brief Check if |q| > 1/2  (strict)
 */
static int
ibq_abs_gt_half(const ibq_t *q)
{
    ibz_t two_num, abs_denom;
    ibz_init(&two_num);
    ibz_init(&abs_denom);
    ibz_abs(&two_num, &((*q)[0]));
    ibz_mul(&two_num, &two_num, &ibz_const_two);
    ibz_abs(&abs_denom, &((*q)[1]));
    int res = (ibz_cmp(&two_num, &abs_denom) > 0);
    ibz_finalize(&two_num);
    ibz_finalize(&abs_denom);
    return res;
}

/* ---------- GS computation for a single position ---------- */

/**
 * @brief Compute Gram-Schmidt coefficients mu[idx][j] and B[idx]
 *        assuming positions 0..idx-1 are already computed.
 */
static void
compute_gs_single(int idx,
                  const ibz_vec_4_t *b,
                  ibq_t mu[][MLLL_MAX_GENERATORS],
                  ibq_t *B,
                  const ibz_t *p)
{
    ibz_t dot_val;
    ibq_t dot_q, prod_q, Binv;
    ibz_init(&dot_val);
    ibq_init(&dot_q);
    ibq_init(&prod_q);
    ibq_init(&Binv);

    /* B[idx] = <b_idx, b_idx>_quat */
    ibz_vec_4_dot_quat(&dot_val, &b[idx], &b[idx], p);
    ibq_set(&B[idx], &dot_val, &ibz_const_one);

    for (int j = 0; j < idx; j++) {
        if (ibq_is_zero(&B[j])) {
            /* B[j] = 0: vector j is dependent, skip */
            ibq_set(&mu[idx][j], &ibz_const_zero, &ibz_const_one);
            continue;
        }

        /* Numerator = <b_idx, b_j> - sum_{k<j} mu[idx][k]*mu[j][k]*B[k] */
        ibz_vec_4_dot_quat(&dot_val, &b[idx], &b[j], p);
        ibq_set(&dot_q, &dot_val, &ibz_const_one);

        for (int k = 0; k < j; k++) {
            if (ibq_is_zero(&B[k]))
                continue;
            ibq_mul(&prod_q, &mu[idx][k], &mu[j][k]);
            ibq_mul(&prod_q, &prod_q, &B[k]);
            ibq_sub(&dot_q, &dot_q, &prod_q);
        }

        /* mu[idx][j] = numerator / B[j] */
        ibq_inv(&Binv, &B[j]);
        ibq_mul(&mu[idx][j], &dot_q, &Binv);
        ibq_reduce(&mu[idx][j]);

        /* B[idx] -= mu[idx][j]^2 * B[j] */
        ibq_mul(&prod_q, &mu[idx][j], &mu[idx][j]);
        ibq_mul(&prod_q, &prod_q, &B[j]);
        ibq_sub(&B[idx], &B[idx], &prod_q);
        ibq_reduce(&B[idx]);
    }

    ibz_finalize(&dot_val);
    ibq_finalize(&dot_q);
    ibq_finalize(&prod_q);
    ibq_finalize(&Binv);
}

/* ========== MLLL core (Algorithm 1 from paper) ========== */

void
quat_mlll(ibz_mat_4x4_t *basis,
          int *rank,
          const ibz_vec_4_t *generators,
          int g,
          const quat_alg_t *alg)
{
    assert(g >= 1 && g <= MLLL_MAX_GENERATORS);

    /* Working arrays (0-indexed) */
    ibz_vec_4_t b[MLLL_MAX_GENERATORS];
    ibq_t mu[MLLL_MAX_GENERATORS][MLLL_MAX_GENERATORS];
    ibq_t B[MLLL_MAX_GENERATORS];

    for (int i = 0; i < g; i++) {
        ibz_vec_4_init(&b[i]);
        ibq_init(&B[i]);
        for (int j = 0; j < g; j++)
            ibq_init(&mu[i][j]);
    }

    int alpha = 0;   /* next generator to load (0-based) */
    int beta = 0;    /* count of working vectors */
    int tau = 1;     /* 0-based LLL reduction start (paper τ=2 → 0-based 1) */
    int m, l;

    /* ===== LOAD: Paper lines 2-3 ===== */
load:
    while (alpha < g) {
        if (ibz_vec_4_is_zero(&generators[alpha])) {
            alpha++;
            continue;
        }

        for (int i = 0; i < 4; i++)
            ibz_copy(&(b[beta][i]), &(generators[alpha][i]));
        alpha++;

        compute_gs_single(beta, b, mu, B, &alg->p);
        beta++;

        /* Paper line 3: if B ≠ 0 and more generators, keep loading */
        if (!ibq_is_zero(&B[beta - 1]) && alpha < g)
            continue;

        break; /* enter reduction */
    }

    if (beta <= 1)
        goto done;

    /* Paper line 4: m ← τ */
    m = tau;
    if (m >= beta) {
        if (alpha < g) {
            tau = beta;
            goto load;
        }
        goto done;
    }

    /* ===== REDUCTION: Paper lines 5-22 ===== */

    /* Paper line 5 */
reduction:
    l = m - 1;

    /* Paper lines 6-9: size-reduce b[m] against b[l] */
size_reduce:
    if (l >= 0 && !ibq_is_zero(&B[l]) && ibq_abs_gt_half(&mu[m][l])) {
        ibz_t t;
        ibq_t t_q, prod_q;
        ibz_init(&t);
        ibq_init(&t_q);
        ibq_init(&prod_q);

        ibq_round(&t, &mu[m][l]);
        ibz_vec_4_sub_scalar_mul(&b[m], &t, &b[l]);

        ibq_set(&t_q, &t, &ibz_const_one);
        ibq_sub(&mu[m][l], &mu[m][l], &t_q);
        ibq_reduce(&mu[m][l]);
        for (int j = 0; j < l; j++) {
            ibq_mul(&prod_q, &t_q, &mu[l][j]);
            ibq_sub(&mu[m][j], &mu[m][j], &prod_q);
            ibq_reduce(&mu[m][j]);
        }

        ibz_finalize(&t);
        ibq_finalize(&t_q);
        ibq_finalize(&prod_q);
    }

    /* Paper line 10: if b_m = 0, remove */
    if (ibz_vec_4_is_zero(&b[m]))
        goto remove_vector;

    /* Paper line 11: if l < m-1, goto Lovász test */
    /* (if l == m-1, also fall through to Lovász test) */

    /* Paper line 13: Lovász condition */
    /* Check: B[m] < (3/4 - mu[m][m-1]^2) * B[m-1] ? */
    if (m > 0 && !ibq_is_zero(&B[m - 1])) {
        ibq_t lhs, rhs, mu_sq, three_quarter;
        ibq_init(&lhs);
        ibq_init(&rhs);
        ibq_init(&mu_sq);
        ibq_init(&three_quarter);

        ibq_copy(&lhs, &B[m]);

        ibz_t four;
        ibz_init(&four);
        ibz_set(&four, 4);
        ibq_set(&three_quarter, &ibz_const_three, &four);
        ibz_finalize(&four);

        ibq_mul(&mu_sq, &mu[m][m - 1], &mu[m][m - 1]);
        ibq_sub(&rhs, &three_quarter, &mu_sq);
        ibq_mul(&rhs, &rhs, &B[m - 1]);

        int need_swap = (ibq_cmp(&lhs, &rhs) < 0);

        ibq_finalize(&lhs);
        ibq_finalize(&rhs);
        ibq_finalize(&mu_sq);
        ibq_finalize(&three_quarter);

        if (need_swap)
            goto do_swap;
    }

    /* Paper lines 14-15 */
    l--;
    if (l >= 0)
        goto size_reduce;
    m++;
    if (m >= beta) {
        if (alpha < g) {
            tau = beta;
            goto load;
        }
        goto done;
    }
    goto reduction;

    /* ===== SWAP: Paper lines 16-22 ===== */
do_swap:
    {
        ibq_t mu_old, B_new;
        ibq_init(&mu_old);
        ibq_init(&B_new);

        ibq_copy(&mu_old, &mu[m][m - 1]);

        /* Paper line 16: B = B[m] + mu^2 * B[m-1] */
        ibq_mul(&B_new, &mu_old, &mu_old);
        ibq_mul(&B_new, &B_new, &B[m - 1]);
        ibq_add(&B_new, &B_new, &B[m]);

        /* Paper line 17: if B = 0, skip GS update */
        if (ibq_is_zero(&B_new))
            goto swap_finish;

        /* Paper lines 18-19: update GS coefficients */
        {
            ibq_t Binv;
            ibq_init(&Binv);
            ibq_inv(&Binv, &B_new);

            /* mu[m][m-1] = mu_old * B[m-1] / B_new */
            ibq_mul(&mu[m][m - 1], &mu_old, &B[m - 1]);
            ibq_mul(&mu[m][m - 1], &mu[m][m - 1], &Binv);
            ibq_reduce(&mu[m][m - 1]);

            /* B[m] = B[m] * B[m-1] / B_new */
            ibq_mul(&B[m], &B[m], &B[m - 1]);
            ibq_mul(&B[m], &B[m], &Binv);
            ibq_reduce(&B[m]);

            ibq_finalize(&Binv);
        }

        /* Paper line 19: update mu[i] for i > m */
        for (int i = m + 1; i < beta; i++) {
            ibq_t old_m1, old_m, prod1;
            ibq_init(&old_m1);
            ibq_init(&old_m);
            ibq_init(&prod1);
            ibq_copy(&old_m1, &mu[i][m - 1]);
            ibq_copy(&old_m, &mu[i][m]);

            /* new mu[i][m] = old_m1 - mu_old * old_m */
            ibq_mul(&prod1, &mu_old, &old_m);
            ibq_sub(&mu[i][m], &old_m1, &prod1);
            ibq_reduce(&mu[i][m]);

            /* new mu[i][m-1] = old_m + mu[m][m-1] * new_mu[i][m] */
            ibq_mul(&prod1, &mu[m][m - 1], &mu[i][m]);
            ibq_add(&mu[i][m - 1], &old_m, &prod1);
            ibq_reduce(&mu[i][m - 1]);

            ibq_finalize(&old_m1);
            ibq_finalize(&old_m);
            ibq_finalize(&prod1);
        }

    swap_finish:
        if (ibq_is_zero(&B_new)) {
            /* B_new=0: GS becomes corrupt after simple swap.
             * Swap vectors only, then recompute GS from m-1. */
            ibz_vec_4_swap(&b[m], &b[m - 1]);

            for (int i = (m - 1 < 0 ? 0 : m - 1); i < beta; i++)
                compute_gs_single(i, b, mu, B, &alg->p);

            ibq_finalize(&mu_old);
            ibq_finalize(&B_new);

            if (m > 1)
                m--;
            goto reduction;
        }

        /* Normal swap: paper lines 20-22 */
        ibq_copy(&B[m - 1], &B_new);

        /* Paper line 21: swap vectors and mu rows */
        ibz_vec_4_swap(&b[m], &b[m - 1]);
        {
            ibq_t tmp_q;
            ibq_init(&tmp_q);
            for (int j = 0; j < m - 1; j++) {
                ibq_copy(&tmp_q, &mu[m][j]);
                ibq_copy(&mu[m][j], &mu[m - 1][j]);
                ibq_copy(&mu[m - 1][j], &tmp_q);
            }
            ibq_finalize(&tmp_q);
        }

        ibq_finalize(&mu_old);
        ibq_finalize(&B_new);
    }

    /* Paper line 22: if m > 1 (0-based), decrement */
    if (m > 1)
        m--;
    goto reduction;

    /* ===== REMOVE: Paper lines 24-27 ===== */
remove_vector:
    /* Paper line 25: shift down from position m */
    for (int i = m; i < beta - 1; i++) {
        for (int c = 0; c < 4; c++)
            ibz_copy(&(b[i][c]), &(b[i + 1][c]));
    }
    beta--;

    /* Paper line 26: if alpha >= g, terminate */
    if (alpha >= g)
        goto done;

    /* Paper line 27: recompute GS from position m, set tau, goto load */
    for (int i = m; i < beta; i++)
        compute_gs_single(i, b, mu, B, &alg->p);

    tau = m + 1; /* Paper: τ ← m+1 (1-based) → 0-based: m+1 */
    if (tau < 1)
        tau = 1;
    goto load;

done:
    /* Final full LLL reduction pass: recompute GS and reduce from scratch.
     * The main loop may leave positions 0..tau-1 unreduced due to tau advancing
     * after removals. A full pass ensures the output is properly LLL-reduced. */
    if (beta > 1) {
        /* Recompute all GS from scratch */
        for (int i = 0; i < beta; i++)
            compute_gs_single(i, b, mu, B, &alg->p);

        int changed = 1;
        while (changed) {
            changed = 0;
            m = 1;
            while (m < beta) {
                /* Size-reduce b[m] against all b[l] for l = m-1 down to 0 */
                for (l = m - 1; l >= 0; l--) {
                    if (ibq_is_zero(&B[l]))
                        continue;
                    if (ibq_abs_gt_half(&mu[m][l])) {
                        ibz_t t;
                        ibq_t t_q, prod_q;
                        ibz_init(&t);
                        ibq_init(&t_q);
                        ibq_init(&prod_q);

                        ibq_round(&t, &mu[m][l]);
                        ibz_vec_4_sub_scalar_mul(&b[m], &t, &b[l]);

                        ibq_set(&t_q, &t, &ibz_const_one);
                        ibq_sub(&mu[m][l], &mu[m][l], &t_q);
                        ibq_reduce(&mu[m][l]);
                        for (int j = 0; j < l; j++) {
                            ibq_mul(&prod_q, &t_q, &mu[l][j]);
                            ibq_sub(&mu[m][j], &mu[m][j], &prod_q);
                            ibq_reduce(&mu[m][j]);
                        }

                        ibz_finalize(&t);
                        ibq_finalize(&t_q);
                        ibq_finalize(&prod_q);
                        changed = 1;
                    }
                }

                /* Remove zero vectors */
                if (ibz_vec_4_is_zero(&b[m])) {
                    for (int i = m; i < beta - 1; i++)
                        for (int c = 0; c < 4; c++)
                            ibz_copy(&(b[i][c]), &(b[i + 1][c]));
                    beta--;
                    /* Recompute GS */
                    for (int i = m; i < beta; i++)
                        compute_gs_single(i, b, mu, B, &alg->p);
                    changed = 1;
                    continue;
                }

                /* Lovász condition */
                if (m > 0 && !ibq_is_zero(&B[m - 1])) {
                    ibq_t lhs, rhs, mu_sq, three_quarter;
                    ibq_init(&lhs);
                    ibq_init(&rhs);
                    ibq_init(&mu_sq);
                    ibq_init(&three_quarter);

                    ibq_copy(&lhs, &B[m]);
                    ibz_t four;
                    ibz_init(&four);
                    ibz_set(&four, 4);
                    ibq_set(&three_quarter, &ibz_const_three, &four);
                    ibz_finalize(&four);

                    ibq_mul(&mu_sq, &mu[m][m - 1], &mu[m][m - 1]);
                    ibq_sub(&rhs, &three_quarter, &mu_sq);
                    ibq_mul(&rhs, &rhs, &B[m - 1]);

                    int need_swap = (ibq_cmp(&lhs, &rhs) < 0);

                    ibq_finalize(&lhs);
                    ibq_finalize(&rhs);
                    ibq_finalize(&mu_sq);
                    ibq_finalize(&three_quarter);

                    if (need_swap) {
                        ibz_vec_4_swap(&b[m], &b[m - 1]);
                        /* Recompute GS from m-1 */
                        for (int i = m - 1; i < beta; i++)
                            compute_gs_single(i, b, mu, B, &alg->p);
                        if (m > 1)
                            m--;
                        changed = 1;
                        continue;
                    }
                }
                m++;
            }
        }
    }

    /* Copy result: basis columns = non-zero b vectors */
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
    for (int i = 0; i < g; i++) {
        ibz_vec_4_finalize(&b[i]);
        ibq_finalize(&B[i]);
        for (int j = 0; j < g; j++)
            ibq_finalize(&mu[i][j]);
    }
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

    /* Compute 16 products: (col_k of lat1) * (col_i of lat2) */
    for (int k = 0; k < 4; k++) {
        ibz_vec_4_copy_ibz(
            &elem1, &(lat1->basis[0][k]), &(lat1->basis[1][k]),
            &(lat1->basis[2][k]), &(lat1->basis[3][k]));
        for (int i = 0; i < 4; i++) {
            ibz_vec_4_copy_ibz(
                &elem2, &(lat2->basis[0][i]), &(lat2->basis[1][i]),
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

    /* Scale lat2 by lat1->denom and lat1 by lat2->denom */
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
