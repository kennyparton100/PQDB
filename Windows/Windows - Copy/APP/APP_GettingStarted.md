# APP Getting Started

This guide is the shortest path from `cpss_viewer` to a useful APP session.

For the full guide map, start at [APP_Guide.md](APP_Guide.md).

---

## APP Mode vs One-Shot CLI

Use APP mode when you want a persistent shell:

- keep a database loaded
- run many related commands in sequence
- explore spaces and semiprime reports interactively
- use NQL inside the shell

Use one-shot CLI commands when you only need file inspection or conversion:

- `info`
- `list`
- `export-primes`
- `clip`
- `split-trillion`
- `decompress-file`
- `compress-file`

Those commands are covered in [APP_CLIUtilitiesGuide.md](APP_CLIUtilitiesGuide.md).

---

## Launching APP Mode

Minimal startup:

```text
cpss_viewer APP
```

With cache and threading options:

```text
cpss_viewer APP --wheel-cache 256MB --hot-cache 1GB --db-ram 8GB --threads 8
```

### Startup options

- `--wheel-cache <size>`: budget for wheel cache entries
- `--hot-cache <size>`: budget for decompressed hot segment cache
- `--db-ram <size>`: RAM ceiling for DB-backed loading
- `--threads <N>`: thread count used by threaded workloads

At startup the shell prints the active cache budgets and reminds you to use `help`.

---

## First Commands to Run

```text
> help
> help db
> help query
> help factor
> help space
```

These are the most useful discovery commands:

- `help` for the top-level category index
- `help <topic>` for category-specific command help
- `space-list [family]` to list available spaces
- `space-regions` to list valid prime-pair region types
- `space-metrics [family] <name>` to inspect metric definitions

---

## Loading Data

Most APP work begins with a database load.

Single-stream load from the decompressed 0-to-1-trillion stream:

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode performance
```

- `benchmark` is the recommended mode for normal development work.
- `performance` is the code default if `--mode` is omitted, but it is a specialist high-RAM mode.
- A practical rule is to reserve `performance` for machines that can spare well over 4x the DB file size in RAM alone.

Sharded database load:

```text
> load-db shards/
```

Inspect the current state:

```text
> db-status
> cache-status
```

Database loading, sidecar tools, and cache management are covered in [APP_DatabaseGuide.md](APP_DatabaseGuide.md).

---

## Your First Useful Session

```text
cpss_viewer APP --hot-cache 512MB
```

```text
> help
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> db-status
> is-prime 1000003
> factor 143
> space-list
> space value diagonal 4 8
> query SELECT p FROM PRIME WHERE p BETWEEN 1 AND 50
```

This touches the main APP areas in order:

- help and discovery
- database loading
- direct query commands
- factorisation
- space navigation
- NQL

---

## Which Commands Need a DB?

Does **not** require a DB first:

- `load`
- `load-db`
- `db-status` (but it is most useful after loading)
- `is-prime-bigint`
- `factor-rho`
- `factor-fermat`
- many ECM / BigNum fallback paths
- `query` syntax parsing itself, though DB-backed NQL functions still need a loaded DB

Usually **yes**:

- `preload`
- most prime query commands such as `next-prime`, `pi`, `nth-prime`
- DB-backed factor methods such as `factor-trial`, `factor-pminus1`, `factor-pplus1`
- prime-pair region commands such as `space`, `space-compare`, `space-stats`, `space-export`
- `corpus-validate`, DB-aware evaluation runs, and many benchmark commands

If in doubt, use `help <topic>` and look for wording such as `require loaded DB`.

---

## Exiting APP Mode

```text
> exit
```

or

```text
> quit
```

---

## Where to Go Next

- Queries: [APP_QueryGuide.md](APP_QueryGuide.md)
- Factorisation: [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
- Prime-pair spaces: [APP_SpaceGuide.md](APP_SpaceGuide.md)
- Semiprime analysis: [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md)
- NQL: [../NQL/NQL_UserGuide.md](../NQL/NQL_UserGuide.md)
