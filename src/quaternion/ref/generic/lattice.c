#include <quaternion.h>
#include <rng.h>
#include "internal.h"
#include "lll_internals.h" /* for quat_ml2 (paper Issue 8 wire) */

// helper functions
int
quat_lattice_equal(const quat_lattice_t *lat1, const quat_lattice_t *lat2)
{
    int equal = 1;
    quat_lattice_t a, b;
    quat_lattice_init(&a);
    quat_lattice_init(&b);
    quat_lattice_reduce_denom(&a, lat1);
    quat_lattice_reduce_denom(&b, lat2);
    ibz_abs(&(a.denom), &(a.denom));
    ibz_abs(&(b.denom), &(b.denom));
    quat_lattice_hnf(&a);
    quat_lattice_hnf(&b);
    equal = equal && (ibz_cmp(&(a.denom), &(b.denom)) == 0);
    equal = equal && ibz_mat_4x4_equal(&(a.basis), &(b.basis));
    quat_lattice_finalize(&a);
    quat_lattice_finalize(&b);
    return (equal);
}

// sublattice test
int
quat_lattice_inclusion(const quat_lattice_t *sublat, const quat_lattice_t *overlat)
{
    int res;
    quat_lattice_t sum;
    quat_lattice_init(&sum);
    quat_lattice_add(&sum, overlat, sublat);
    res = quat_lattice_equal(&sum, overlat);
    quat_lattice_finalize(&sum);
    return (res);
}

void
quat_lattice_reduce_denom(quat_lattice_t *reduced, const quat_lattice_t *lat)
{
    ibz_t gcd;
    ibz_init(&gcd);
    ibz_mat_4x4_gcd(&gcd, &(lat->basis));
    ibz_gcd(&gcd, &gcd, &(lat->denom));
    ibz_mat_4x4_scalar_div(&(reduced->basis), &gcd, &(lat->basis));
    ibz_div(&(reduced->denom), &gcd, &(lat->denom), &gcd);
    ibz_abs(&(reduced->denom), &(reduced->denom));
    ibz_finalize(&gcd);
}

void
quat_lattice_conjugate_without_hnf(quat_lattice_t *conj, const quat_lattice_t *lat)
{
    ibz_mat_4x4_copy(&(conj->basis), &(lat->basis));
    ibz_copy(&(conj->denom), &(lat->denom));

    for (int row = 1; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            ibz_neg(&(conj->basis[row][col]), &(conj->basis[row][col]));
        }
    }
}

// Method described in https://cseweb.ucsd.edu/classes/sp14/cse206A-a/lec4.pdf consulted on 19 of
// May 2023, 12h40 CEST
void
quat_lattice_dual_without_hnf(quat_lattice_t *dual, const quat_lattice_t *lat)
{
    /* paper Algorithm LatticeDual (91proof.tex:435): (A, Delta) = adj+det,
     * M^# = r * A^T, r^# = Delta, then GCD-normalize (Lines 7-8). The GCD
     * step is essential for the paper bound lem:dual-bound; without it,
     * dual.basis can grow to 4*|entry| bits (cofactor expansion). */
    ibz_mat_4x4_t inv;
    ibz_t det;
    ibz_init(&det);
    ibz_mat_4x4_init(&inv);
    ibz_mat_4x4_inv_with_det_as_denom(&inv, &det, &(lat->basis));
    ibz_mat_4x4_transpose(&inv, &inv);
    // dual_denom = det/lat_denom
    ibz_mat_4x4_scalar_mul(&(dual->basis), &(lat->denom), &inv);
    ibz_copy(&(dual->denom), &det);

    /* paper Line 7-8: GCD normalization */
    quat_lattice_reduce_denom(dual, dual);

    ibz_finalize(&det);
    ibz_mat_4x4_finalize(&inv);
}

