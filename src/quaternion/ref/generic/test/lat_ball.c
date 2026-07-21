#include "quaternion_tests.h"
#include <stdlib.h>

/* Regression for the DPE inverse-diagonal underbound found at the fixed lvl1
 * width.  The old code returned K-19591 for the coordinate whose exact
 * capacity is K.  Check capacity after the new helper's unimodular basis
 * change, rather than assuming that L2 keeps the long basis vector in column
 * zero. */
static int
quat_test_lat_ball_large_coordinate_bound(void)
{
    int res = 0;
    quat_alg_t alg;
    quat_lattice_t lattice;
    ibz_mat_4x4_t G, U, inv_U;
    ibz_vec_4_t box, x, reduced_x, mapped_x;
    ibz_t a, K, radius, det, norm, abs_coord;

    quat_alg_init_set_ui(&alg, 11);
    quat_lattice_init(&lattice);
    ibz_mat_4x4_init(&G);
    ibz_mat_4x4_init(&U);
    ibz_mat_4x4_init(&inv_U);
    ibz_vec_4_init(&box);
    ibz_vec_4_init(&x);
    ibz_vec_4_init(&reduced_x);
    ibz_vec_4_init(&mapped_x);
    ibz_init(&a);
    ibz_init(&K);
    ibz_init(&radius);
    ibz_init(&det);
    ibz_init(&norm);
    ibz_init(&abs_coord);

    res |= !ibz_set_from_str(&a, "192984485", 10);
    res |= !ibz_set_from_str(&K, "248120864280296583124", 10);
    ibz_mat_4x4_zero(&lattice.basis);
    ibz_copy(&lattice.basis[0][0], &a);
    ibz_copy(&lattice.basis[1][1], &ibz_const_one);
    ibz_copy(&lattice.basis[2][2], &ibz_const_one);
    ibz_copy(&lattice.basis[3][3], &ibz_const_one);
    ibz_copy(&lattice.denom, &ibz_const_one);

    quat_lattice_gram(&G, &lattice, &alg);
    ibz_mul(&radius, &G[0][0], &K);
    ibz_mul(&radius, &radius, &K);
    ibz_copy(&x[0], &K);
    quat_qf_eval(&norm, &G, &x);
    res |= ibz_cmp(&norm, &radius) != 0;

    if (!quat_lattice_bound_parallelogram(&box, &U, &G, &radius) ||
        !ibz_mat_4x4_inv_with_det_as_denom(&inv_U, &det, &U)) {
        res = 1;
    } else {
        ibz_abs(&abs_coord, &det);
        res |= !ibz_is_one(&abs_coord);
        /* U^t*reduced_x=x, hence reduced_x=U^-t*x.  inv_U is
         * det(U)*U^-1, with det(U)=+/-1. */
        ibz_mat_4x4_eval_t(&reduced_x, &x, &inv_U);
        if (ibz_is_negative(&det))
            ibz_vec_4_negate(&reduced_x, &reduced_x);
        ibz_mat_4x4_eval_t(&mapped_x, &reduced_x, &U);
        for (int i = 0; i < 4; i++) {
            ibz_abs(&abs_coord, &reduced_x[i]);
            res |= ibz_cmp(&abs_coord, &box[i]) > 0;
            res |= ibz_cmp(&mapped_x[i], &x[i]) != 0;
        }
    }

    /* Floor of the real orthogonal bound is intentional: integer back
     * substitution adds ceil(eta*tail).  Here t0=sqrt(399/400)<1, so the
     * direct floor for coordinate zero is 0, yet the boundary point (-1,1)
     * has norm exactly 399 and must be recovered through the tail term.
     * This catches replacing that tail ceil by inward rounding. */
    ibz_mat_4x4_zero(&G);
    ibz_set(&G[0][0], 400);
    ibz_set(&G[0][1], 200);
    ibz_set(&G[1][0], 200);
    ibz_set(&G[1][1], 399);
    ibz_set(&G[2][2], 400);
    ibz_set(&G[3][3], 400);
    ibz_set(&radius, 399);
    for (int i = 0; i < 4; i++) {
        ibz_set(&x[i], 0);
        ibz_set(&reduced_x[i], 0);
        ibz_set(&mapped_x[i], 0);
    }
    ibz_set(&x[0], -1);
    ibz_set(&x[1], 1);
    quat_qf_eval(&norm, &G, &x);
    res |= ibz_cmp(&norm, &radius) != 0;
    if (!quat_lattice_bound_parallelogram(&box, &U, &G, &radius) ||
        !ibz_mat_4x4_inv_with_det_as_denom(&inv_U, &det, &U)) {
        res = 1;
    } else {
        ibz_mat_4x4_eval_t(&reduced_x, &x, &inv_U);
        if (ibz_is_negative(&det))
            ibz_vec_4_negate(&reduced_x, &reduced_x);
        ibz_mat_4x4_eval_t(&mapped_x, &reduced_x, &U);
        for (int i = 0; i < 4; i++) {
            ibz_abs(&abs_coord, &reduced_x[i]);
            res |= ibz_cmp(&abs_coord, &box[i]) > 0;
            res |= ibz_cmp(&mapped_x[i], &x[i]) != 0;
        }
    }

    /* With G=2I and radius=1, the origin is the only integral point.  Since
     * sampling deliberately excludes it, the bound helper must fail
     * immediately rather than hand the caller a nonempty rejection box. */
    ibz_mat_4x4_zero(&G);
    for (int i = 0; i < 4; i++)
        ibz_copy(&G[i][i], &ibz_const_two);
    ibz_copy(&radius, &ibz_const_one);
    res |= quat_lattice_bound_parallelogram(&box, &U, &G, &radius);

    /* Singular input is rejected before L2 performs a floating division. */
    ibz_mat_4x4_zero(&G);
    res |= quat_lattice_bound_parallelogram(&box, &U, &G, &ibz_const_one);

    ibz_finalize(&abs_coord);
    ibz_finalize(&norm);
    ibz_finalize(&det);
    ibz_finalize(&radius);
    ibz_finalize(&K);
    ibz_finalize(&a);
    ibz_vec_4_finalize(&mapped_x);
    ibz_vec_4_finalize(&reduced_x);
    ibz_vec_4_finalize(&x);
    ibz_vec_4_finalize(&box);
    ibz_mat_4x4_finalize(&inv_U);
    ibz_mat_4x4_finalize(&U);
    ibz_mat_4x4_finalize(&G);
    quat_lattice_finalize(&lattice);
    quat_alg_finalize(&alg);

    if (res != 0)
        printf("Quaternion unit test lat_ball_large_coordinate_bound failed\n");
    return res;
}

