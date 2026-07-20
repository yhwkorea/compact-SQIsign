#include <quaternion.h>
#include <internal.h>
#include "lll_internals.h"

void
quat_lideal_reduce_basis(ibz_mat_4x4_t *reduced,
                         ibz_mat_4x4_t *gram,
                         const quat_left_ideal_t *lideal,
                         const quat_alg_t *alg)
{
    assert(quat_order_is_maximal((lideal->parent_order), alg));
    ibz_t gram_corrector;
    ibz_init(&gram_corrector);
    ibz_mul(&gram_corrector, &(lideal->lattice.denom), &(lideal->lattice.denom));
    quat_lideal_class_gram(gram, lideal, alg);
    ibz_mat_4x4_copy(reduced, &(lideal->lattice.basis));
    quat_lll_core(gram, reduced);
    ibz_mat_4x4_scalar_mul(gram, &gram_corrector, gram);
    for (int i = 0; i < 4; i++) {
        ibz_div_2exp(&((*gram)[i][i]), &((*gram)[i][i]), 1);
        for (int j = i + 1; j < 4; j++) {
            ibz_set(&((*gram)[i][j]), 0);
        }
    }
    ibz_finalize(&gram_corrector);
}

int
quat_lideal_lideal_mul_reduced(quat_left_ideal_t *prod,
                               ibz_mat_4x4_t *gram,
                               const quat_left_ideal_t *lideal1,
                               const quat_left_ideal_t *lideal2,
                               const quat_alg_t *alg)
{
    ibz_mat_4x4_t red;
    ibz_mat_4x4_init(&red);

    /* paper Issue 11/12: this is the bar(J_t)·I (or symmetric variant)
     * ideal multiplication used by SuitableIdeals / IdealToIsogeny.
     * Lemma idealmult-bound predicts max bit < 1024 p^2/π^4 · log p · nrd(I).
     * Trace I/O sizes to verify. */
    {
        int b1 = ibz_bitsize(&(lideal1->lattice.basis[0][0]));
        int b2 = ibz_bitsize(&(lideal2->lattice.basis[0][0]));
        int n1 = ibz_bitsize(&(lideal1->norm));
        int n2 = ibz_bitsize(&(lideal2->norm));
        fprintf(stderr, "[LMUL] in basis1=%d basis2=%d nrd1=%d nrd2=%d\n", b1, b2, n1, n2);
        fflush(stderr);
    }

    if (!quat_lattice_mul(&(prod->lattice),
                          &(lideal1->lattice),
                          &(lideal2->lattice),
                          alg,
                          (&lideal1->norm),
                          &(lideal2->norm))) {
        ibz_mat_4x4_finalize(&red);
        return 0;
    }

    {
        int bp = ibz_bitsize(&(prod->lattice.basis[0][0]));
        int dp = ibz_bitsize(&(prod->lattice.denom));
        fprintf(stderr, "[LMUL] post_mul basis=%d denom=%d\n", bp, dp);
        fflush(stderr);
    }

    prod->parent_order = lideal1->parent_order;
    fprintf(stderr, "[LMUL] pre_norm prod.basis=%d prod.denom=%d\n",
        ibz_bitsize(&(prod->lattice.basis[0][0])), ibz_bitsize(&(prod->lattice.denom))); fflush(stderr);
    quat_lideal_norm(prod);
    fprintf(stderr, "[LMUL] post_norm prod.norm=%d\n", ibz_bitsize(&(prod->norm))); fflush(stderr);
    quat_lideal_reduce_basis(&red, gram, prod, alg);
    fprintf(stderr, "[LMUL] post_reduce_basis red[0][0]=%d\n", ibz_bitsize(&red[0][0])); fflush(stderr);
    ibz_mat_4x4_copy(&(prod->lattice.basis), &red);

    {
        int bf = ibz_bitsize(&(prod->lattice.basis[0][0]));
        int gf = ibz_bitsize(&((*gram)[0][0]));
        fprintf(stderr, "[LMUL] post_LLL basis=%d gram[0][0]=%d\n", bf, gf);
        fflush(stderr);
    }

    ibz_mat_4x4_finalize(&red);
    return 1;
}