void
quat_lattice_add(quat_lattice_t *res, const quat_lattice_t *lat1, const quat_lattice_t *lat2)
{
    ibz_vec_4_t generators[8];
    ibz_mat_4x4_t tmp;
    ibz_t det1, det2, detprod;
    ibz_init(&det1);
    ibz_init(&det2);
    ibz_init(&detprod);
    for (int i = 0; i < 8; i++)
        ibz_vec_4_init(&(generators[i]));
    ibz_mat_4x4_init(&tmp);

    /* Issue 17 trace: log input sizes */
    {
        int max1 = 0, max2 = 0;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                int b1 = ibz_bitsize(&(lat1->basis[i][j]));
                int b2 = ibz_bitsize(&(lat2->basis[i][j]));
                if (b1 > max1) max1 = b1;
                if (b2 > max2) max2 = b2;
            }
        fprintf(stderr, "[LATTICE-ADD] lat1.basis_max=%d lat1.denom=%d lat2.basis_max=%d lat2.denom=%d\n",
            max1, ibz_bitsize(&(lat1->denom)), max2, ibz_bitsize(&(lat2->denom)));
        fflush(stderr);
    }

    ibz_mat_4x4_scalar_mul(&tmp, &(lat1->denom), &(lat2->basis));
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ibz_copy(&(generators[j][i]), &(tmp[i][j]));
        }
    }
    ibz_mat_4x4_inv_with_det_as_denom(NULL, &det1, &tmp);
    ibz_mat_4x4_scalar_mul(&tmp, &(lat2->denom), &(lat1->basis));
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ibz_copy(&(generators[4 + j][i]), &(tmp[i][j]));
        }
    }
    ibz_mat_4x4_inv_with_det_as_denom(NULL, &det2, &tmp);
    assert(!ibz_is_zero(&det1));
    assert(!ibz_is_zero(&det2));
    ibz_gcd(&detprod, &det1, &det2);
    /* paper Issue 8 wire (2026-05-17 04:10 retry with ml2 entry trace).
     * Fallback (2026-05-17 18:xx): if ML2 aborts (oscillation/iter-cap),
     * use the original HNF on the 8 generators. detprod is a multiple of
     * the union lattice volume, suitable as the HNF modulus. */
    {
        ibz_vec_4_t reduced[8];
        for (int i = 0; i < 8; i++)
            ibz_vec_4_init(&reduced[i]);
        int rho = quat_ml2(reduced, 4, generators, 8, NULL);
        if (rho < 0) {
            static int _hnf_fb_n = 0;
            if (_hnf_fb_n++ < 3)
                fprintf(stderr, "[LATTICE-ADD] ML2 abort, HNF fallback (%d)\n", _hnf_fb_n);
            ibz_mat_4xn_hnf_mod_core(&(res->basis), 8, generators, &detprod);
        } else {
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                    ibz_copy(&(res->basis[i][j]), &reduced[j][i]);
        }
        for (int i = 0; i < 8; i++)
            ibz_vec_4_finalize(&reduced[i]);
    }
    ibz_mul(&(res->denom), &(lat1->denom), &(lat2->denom));
    quat_lattice_reduce_denom(res, res);
    ibz_mat_4x4_finalize(&tmp);
    ibz_finalize(&det1);
    ibz_finalize(&det2);
    ibz_finalize(&detprod);
    for (int i = 0; i < 8; i++)
        ibz_vec_4_finalize(&(generators[i]));
}

// method described in https://cseweb.ucsd.edu/classes/sp14/cse206A-a/lec4.pdf consulted on 19 of
// May 2023, 12h40 CEST
void
quat_lattice_intersect(quat_lattice_t *res, const quat_lattice_t *lat1, const quat_lattice_t *lat2)
{
    quat_lattice_t dual1, dual2, dual_res;
    quat_lattice_init(&dual1);
    quat_lattice_init(&dual2);
    quat_lattice_init(&dual_res);
    quat_lattice_dual_without_hnf(&dual1, lat1);

    quat_lattice_dual_without_hnf(&dual2, lat2);
    quat_lattice_add(&dual_res, &dual1, &dual2);
    quat_lattice_dual_without_hnf(res, &dual_res);
    /* paper Issue 8 (2026-05-17): removed the redundant final HNF.
     * The lattice basis is already a valid (non-HNF) basis; consumers via
     * sample_response only need a basis matrix, not a unique form. Original
     * comment in upstream: "could be removed if we do not expect HNF any more". */
    /* quat_lattice_hnf(res); */
    quat_lattice_finalize(&dual1);
    quat_lattice_finalize(&dual2);
    quat_lattice_finalize(&dual_res);
}

void
quat_lattice_mat_alg_coord_mul_without_hnf(ibz_mat_4x4_t *prod,
                                           const ibz_mat_4x4_t *lat,
                                           const ibz_vec_4_t *coord,
                                           const quat_alg_t *alg)
{
    ibz_vec_4_t p, a;
    ibz_vec_4_init(&p);
    ibz_vec_4_init(&a);
    for (int i = 0; i < 4; i++) {
        ibz_vec_4_copy_ibz(&a, &((*lat)[0][i]), &((*lat)[1][i]), &((*lat)[2][i]), &((*lat)[3][i]));
        quat_alg_coord_mul(&p, &a, coord, alg);
        ibz_copy(&((*prod)[0][i]), &(p[0]));
        ibz_copy(&((*prod)[1][i]), &(p[1]));
        ibz_copy(&((*prod)[2][i]), &(p[2]));
        ibz_copy(&((*prod)[3][i]), &(p[3]));
    }
    ibz_vec_4_finalize(&p);
    ibz_vec_4_finalize(&a);
}