// int quat_lattice_bound_parallelogram(ibz_vec_4_t *box, ibz_mat_4x4_t *U, const ibz_mat_4x4_t *G,
// const ibz_t *radius);
int
quat_test_lat_ball_paralellogram_randomized(int iterations, int bitsize)
{
    int res = 0;

    quat_lattice_t lattice;
    ibz_t radius, length, tmp;
    ibz_mat_4x4_t U, G;
    ibz_vec_4_t box, dbox, x;
    quat_lattice_init(&lattice);
    ibz_vec_4_init(&box);
    ibz_vec_4_init(&dbox);
    ibz_vec_4_init(&x);
    ibz_mat_4x4_init(&U);
    ibz_mat_4x4_init(&G);
    ibz_init(&radius);
    ibz_init(&length);
    ibz_init(&tmp);

    for (int it = 0; it < iterations; it++) {
        // Create a random positive definite quadratic form.
        if (quat_test_input_random_lattice_generation(&lattice, bitsize, 1, 0) != 0) {
            res = 1;
            break;
        }
        ibz_mat_4x4_transpose(&G, &lattice.basis);
        ibz_mat_4x4_mul(&G, &G, &lattice.basis);

        // Set radius to 2 × sqrt(lattice volume).
        if (!ibz_mat_4x4_inv_with_det_as_denom(NULL, &radius, &(lattice.basis))) {
            res = 1;
            break;
        }
        ibz_abs(&radius, &radius);
        ibz_sqrt_floor(&radius, &radius);
        ibz_mul(&radius, &radius, &ibz_const_two);

        if (!quat_lattice_bound_parallelogram(&box, &U, &G, &radius)) {
            res = 1;
            break;
        }
        for (int i = 0; i < 4; i++) {
            // dbox is a box with sides dbox[i] =  2*box[i] + 1
            ibz_add(&dbox[i], &box[i], &box[i]);
            ibz_add(&dbox[i], &dbox[i], &ibz_const_one);
            // initialize x[i] to the bottom of dbox[i]
            ibz_neg(&x[i], &dbox[i]);
        }

        // Integrate U into the Gram matrix
        ibz_mat_4x4_mul(&G, &U, &G);
        ibz_mat_4x4_transpose(&U, &U);
        ibz_mat_4x4_mul(&G, &G, &U);

        // Treat x[0]...x[3] as a complete mixed-radix counter.  Earlier code
        // stopped x[0] at +1 and unintentionally skipped the positive outer
        // slab; this exhaustive loop checks both signs explicitly.
        while (1) {
            int in_box = 1;
            for (int i = 0; i < 4; i++) {
                ibz_abs(&tmp, &x[i]);
                in_box &= ibz_cmp(&tmp, &box[i]) <= 0;
            }
            if (!in_box) {
                quat_qf_eval(&length, &G, &x);
                if (ibz_cmp(&length, &radius) <= 0) {
                    res = 1;
                    break;
                }
            }

            int carry = 1;
            for (int i = 0; carry && i < 4; i++) {
                ibz_add(&x[i], &x[i], &ibz_const_one);
                if (ibz_cmp(&x[i], &dbox[i]) > 0) {
                    ibz_neg(&x[i], &dbox[i]);
                } else {
                    carry = 0;
                }
            }
            if (carry)
                break;
        }
        if (res != 0)
            break;
    }

    quat_lattice_finalize(&lattice);
    ibz_vec_4_finalize(&box);
    ibz_vec_4_finalize(&dbox);
    ibz_vec_4_finalize(&x);
    ibz_mat_4x4_finalize(&U);
    ibz_mat_4x4_finalize(&G);
    ibz_finalize(&radius);
    ibz_finalize(&length);
    ibz_finalize(&tmp);

    if (res != 0) {
        printf("Quaternion unit test lat_ball_paralellogram_randomized failed\n");
    }
    return (res);
}