int
quat_lideal_prime_norm_reduced_equivalent(quat_left_ideal_t *lideal,
                                          const quat_alg_t *alg,
                                          const int primality_num_iter,
                                          const int equiv_bound_coeff)
{
    ibz_mat_4x4_t gram, red;
    ibz_mat_4x4_init(&gram);
    ibz_mat_4x4_init(&red);

    int found = 0;

    // computing the reduced basis
    quat_lideal_reduce_basis(&red, &gram, lideal, alg);

    quat_alg_elem_t new_alpha;
    quat_alg_elem_init(&new_alpha);
    ibz_t tmp, remainder, adjusted_norm;
    ibz_init(&tmp);
    ibz_init(&remainder);
    ibz_init(&adjusted_norm);

    ibz_mul(&adjusted_norm, &lideal->lattice.denom, &lideal->lattice.denom);

    int ctr = 0;

    // equiv_num_iter = (2 * equiv_bound_coeff + 1)^4
    assert(equiv_bound_coeff < (1 << 20));
    int equiv_num_iter = (2 * equiv_bound_coeff + 1);
    equiv_num_iter = equiv_num_iter * equiv_num_iter;
    equiv_num_iter = equiv_num_iter * equiv_num_iter;

    while (!found && ctr < equiv_num_iter) {
        ctr++;
        // we select our linear combination at random
        ibz_rand_interval_minm_m(&new_alpha.coord[0], equiv_bound_coeff);
        ibz_rand_interval_minm_m(&new_alpha.coord[1], equiv_bound_coeff);
        ibz_rand_interval_minm_m(&new_alpha.coord[2], equiv_bound_coeff);
        ibz_rand_interval_minm_m(&new_alpha.coord[3], equiv_bound_coeff);

        // computation of the norm of the vector sampled
        quat_qf_eval(&tmp, &gram, &new_alpha.coord);

        // compute the norm of the equivalent ideal
        // can be improved by removing the power of two first and the odd part only if the trial
        // division failed (this should always be called on an ideal of norm 2^x * N for some
        // big prime N )
        ibz_div(&tmp, &remainder, &tmp, &adjusted_norm);

        // debug : check that the remainder is zero
        assert(ibz_is_zero(&remainder));

        // pseudo-primality test
        if (ibz_probab_prime(&tmp, primality_num_iter)) {

            // computes the generator using a matrix multiplication
            ibz_mat_4x4_eval(&new_alpha.coord, &red, &new_alpha.coord);
            ibz_copy(&new_alpha.denom, &lideal->lattice.denom);
            assert(quat_lattice_contains(NULL, &lideal->lattice, &new_alpha));

            quat_alg_conj(&new_alpha, &new_alpha);
            ibz_mul(&new_alpha.denom, &new_alpha.denom, &lideal->norm);
            fprintf(stderr, "[PNRE] calling lideal_mul ctr=%d\n", ctr); fflush(stderr);
            quat_lideal_mul(lideal, lideal, &new_alpha, alg);
            /* tmp was accepted as prime above and becomes the norm of the
             * equivalent ideal.  Re-running the randomized primality test in
             * an assert consumed the application DRBG only in Debug builds,
             * making KAT output depend on NDEBUG. */
            assert(ibz_cmp(&lideal->norm, &tmp) == 0);
            fprintf(stderr, "[PNRE] lideal_mul returned; norm preserved; found=1\n"); fflush(stderr);

            found = 1;
            break;
        }
    }
    assert(found);

    ibz_finalize(&tmp);
    ibz_finalize(&remainder);
    ibz_finalize(&adjusted_norm);
    quat_alg_elem_finalize(&new_alpha);

    ibz_mat_4x4_finalize(&gram);
    ibz_mat_4x4_finalize(&red);

    return found;
}