void
quat_lattice_alg_elem_mul(quat_lattice_t *prod,
                          const quat_lattice_t *lat,
                          const quat_alg_elem_t *elem,
                          const quat_alg_t *alg)
{
    quat_lattice_mat_alg_coord_mul_without_hnf(&(prod->basis), &(lat->basis), &(elem->coord), alg);
    ibz_mul(&(prod->denom), &(lat->denom), &(elem->denom));
    /* paper Issue 8 extension (replaces quat_lattice_hnf): use ML2 on the
     * 4 column generators to obtain a LLL-reduced basis of the lattice
     * (without cofactor blow-up). Columns of prod->basis ARE the generators. */
    {
        ibz_vec_4_t gens[4], reduced[4];
        for (int i = 0; i < 4; i++) {
            ibz_vec_4_init(&gens[i]);
            ibz_vec_4_init(&reduced[i]);
            for (int j = 0; j < 4; j++)
                ibz_copy(&gens[i][j], &(prod->basis[j][i]));
        }
        int rho = quat_ml2(reduced, 4, gens, 4, alg);
        if (rho >= 4) {
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++)
                    ibz_copy(&(prod->basis[j][i]), &reduced[i][j]);
        } else {
            fprintf(stderr, "[ALG_ELEM_MUL] ML2 returned rho=%d (<4), keeping un-reduced basis\n", rho);
        }
        for (int i = 0; i < 4; i++) {
            ibz_vec_4_finalize(&gens[i]);
            ibz_vec_4_finalize(&reduced[i]);
        }
    }
    quat_lattice_reduce_denom(prod, prod);
}

