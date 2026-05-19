#include <quaternion.h>
#include <rng.h>
#include <stdio.h>
#include "internal.h"
#include "lll_internals.h"
#include "dpe.h"

/* paper Issue 17/Gap-L: ibz_mat_4x4_inv_with_det_as_denom (cofactor) produces
 * up to ~4*K bit transients (K = gram entry bit). At lvl1 K~648 -> 2592 bit
 * transient, overflowing 28-limb (1792 bit). Use dpe (53-bit float + arbitrary
 * exponent) for the inv-derived diagonals so we avoid the overflow entirely.
 * For LLL-reduced gram, the axis-aligned bound box[i] = floor(sqrt((G^-1)_ii * r))
 * with U = identity is correct (slightly looser than LLL-of-dual, but rejection
 * sampling still converges because off-diagonals of G are small). */
static int
bound_parallelogram_dpe(ibz_vec_4_t *box, ibz_mat_4x4_t *U, const ibz_mat_4x4_t *G, const ibz_t *radius)
{
    dpe_t Gd[4][4], rd, det, cof, box_sq;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            dpe_init(Gd[i][j]);
            dpe_set_z(Gd[i][j], &(*G)[i][j]);
        }
    dpe_init(rd);
    dpe_set_z(rd, radius);
    dpe_init(det);
    dpe_init(cof);
    dpe_init(box_sq);

    /* full 4x4 det via cofactor expansion along row 0 */
    {
        dpe_t a, b, c, d, e, f, t1, t2, m;
        dpe_init(a); dpe_init(b); dpe_init(c); dpe_init(d); dpe_init(e); dpe_init(f);
        dpe_init(t1); dpe_init(t2); dpe_init(m);
        dpe_set_d(det, 0.0);
        for (int j = 0; j < 4; j++) {
            int cols[3], k = 0;
            for (int jj = 0; jj < 4; jj++) if (jj != j) cols[k++] = jj;
            /* 3x3 det of rows 1,2,3 with columns cols[0..2] */
            dpe_mul(t1, Gd[2][cols[1]], Gd[3][cols[2]]);
            dpe_mul(t2, Gd[2][cols[2]], Gd[3][cols[1]]);
            dpe_sub(m, t1, t2);
            dpe_mul(a, Gd[1][cols[0]], m);

            dpe_mul(t1, Gd[2][cols[0]], Gd[3][cols[2]]);
            dpe_mul(t2, Gd[2][cols[2]], Gd[3][cols[0]]);
            dpe_sub(m, t1, t2);
            dpe_mul(b, Gd[1][cols[1]], m);

            dpe_mul(t1, Gd[2][cols[0]], Gd[3][cols[1]]);
            dpe_mul(t2, Gd[2][cols[1]], Gd[3][cols[0]]);
            dpe_sub(m, t1, t2);
            dpe_mul(c, Gd[1][cols[2]], m);

            dpe_sub(a, a, b);
            dpe_add(a, a, c);
            dpe_mul(a, a, Gd[0][j]);
            if (j & 1) dpe_sub(det, det, a);
            else dpe_add(det, det, a);
        }
        dpe_clear(a); dpe_clear(b); dpe_clear(c); dpe_clear(d); dpe_clear(e); dpe_clear(f);
        dpe_clear(t1); dpe_clear(t2); dpe_clear(m);
    }

    int trivial = 1;
    ibz_t box_sq_ibz, abs_box;
    ibz_init(&box_sq_ibz);
    ibz_init(&abs_box);

    /* For each i: cof = det of 3x3 submatrix with row i, col i removed,
     * box[i]² = cof * radius / det,  box[i] = floor(sqrt(box[i]²)). */
    for (int i = 0; i < 4; i++) {
        int rows[3], cols[3], rk = 0, ck = 0;
        for (int x = 0; x < 4; x++) if (x != i) { rows[rk++] = x; cols[ck++] = x; }
        dpe_t t1, t2, m, A, B, C;
        dpe_init(t1); dpe_init(t2); dpe_init(m);
        dpe_init(A); dpe_init(B); dpe_init(C);

        dpe_mul(t1, Gd[rows[1]][cols[1]], Gd[rows[2]][cols[2]]);
        dpe_mul(t2, Gd[rows[1]][cols[2]], Gd[rows[2]][cols[1]]);
        dpe_sub(m, t1, t2);
        dpe_mul(A, Gd[rows[0]][cols[0]], m);

        dpe_mul(t1, Gd[rows[1]][cols[0]], Gd[rows[2]][cols[2]]);
        dpe_mul(t2, Gd[rows[1]][cols[2]], Gd[rows[2]][cols[0]]);
        dpe_sub(m, t1, t2);
        dpe_mul(B, Gd[rows[0]][cols[1]], m);

        dpe_mul(t1, Gd[rows[1]][cols[0]], Gd[rows[2]][cols[1]]);
        dpe_mul(t2, Gd[rows[1]][cols[1]], Gd[rows[2]][cols[0]]);
        dpe_sub(m, t1, t2);
        dpe_mul(C, Gd[rows[0]][cols[2]], m);

        dpe_sub(A, A, B);
        dpe_add(cof, A, C);

        dpe_mul(box_sq, cof, rd);
        dpe_div(box_sq, box_sq, det);

        if (dpe_cmp_d(box_sq, 0.0) > 0) {
            dpe_get_z(&box_sq_ibz, box_sq);
            ibz_abs(&abs_box, &box_sq_ibz);
            ibz_sqrt_floor(&(*box)[i], &abs_box);
        } else {
            ibz_set(&(*box)[i], 0);
        }
        trivial &= ibz_is_zero(&(*box)[i]);
        dpe_clear(t1); dpe_clear(t2); dpe_clear(m);
        dpe_clear(A); dpe_clear(B); dpe_clear(C);
    }

    ibz_finalize(&box_sq_ibz);
    ibz_finalize(&abs_box);

    /* U = identity (skip LLL-of-dual step; axis-aligned bound in G coords) */
    ibz_mat_4x4_identity(U);

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            dpe_clear(Gd[i][j]);
    dpe_clear(rd); dpe_clear(det); dpe_clear(cof); dpe_clear(box_sq);

    fprintf(stderr, "[BOUND-DPE] box.bits: %d %d %d %d  trivial=%d\n",
        ibz_bitsize(&(*box)[0]), ibz_bitsize(&(*box)[1]),
        ibz_bitsize(&(*box)[2]), ibz_bitsize(&(*box)[3]), trivial);
    fflush(stderr);
    return !trivial;
}