// helper which tests quat_lattice_sample_from_ball on given input
int
quat_test_lat_ball_sample_helper(const quat_lattice_t *lat, const ibz_t *radius, const quat_alg_t *alg)
{
    int res = 0;
    quat_alg_elem_t vec;
    ibz_t norm_d, norm_n;
    ibz_init(&norm_d);
    ibz_init(&norm_n);
    quat_alg_elem_init(&vec);
    // check return value
    res |= !quat_lattice_sample_from_ball(&vec, lat, alg, radius);
    // check result is in lattice
    res |= !quat_lattice_contains(NULL, lat, &vec);
    quat_alg_norm(&norm_n, &norm_d, &vec, alg);
    // test that n/d <= r so that n <= rd
    ibz_mul(&norm_d, &norm_d, radius);
    res |= !(ibz_cmp(&norm_n, &norm_d) <= 0);

    ibz_finalize(&norm_d);
    ibz_finalize(&norm_n);
    quat_alg_elem_finalize(&vec);
    return res;
}

// int quat_lattice_sample_from_ball(ibz_vec_4_t *x, const quat_lattice_t *lattice, const quat_alg_t
// *alg, const ibz_t *radius);
int
quat_test_lat_ball_sample_from_ball()
{
    int res = 0;

    ibz_t norm_n, norm_d;
    quat_alg_t alg;
    quat_alg_elem_t vec;
    quat_lattice_t lattice;
    ibz_t radius;

    quat_alg_init_set_ui(&alg, 11);
    ibz_init(&norm_n);
    ibz_init(&norm_d);
    quat_lattice_init(&lattice);
    ibz_init(&radius);
    quat_alg_elem_init(&vec);

    for (int it = 0; it < 3; it++) {
        if (it == 0) {
            ibz_mat_4x4_identity(&(lattice.basis)); // Test inner product
        } else if (it == 1) {
            ibz_set(&lattice.denom, 13); // Test with denominator
        } else {                         // if (it == 2)
            // Test a very much non-orthogonal qf
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++)
                    ibz_add(&(lattice.basis[i][j]), &(lattice.basis[i][j]), &ibz_const_one);
        }

        for (int i = 0; i < 100; i++) {
            ibz_set(&radius, i + 1);
            res |= quat_test_lat_ball_sample_helper(&lattice, &radius, &alg);
            if (res != 0)
                break;
        }
    }

    /* Force the post-bound quadratic-form capacity preflight to fail: the
     * radius itself fits, but the complete reduced box's corner bound does
     * not.  Failure must not be reported as success and must not publish a
     * partial result. */
    ibz_mat_4x4_identity(&lattice.basis);
    ibz_copy(&lattice.denom, &ibz_const_one);
    ibz_copy(&radius, &ibz_const_one);
    ibz_mul_2exp(&radius, &radius, IBZ_BITS - 5);
    ibz_set(&vec.coord[0], 7);
    ibz_set(&vec.coord[1], -3);
    ibz_set(&vec.denom, 5);
    res |= quat_lattice_sample_from_ball(&vec, &lattice, &alg, &radius);
    res |= ibz_cmp_int32(&vec.coord[0], 7) != 0;
    res |= ibz_cmp_int32(&vec.coord[1], -3) != 0;
    res |= ibz_cmp_int32(&vec.denom, 5) != 0;

    if (res != 0) {
        printf("Quaternion unit test lat_ball_sample_from_ball failed\n");
    }

    quat_alg_finalize(&alg);
    quat_lattice_finalize(&lattice);
    ibz_finalize(&radius);
    quat_alg_elem_finalize(&vec);
    ibz_finalize(&norm_n);
    ibz_finalize(&norm_d);
    return (res);
}

