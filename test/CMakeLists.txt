# SPDX-License-Identifier: Apache-2.0

FOREACH(SVARIANT ${SVARIANT_S})

    string(TOLOWER ${SVARIANT} SVARIANT_LOWER)
    string(TOUPPER ${SVARIANT} SVARIANT_UPPER)

    # Stored vectors are deterministic regression fixtures, not an independent
    # correctness oracle.  They are opt-in, and the legacy NIST-v2 vectors are
    # intentionally not registered because Compact key generation has a
    # different random transcript.
    if (ENABLE_KAT_TESTS)
        add_executable(sqisign_test_kat_${SVARIANT} test_kat.c)
        target_link_libraries(sqisign_test_kat_${SVARIANT} sqisign_${SVARIANT_LOWER}_test)
        target_include_directories(sqisign_test_kat_${SVARIANT} PRIVATE ${PROJECT_SOURCE_DIR}/src/nistapi/${SVARIANT_LOWER} ${INC_PUBLIC} ${INC_INTBIG} ${INC_PRECOMP_${SVARIANT_UPPER}} ${INC_QUATERNION} ${INC_KLPT} ${INC_GF_${SVARIANT_UPPER}} ${INC_EC} ${INC_COMMON} ${INC_ID2ISO} ${INC_PROTOCOLS})

        if (SVARIANT_LOWER STREQUAL "lvl1")
            set(KAT_SECRETKEY_BYTES 353)
        elseif (SVARIANT_LOWER STREQUAL "lvl3")
            set(KAT_SECRETKEY_BYTES 529)
        elseif (SVARIANT_LOWER STREQUAL "lvl5")
            set(KAT_SECRETKEY_BYTES 701)
        else()
            message(FATAL_ERROR "Unknown SQIsign variant: ${SVARIANT}")
        endif()

        set(KAT_FILENAME "PQCsignKAT_${KAT_SECRETKEY_BYTES}_SQIsign_${SVARIANT_LOWER}.rsp")
        set(KAT_COMPACT_FILE "${PROJECT_SOURCE_DIR}/KAT/compact-v1/${KAT_FILENAME}")

        add_test(NAME sqisign_${SVARIANT}_COMPACT_VECTOR_VERIFY
            COMMAND sqisign_test_kat_${SVARIANT}
                --file ${KAT_COMPACT_FILE} --mode verify-only --expected-vectors 1)
        set_tests_properties(sqisign_${SVARIANT}_COMPACT_VECTOR_VERIFY
            PROPERTIES LABELS "regression;vectors" TIMEOUT 1800)

        # Byte-for-byte replay is intentionally not registered: Compact ML2
        # and order-relative ideal construction changed the deterministic key
        # transcript. Stored-signature verification remains a useful regression.
    endif()

    if (ENABLE_SIGN)

        add_executable(sqisign_test_scheme_${SVARIANT} test_sqisign.c)
        # Use the deterministic test RNG so --seed is reproducible.  The
        # separate thread-safety tests deliberately link the system RNG.
        target_link_libraries(sqisign_test_scheme_${SVARIANT} sqisign_${SVARIANT_LOWER}_test)
        target_include_directories(sqisign_test_scheme_${SVARIANT} PUBLIC ${PROJECT_SOURCE_DIR}/src/common ${INC_PUBLIC} ${PROJECT_SOURCE_DIR}/src/nistapi/${SVARIANT_LOWER})
        add_test(NAME sqisign_${SVARIANT}_CORRECTNESS
            COMMAND sqisign_test_scheme_${SVARIANT} --seed=1 --msglen=33)
        set_tests_properties(sqisign_${SVARIANT}_CORRECTNESS
            PROPERTIES LABELS "correctness;scheme;slow" TIMEOUT 7200)

    endif()

ENDFOREACH()