int
quat_lattice_bound_parallelogram(ibz_vec_4_t *box, ibz_mat_4x4_t *U, const ibz_mat_4x4_t *G, const ibz_t *radius)
{
    ibz_t denom, rem;
    ibz_init(&denom);
    ibz_init(&rem);
    ibz_mat_4x4_t dualG;
    ibz_mat_4x4_init(&dualG);

// Compute the Gram matrix of the dual lattice
#ifndef NDEBUG
    int inv_check = ibz_mat_4x4_inv_with_det_as_denom(&dualG, &denom, G);
    assert(inv_check);
#else
    (void)ibz_mat_4x4_inv_with_det_as_denom(&dualG, &denom, G);
#endif
    // Initialize the dual lattice basis to the identity matrix
    ibz_mat_4x4_identity(U);
    // Reduce the dual lattice
    quat_lll_core(&dualG, U);

    /* R8 trace: dualG diagonal + denom + product right before sqrt */
    fprintf(stderr, "[BOUND-P] denom.bits=%d radius.bits=%d  dualG[0..3][i,i]: %d %d %d %d\n",
        ibz_bitsize(&denom), ibz_bitsize(radius),
        ibz_bitsize(&dualG[0][0]), ibz_bitsize(&dualG[1][1]),
        ibz_bitsize(&dualG[2][2]), ibz_bitsize(&dualG[3][3]));
    fflush(stderr);
    // Compute the parallelogram's bounds
    int trivial = 1;
    for (int i = 0; i < 4; i++) {
        ibz_mul(&(*box)[i], &dualG[i][i], radius);
        int pre_div = ibz_bitsize(&(*box)[i]);
        ibz_div(&(*box)[i], &rem, &(*box)[i], &denom);
        int post_div = ibz_bitsize(&(*box)[i]);
        ibz_sqrt_floor(&(*box)[i], &(*box)[i]);
        fprintf(stderr, "[BOUND-P] i=%d  dualG[i,i]*rad.bits=%d  /denom.bits=%d  sqrt=%d\n",
            i, pre_div, post_div, ibz_bitsize(&(*box)[i]));
        fflush(stderr);
        trivial &= ibz_is_zero(&(*box)[i]);
    }

    // Compute the transpose transformation matrix
#ifndef NDEBUG
    int inv = ibz_mat_4x4_inv_with_det_as_denom(U, &denom, U);
#else
    (void)ibz_mat_4x4_inv_with_det_as_denom(U, &denom, U);
#endif
    // U is unitary, det(U) = ± 1
    ibz_mat_4x4_scalar_mul(U, &denom, U);
#ifndef NDEBUG
    assert(inv);
    ibz_abs(&denom, &denom);
    assert(ibz_is_one(&denom));
#endif

    ibz_mat_4x4_finalize(&dualG);
    ibz_finalize(&denom);
    ibz_finalize(&rem);
    return !trivial;
}