int
quat_test_lat_ball_sample_from_ball_randomized(int iterations, int bitsize)
{
    int res = 0;

    ibz_t norm_n, norm_d;
    quat_alg_t alg;
    quat_alg_elem_t vec;
    quat_lattice_t *lattices;
    ibz_t radius;

    quat_alg_init_set_ui(&alg, 11);
    ibz_init(&norm_n);
    ibz_init(&norm_d);
    lattices = malloc(iterations * sizeof(quat_lattice_t));
    for (int i = 0; i < iterations; i++)
        quat_lattice_init(&(lattices[i]));
    ibz_init(&radius);
    quat_alg_elem_init(&vec);

    int randret = quat_test_input_random_lattice_generation(lattices, bitsize, iterations, 0);

    if (!randret) {
        for (int i = 0; i < iterations; i++) {
#ifndef NDEBUG

            int ok = ibz_mat_4x4_inv_with_det_as_denom(NULL, &radius, &(lattices[i].basis));
            assert(ok);
#else
            (void)ibz_mat_4x4_inv_with_det_as_denom(NULL, &radius, &(lattices[i].basis));
#endif
            ibz_abs(&radius, &radius);
            res |= quat_test_lat_ball_sample_helper(&(lattices[i]), &radius, &alg);
            if (res != 0)
                break;
        }
    }

    if (res != 0) {
        printf("Quaternion unit test lat_ball_sample_from_ball_randomized failed\n");
    }

    quat_alg_finalize(&alg);
    for (int i = 0; i < iterations; i++)
        quat_lattice_finalize(&(lattices[i]));
    free(lattices);
    ibz_finalize(&radius);
    quat_alg_elem_finalize(&vec);
    ibz_finalize(&norm_n);
    ibz_finalize(&norm_d);
    return (res);
}

// run all previous tests
int
quat_test_lat_ball(void)
{
    int res = 0;
    printf("\nRunning quaternion tests for sampling lattice points of bounded norm\n");
    res = res | quat_test_lat_ball_large_coordinate_bound();
    res = res | quat_test_lat_ball_sample_from_ball();
    res = res | quat_test_lat_ball_sample_from_ball_randomized(100, 10);
    res = res | quat_test_lat_ball_paralellogram_randomized(100, 100);
    return (res);
}
