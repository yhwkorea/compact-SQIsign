#include <assert.h>
#include <stdio.h>
#include <inttypes.h>
#include <ec.h>

/******************************
Test functions
******************************/

int
inner_test_generated_basis(ec_basis_t *basis, ec_curve_t *curve, unsigned int n)
{
    unsigned int i;
    int PASSED = 1;

    ec_point_t P, Q;
    copy_point(&P, &basis->P);
    copy_point(&Q, &basis->Q);

    // Double points to get point of order 2
    for (i = 0; i < n - 1; i++) {
        xDBL_A24(&P, &P, &curve->A24, curve->is_A24_computed_and_normalized);
        xDBL_A24(&Q, &Q, &curve->A24, curve->is_A24_computed_and_normalized);
    }
    if (ec_is_zero(&P)) {
        printf("Point P generated does not have full order\n");
        PASSED = 0;
    }
    if (ec_is_zero(&Q)) {
        printf("Point Q generated does not have full order\n");
        PASSED = 0;
    }
    if (ec_is_equal(&P, &Q)) {
        printf("Points P, Q are linearly dependent\n");
        PASSED = 0;
    }

    if (!fp2_is_zero(&Q.x)) {
        printf("Points Q is not above the Montgomery point\n");
        PASSED = 0;
    }

    // This should give the identity
    xDBL_A24(&P, &P, &curve->A24, curve->is_A24_computed_and_normalized);
    xDBL_A24(&Q, &Q, &curve->A24, curve->is_A24_computed_and_normalized);
    if (!ec_is_zero(&P)) {
        printf("Point P generated does not have order exactly 2^n\n");
        PASSED = 0;
    }
    if (!ec_is_zero(&Q)) {
        printf("Point Q generated does not have order exactly 2^n\n");
        PASSED = 0;
    }

    if (PASSED == 0) {
        printf("Test failed with n = %u\n", n);
    }

    return PASSED;
}

int
inner_test_hint_basis(ec_basis_t *basis, ec_basis_t *basis_hint)
{
    int PASSED = 1;

    if (!ec_is_equal(&basis->P, &basis_hint->P)) {
        printf("The points P do not match using the hint\n");
        PASSED = 0;
    }

    if (!ec_is_equal(&basis->Q, &basis_hint->Q)) {
        printf("The points Q do not match using the hint\n");
        PASSED = 0;
    }

    if (!ec_is_equal(&basis->PmQ, &basis_hint->PmQ)) {
        printf("The points PmQ do not match using the hint\n");
        PASSED = 0;
    }

    if (PASSED == 0) {
        printf("Test failed\n");
    }

    return PASSED;
}

/******************************
Test wrapper functions
******************************/

int
test_basis_generation_E0(unsigned int n)
{
    ec_basis_t basis, decoded_basis;
    ec_curve_t curve;

    ec_curve_init(&curve);

    // Set a supersingular elliptic curve
    // E : y^2 = x^3 + 6*x^2 + x
    fp2_set_small(&(curve.A), 0);
    fp2_set_one(&(curve.C));
    ec_curve_normalize_A24(&curve);

    // Generate a basis
    uint8_t hint;
    if (!ec_curve_to_basis_2f_to_hint(&basis, &curve, n, &hint)) {
        return 0;
    }

    /* E0 has a single canonical hint.  Mutating the hint byte must never
     * produce an alternate encoding of the same basis. */
    if (hint != 0 ||
        !ec_curve_to_basis_2f_from_hint(&decoded_basis, &curve, n, 0)) {
        return 0;
    }
    for (unsigned int mutated_hint = 1; mutated_hint <= UINT8_MAX; ++mutated_hint) {
        ec_curve_t mutated_curve;
        copy_curve(&mutated_curve, &curve);
        if (ec_curve_to_basis_2f_from_hint(
                &decoded_basis, &mutated_curve, n, (uint8_t)mutated_hint)) {
            return 0;
        }
    }

    // Test result
    return inner_test_generated_basis(&basis, &curve, n) &&
           inner_test_hint_basis(&basis, &decoded_basis);
}

int
test_basis_generation(unsigned int n)
{
    ec_basis_t basis;
    ec_curve_t curve;

    ec_curve_init(&curve);

    // Set a supersingular elliptic curve
    // E : y^2 = x^3 + 6*x^2 + x
    fp2_set_small(&(curve.A), 6);
    fp2_set_one(&(curve.C));
    ec_curve_normalize_A24(&curve);

    // Generate a basis
    uint8_t hint;
    if (!ec_curve_to_basis_2f_to_hint(&basis, &curve, n, &hint)) {
        return 0;
    }

    // Test result
    return inner_test_generated_basis(&basis, &curve, n);
}

