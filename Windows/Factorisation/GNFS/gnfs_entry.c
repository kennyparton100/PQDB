/** GNFS entry — u64 inputs promoted to BigNum. */
static FactorResult cpss_factor_gnfs_u64(uint64_t n) {
    FactorResult fr;
    factor_result_init(&fr, n, "gnfs(pipeline)");
    BigNum bn; bn_from_u64(&bn, n);
    unsigned bits = 0u; { uint64_t t = n; while (t) { ++bits; t >>= 1; } }
    GNFSConfig cfg; gnfs_config_default(bits, &cfg);
    FactorResultBigNum frbn = cpss_factor_gnfs_pipeline(&bn, &cfg);
    fr.status = frbn.status;
    fr.time_seconds = frbn.time_seconds;
    return fr;
}

/** GNFS entry — BigNum. */
static FactorResultBigNum cpss_factor_gnfs_bn(const BigNum* n) {
    unsigned bits = bn_bitlen(n);
    GNFSConfig cfg; gnfs_config_default(bits, &cfg);
    return cpss_factor_gnfs_pipeline(n, &cfg);
}

/** Print GNFS status (for help/info). */
static void gnfs_print_status(const GNFSConfig* cfg) {
    printf("GNFS status:\n");
    printf("  implementation = Milestone 1 (pipeline validation, no factor extraction)\n");
    printf("  stages: poly_select=YES  sieve=YES(naive)  filter=YES  linalg=YES(dense)  sqrt=NO\n");
    printf("  config: degree=%d  rfb=%d  afb=%d  arange=%d  brange=%d\n",
        cfg->degree, cfg->rfb_bound, cfg->afb_bound, cfg->a_range, cfg->b_range);
}