int
quat_lattice_sample_from_ball(quat_alg_elem_t *res,
                              const quat_lattice_t *lattice,
                              const quat_alg_t *alg,
                              const ibz_t *radius)
{
    assert(ibz_cmp(radius, &ibz_const_zero) > 0);

    ibz_vec_4_t box;
    ibz_vec_4_init(&box);
    ibz_mat_4x4_t U, G;
    ibz_mat_4x4_init(&U);
    ibz_mat_4x4_init(&G);
    ibz_vec_4_t x;
    ibz_vec_4_init(&x);
    ibz_t rad, tmp;
    ibz_init(&rad);
    ibz_init(&tmp);

    // Compute the Gram matrix of the lattice
    quat_lattice_gram(&G, lattice, alg);

    // Correct ball radius by the denominator
    ibz_mul(&rad, radius, &lattice->denom);
    ibz_mul(&rad, &rad, &lattice->denom);
    // Correct by 2 (Gram matrix corresponds to twice the norm)
    ibz_mul(&rad, &rad, &ibz_const_two);

    // Compute a bounding parallelogram for the ball using dpe-based diagonal
    // estimate (avoids 4K-bit transients of integer 4x4 inv at narrow IBZ_LIMBS).
    int ok = bound_parallelogram_dpe(&box, &U, &G, &rad);
    if (!ok)
        goto err;

    /* R8 trace: log box/rad entry */
    {
        int box_max = 0;
        for (int i = 0; i < 4; i++) {
            int b = ibz_bitsize(&box[i]);
            if (b > box_max) box_max = b;
        }
        int U_max = 0;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
            int b = ibz_bitsize(&U[i][j]);
            if (b > U_max) U_max = b;
        }
        fprintf(stderr, "[BALL] enter rejection loop: box_max=%d rad=%d U_max=%d\n",
            box_max, ibz_bitsize(&rad), U_max);
        fflush(stderr);
    }
    // Rejection sampling from the parallelogram
    int cnt = 0;
    int tmp_max_seen = 0;
    do {
        // Sample vector
        for (int i = 0; i < 4; i++) {
            if (ibz_is_zero(&box[i])) {
                ibz_copy(&x[i], &ibz_const_zero);
            } else {
                ibz_add(&tmp, &box[i], &box[i]);
                ok &= ibz_rand_interval(&x[i], &ibz_const_zero, &tmp);
                ibz_sub(&x[i], &x[i], &box[i]);
                if (!ok)
                    goto err;
            }
        }
        // Map to parallelogram
        ibz_mat_4x4_eval_t(&x, &x, &U);
        // Evaluate quadratic form
        quat_qf_eval(&tmp, &G, &x);
        int tb = ibz_bitsize(&tmp);
        if (tb > tmp_max_seen) tmp_max_seen = tb;
        cnt++;
        if (cnt == 100 || cnt == 1000 || cnt == 10000 || cnt == 100000 || (cnt % 1000000 == 0)) {
            fprintf(stderr, "[BALL] reject=%d tmp_max_seen=%d (rad=%d)\n",
                cnt - 1, tmp_max_seen, ibz_bitsize(&rad));
            fflush(stderr);
        }
        if (cnt > 5000000) {
            fprintf(stderr, "[BALL] ABORT after 5M rejections (likely infinite loop)\n");
            fflush(stderr);
            ok = 0;
            goto err;
        }
    } while (ibz_is_zero(&tmp) || (ibz_cmp(&tmp, &rad) > 0));
    fprintf(stderr, "[BALL] accepted after %d rejects, final tmp=%d\n", cnt - 1, ibz_bitsize(&tmp));
    fflush(stderr);

    // Evaluate linear combination
    ibz_mat_4x4_eval(&(res->coord), &(lattice->basis), &x);
    ibz_copy(&(res->denom), &(lattice->denom));
    quat_alg_normalize(res);

#ifndef NDEBUG
    // Check norm is smaller than radius
    quat_alg_norm(&tmp, &rad, res, alg);
    ibz_mul(&rad, &rad, radius);
    assert(ibz_cmp(&tmp, &rad) <= 0);
#endif

err:
    ibz_finalize(&rad);
    ibz_finalize(&tmp);
    ibz_vec_4_finalize(&x);
    ibz_mat_4x4_finalize(&U);
    ibz_mat_4x4_finalize(&G);
    ibz_vec_4_finalize(&box);
    return ok;
}