int
test_basis_generation_with_hints(unsigned int n)
{
    int check_1, check_2, check_3, rejected_invalid_point = 0;
    ec_basis_t basis, basis_hint;
    ec_curve_t curve;
    ec_curve_init(&curve);

    // Set a supersingular elliptic curve
    // E : y^2 = x^3 + 6*x^2 + x
    fp2_set_small(&(curve.A), 6);
    fp2_set_one(&(curve.C));
    ec_curve_normalize_A24(&curve);

    // Generate a basis with hints
    uint8_t hint;
    if (!ec_curve_to_basis_2f_to_hint(&basis, &curve, n, &hint)) {
        return 0;
    }

    // Ensure the basis from the hint is good
    check_1 = inner_test_generated_basis(&basis, &curve, n);

    // Generate a basis using hints
    check_3 = ec_curve_to_basis_2f_from_hint(&basis_hint, &curve, n, hint);

    // These two bases should be the same
    check_2 = inner_test_hint_basis(&basis, &basis_hint);

    // Flipping the quadratic-character bit selects a construction whose
    // precondition is false.  Attacker-controlled hints must be rejected in
    // both debug and release builds.
    ec_curve_t invalid_hint_curve;
    copy_curve(&invalid_hint_curve, &curve);
    check_3 &= !ec_curve_to_basis_2f_from_hint(&basis_hint, &invalid_hint_curve, n, hint ^ 1);

    /* At least one alternate seven-bit hint with the correct character bit
     * must be rejected because it does not reconstruct a valid full-order
     * basis.  This specifically guards the checks that used to be compiled
     * out under NDEBUG. */
    for (unsigned int candidate = 2U | (hint & 1U); candidate <= UINT8_MAX;
         candidate += 2) {
        if (candidate == hint) {
            continue;
        }
        copy_curve(&invalid_hint_curve, &curve);
        if (!ec_curve_to_basis_2f_from_hint(
                &basis_hint, &invalid_hint_curve, n, (uint8_t)candidate)) {
            rejected_invalid_point = 1;
            break;
        }
    }

    return check_1 && check_2 && check_3 && rejected_invalid_point;
}

/* For A^2 = 2 and z = 1 + i*b, the QR-branch precheck is
 *
 *     A^2*(z - 1) - z^2 = b^2 - 1.
 *
 * This lies in Fp and is therefore always a square in Fp2.  The old zero-hint
 * fallback consequently looped forever on this nonsingular attacker-selected
 * curve.  The bounded decoder must reject it instead. */
int
test_bounded_zero_hint_rejection(unsigned int n)
{
    ec_basis_t basis;
    ec_curve_t curve;
    fp2_t A_squared, two;

    ec_curve_init(&curve);
    fp2_set_small(&curve.A, 2);
    fp_sqrt(&curve.A.re);
    fp2_set_one(&curve.C);

    fp2_sqr(&A_squared, &curve.A);
    fp2_set_small(&two, 2);
    if (!fp2_is_equal(&A_squared, &two)) {
        return 0;
    }

    /* hint_P = 0 requests fallback search; hint_A = 1 selects the QR branch. */
    return !ec_curve_to_basis_2f_from_hint(&basis, &curve, n, 1);
}

int
test_basis(void)
{
    int passed;

    // Test full order
    passed = test_basis_generation(TORSION_EVEN_POWER);
    passed &= test_basis_generation_with_hints(TORSION_EVEN_POWER);

    // Test partial order
    passed &= test_basis_generation(128);
    passed &= test_basis_generation_with_hints(128);

    // Malicious zero-hint fallback must terminate and fail closed.
    passed &= test_bounded_zero_hint_rejection(TORSION_EVEN_POWER);

    // Special case when we have A = 0
    passed &= test_basis_generation_E0(TORSION_EVEN_POWER);
    passed &= test_basis_generation_E0(128);

    return passed;
}

int
main(void)
{
    bool ok;
    ok = test_basis();
    if (!ok) {
        printf("Tests failed!\n");
    } else {
        printf("All basis generation tests passed.\n");
    }
    return !ok;
}
