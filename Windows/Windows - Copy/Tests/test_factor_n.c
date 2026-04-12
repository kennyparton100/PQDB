/**
 * test_factor_n.c — Factor a specific number using the CPSS engine.
 * Build: cl /O2 Tests\test_factor_n.c zlib.lib /Fe:test_factor.exe
 * Run:   test_factor.exe
 */
#include "../CompressedPrimalityStateStreamViewer.c"

int main(void) {
    init_popcount_table();
    init_wheel();

    uint64_t N = 1000000016000000063ULL;
    printf("Factoring: %" PRIu64 "\n", N);
    printf("Bits: %u\n", 60);

    /* Try the auto-router (no DB) */
    printf("\n--- factor-rho ---\n");
    FactorResult fr_rho = cpss_factor_rho(N, 0u, 0u);
    factor_result_print(&fr_rho);

    if (!fr_rho.fully_factored) {
        printf("\n--- factor-fermat ---\n");
        FactorResult fr_fer = cpss_factor_fermat(N, 0u);
        factor_result_print(&fr_fer);
    }

    if (!fr_rho.fully_factored) {
        printf("\n--- factor-squfof ---\n");
        FactorResult fr_sq = cpss_factor_squfof(N);
        factor_result_print(&fr_sq);
    }

    if (!fr_rho.fully_factored) {
        printf("\n--- factor-qs ---\n");
        FactorResult fr_qs = cpss_factor_qs_u64(N);
        factor_result_print(&fr_qs);
    }

    return 0;
}