void
quat_lattice_mul(quat_lattice_t *res,
                 const quat_lattice_t *lat1,
                 const quat_lattice_t *lat2,
                 const quat_alg_t *alg,
                 const ibz_t *norm1,
                 const ibz_t *norm2)
{
    ibz_vec_4_t elem1, elem2, elem_res;
    ibz_vec_4_t generators[16];
    // ibz_mat_4x4_t detmat;
    // ibz_t det;
    ibz_t hnfmod, r1, r2;
    quat_lattice_t lat_res;
    // ibz_init(&det);
    // ibz_mat_4x4_init(&detmat);
    ibz_init(&hnfmod);
    ibz_init(&r1);
    ibz_init(&r2);
    quat_lattice_init(&lat_res);
    ibz_vec_4_init(&elem1);
    ibz_vec_4_init(&elem2);
    ibz_vec_4_init(&elem_res);
    for (int i = 0; i < 16; i++)
        ibz_vec_4_init(&(generators[i]));
    for (int k = 0; k < 4; k++) {
        ibz_vec_4_copy_ibz(
            &elem1, &(lat1->basis[0][k]), &(lat1->basis[1][k]), &(lat1->basis[2][k]), &(lat1->basis[3][k]));
        for (int i = 0; i < 4; i++) {
            ibz_vec_4_copy_ibz(
                &elem2, &(lat2->basis[0][i]), &(lat2->basis[1][i]), &(lat2->basis[2][i]), &(lat2->basis[3][i]));
            quat_alg_coord_mul(&elem_res, &elem1, &elem2, alg);
            for (int j = 0; j < 4; j++) {
                /*if (k == 0)
                    ibz_copy(&(detmat[i][j]), &(elem_res[j]));*/
                ibz_copy(&(generators[4 * k + i][j]), &(elem_res[j]));
            }
        }
    }


    assert(ibz_cmp(norm1, &ibz_const_zero) > 0);
    assert(ibz_cmp(norm2, &ibz_const_zero) > 0);


    /*ibz_mat_4x4_inv_with_det_as_denom(NULL, &det, &detmat);
    ibz_abs(&det, &det);*/
    ibz_mul(&hnfmod, norm1, norm1);
    ibz_mul(&hnfmod, &hnfmod, norm2);
    ibz_mul(&hnfmod, &hnfmod, norm2);
    ibz_mul(&r1, &(lat1->denom), &(lat1->denom));
    ibz_mul(&r1, &r1, &r1);
    ibz_mul(&r2, &(lat2->denom), &(lat2->denom));
    ibz_mul(&r2, &r2, &r2);
    ibz_mul(&hnfmod, &hnfmod, &r1);
    ibz_mul(&hnfmod, &hnfmod, &r2);
    // ibz_t t1, t2;
    // ibz_init(&t1);
    // ibz_init(&t2);

    // // hnfmod = norm1^2 * norm2^2
    // ibz_mul(&t1, norm1, norm1);   // t1 = norm1^2
    // ibz_mul(&t2, norm2, norm2);   // t2 = norm2^2
    // ibz_mul(&hnfmod, &t1, &t2);   // hnfmod = norm1^2 * norm2^2

    // // verify hnfmod > 0 (debug, may remove later)
    // assert(ibz_cmp(&hnfmod, &ibz_const_zero) > 0);

    // ibz_finalize(&t1);
    // ibz_finalize(&t2);

    {
        int gmax = 0;
        for (int g = 0; g < 16; g++)
            for (int c = 0; c < 4; c++) {
                int b = ibz_bitsize(&(generators[g][c]));
                if (b > gmax) gmax = b;
            }
        fprintf(stderr, "[LMUL] enter hnfmod_bits=%d gen_max_bit=%d (transient mul ~= 2*hnfmod = %d)\n",
                ibz_bitsize(&hnfmod), gmax, 2 * ibz_bitsize(&hnfmod));
        fflush(stderr);
    }
    /* paper Issue 11/12 spec: when HNF mod core's m^2 transient would exceed
     * the IBZ_BITS cap, fall through to the spec-faithful "form bar(J_t)·I
     * product, run LLL directly on its 16 column generators" path via
     * ML2(d=16). This matches paper §SuitableIdeals "form ideal product,
     * compute Gram, run LLL" without the canonical-HNF cofactor expansion.
     *
     * At 110-limb (IBZ_BITS=7040), HNF's ~2*hnfmod transient stays well under
     * the cap and HNF is faster + paper-strict for that path — we keep it.
     * At 28-limb (IBZ_BITS=1792), hnfmod ~ 1000 bit yields ~2000 bit transient
     * > cap → silent overflow → hang; route via ML2 instead. ML2(d=16) at
     * 110-limb had a known regression (oscillation in dpe precision), so we
     * only invoke it when forced by the cap. */
    {
        int hnfmod_bits = ibz_bitsize(&hnfmod);
        int safety_margin = 64; /* conservative; xgcd produces u,v slightly larger than m/d */
        int use_ml2 = (2 * hnfmod_bits + safety_margin) > IBZ_BITS;
        if (use_ml2) {
            ibz_vec_4_t reduced[4];
            for (int i = 0; i < 4; i++)
                ibz_vec_4_init(&reduced[i]);
            int rho = quat_ml2(reduced, 4, generators, 16, alg);
            if (rho >= 4) {
                for (int j = 0; j < 4; j++)
                    for (int i = 0; i < 4; i++)
                        ibz_copy(&(res->basis[i][j]), &reduced[j][i]);
                fprintf(stderr, "[LMUL] ML2(d=16) ok, rho=%d (forced: 2*hnfmod=%d > IBZ_BITS=%d)\n",
                        rho, 2 * hnfmod_bits, IBZ_BITS);
                fflush(stderr);
            } else {
                static int _lmul_fb_n = 0;
                if (_lmul_fb_n++ < 3)
                    fprintf(stderr, "[LMUL] ML2(d=16) abort, HNF fallback (%d)\n", _lmul_fb_n);
                fflush(stderr);
                ibz_mat_4xn_hnf_mod_core(&(res->basis), 16, generators, &hnfmod);
            }
            for (int i = 0; i < 4; i++)
                ibz_vec_4_finalize(&reduced[i]);
        } else {
            ibz_mat_4xn_hnf_mod_core(&(res->basis), 16, generators, &hnfmod);
        }
    }
    fprintf(stderr, "[LMUL] reduce returned\n"); fflush(stderr);
    ibz_mul(&(res->denom), &(lat1->denom), &(lat2->denom));
    quat_lattice_reduce_denom(res, res);
    ibz_vec_4_finalize(&elem1);
    ibz_vec_4_finalize(&elem2);
    ibz_vec_4_finalize(&elem_res);
    quat_lattice_finalize(&lat_res);
    // ibz_finalize(&det);
    // ibz_mat_4x4_finalize(&(detmat));
    ibz_finalize(&hnfmod);
    ibz_finalize(&r1);
    ibz_finalize(&r2);
    for (int i = 0; i < 16; i++)
        ibz_vec_4_finalize(&(generators[i]));
}

// lattice assumed of full rank
int
quat_lattice_contains(ibz_vec_4_t *coord, const quat_lattice_t *lat, const quat_alg_elem_t *x)
{
    int divisible = 0;
    ibz_vec_4_t work_coord;
    ibz_mat_4x4_t inv;
    ibz_t det, prod;
    ibz_init(&prod);
    ibz_init(&det);
    ibz_vec_4_init(&work_coord);
    ibz_mat_4x4_init(&inv);
    ibz_mat_4x4_inv_with_det_as_denom(&inv, &det, &(lat->basis));
    assert(!ibz_is_zero(&det));
    ibz_mat_4x4_eval(&work_coord, &inv, &(x->coord));
    ibz_vec_4_scalar_mul(&(work_coord), &(lat->denom), &work_coord);
    ibz_mul(&prod, &(x->denom), &det);
    divisible = ibz_vec_4_scalar_div(&work_coord, &prod, &work_coord);
    // copy result
    if (divisible && (coord != NULL)) {
        for (int i = 0; i < 4; i++) {
            ibz_copy(&((*coord)[i]), &(work_coord[i]));
        }
    }
    ibz_finalize(&prod);
    ibz_finalize(&det);
    ibz_mat_4x4_finalize(&inv);
    ibz_vec_4_finalize(&work_coord);
    return (divisible);
}

void
quat_lattice_index(ibz_t *index, const quat_lattice_t *sublat, const quat_lattice_t *overlat)
{
    ibz_t tmp, det;
    ibz_init(&tmp);
    ibz_init(&det);

    // det = det(sublat->basis)
    ibz_mat_4x4_inv_with_det_as_denom(NULL, &det, &sublat->basis);
    // tmp = (overlat->denom)⁴
    ibz_mul(&tmp, &overlat->denom, &overlat->denom);
    ibz_mul(&tmp, &tmp, &tmp);
    // index = (overlat->denom)⁴ · det(sublat->basis)
    ibz_mul(index, &det, &tmp);
    // tmp = (sublat->denom)⁴
    ibz_mul(&tmp, &sublat->denom, &sublat->denom);
    ibz_mul(&tmp, &tmp, &tmp);
    // det = det(overlat->basis)
    ibz_mat_4x4_inv_with_det_as_denom(NULL, &det, &overlat->basis);
    // tmp = (sublat->denom)⁴ · det(overlat->basis)
    ibz_mul(&tmp, &tmp, &det);
    // index = index / tmp
    ibz_div(index, &tmp, index, &tmp);
    assert(ibz_is_zero(&tmp));
    // index = |index|
    ibz_abs(index, index);

    ibz_finalize(&tmp);
    ibz_finalize(&det);
}

void
quat_lattice_hnf(quat_lattice_t *lat)
{
    ibz_t mod;
    ibz_vec_4_t generators[4];
    ibz_init(&mod);
    ibz_mat_4x4_inv_with_det_as_denom(NULL, &mod, &(lat->basis));
    ibz_abs(&mod, &mod);
    for (int i = 0; i < 4; i++)
        ibz_vec_4_init(&(generators[i]));
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ibz_copy(&(generators[j][i]), &(lat->basis[i][j]));
        }
    }
    ibz_mat_4xn_hnf_mod_core(&(lat->basis), 4, generators, &mod);
    quat_lattice_reduce_denom(lat, lat);
    ibz_finalize(&mod);
    for (int i = 0; i < 4; i++)
        ibz_vec_4_finalize(&(generators[i]));
}

void
quat_lattice_gram(ibz_mat_4x4_t *G, const quat_lattice_t *lattice, const quat_alg_t *alg)
{
    ibz_t tmp;
    ibz_init(&tmp);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j <= i; j++) {
            ibz_set(&(*G)[i][j], 0);
            for (int k = 0; k < 4; k++) {
                ibz_mul(&tmp, &(lattice->basis)[k][i], &(lattice->basis)[k][j]);
                if (k >= 2)
                    ibz_mul(&tmp, &tmp, &alg->p);
                ibz_add(&(*G)[i][j], &(*G)[i][j], &tmp);
            }
            ibz_mul(&(*G)[i][j], &(*G)[i][j], &ibz_const_two);
        }
    }
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            ibz_copy(&(*G)[i][j], &(*G)[j][i]);
        }
    }
    ibz_finalize(&tmp);
}
