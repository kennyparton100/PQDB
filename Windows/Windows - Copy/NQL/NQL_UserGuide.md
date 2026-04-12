# NQL User Guide

A complete guide to using the Number Query Language — SQL for numbers.

---

## Getting Started

NQL runs inside the CPSS APP mode. Start the app, then prefix any NQL statement with `query`:

```
> query SELECT p FROM PRIME WHERE p BETWEEN 1 AND 50
> query 2 + 3
> query FACTORISE(600851475143)
```

## Basic Queries

### Enumerate numbers from a type
```sql
SELECT p FROM PRIME WHERE p BETWEEN 1 AND 100
SELECT n FROM FIBONACCI WHERE n BETWEEN 1 AND 1000
SELECT n FROM PALINDROME WHERE n BETWEEN 100 AND 999
```

### Count and aggregate
```sql
SELECT COUNT(*) FROM PRIME WHERE p <= 1000000
SELECT SUM(p) FROM PRIME WHERE p BETWEEN 1 AND 100
SELECT MIN(p), MAX(p), AVG(p) FROM PRIME WHERE p BETWEEN 1 AND 1000
```

### Compute expressions per row
```sql
SELECT n, n * n AS square, FACTORISE(n) FROM N WHERE n BETWEEN 1 AND 20
SELECT p, EULER_TOTIENT(p) FROM PRIME WHERE p BETWEEN 1 AND 50
SELECT n, DIVISOR_SUM(n), NUM_DIVISORS(n) FROM N WHERE n BETWEEN 1 AND 30
```

---

## Number Types (FROM clause)

| Type | Column | Description |
|------|--------|-------------|
| `N` | n | All natural numbers ≥ 1 |
| `PRIME` | p | Prime numbers |
| `COMPOSITE` | n | Composite numbers |
| `SEMIPRIME` | N | Products of exactly 2 primes |
| `EVEN` | n | Even numbers |
| `ODD` | n | Odd numbers |
| `PERFECT_POWER` | n | Numbers n = a^k, k ≥ 2 |
| `TWIN_PRIME` | p | Primes p where p+2 is also prime |
| `MERSENNE` | p | Mersenne primes (2^k − 1) |
| `FIBONACCI` | n | Fibonacci numbers |
| `PALINDROME` | n | Decimal palindromes |
| `SQUARE` | n | Perfect squares |
| `CUBE` | n | Perfect cubes |
| `POWERFUL` | n | All prime factors p satisfy p² ∣ n |
| `SQUAREFREE` | n | No repeated prime factors |
| `SMOOTH(B)` | n | All prime factors ≤ B |
| `RANGE(lo, hi)` | n | Integers from lo to hi |
| `CARMICHAEL` | n | Carmichael numbers (Korselt’s criterion) |
| `CUBAN_PRIME` | p | Primes of the form 3n² + 3n + 1 |

---

## Conditionals

### IF expression
```sql
SELECT n, IF(IS_PRIME(n), 'prime', 'composite') FROM N WHERE n BETWEEN 2 AND 20
```

### CASE expression
```sql
SELECT n, CASE
    WHEN IS_PRIME(n) THEN 'prime'
    WHEN IS_SEMIPRIME(n) THEN 'semiprime'
    ELSE 'other'
END AS classification
FROM N WHERE n BETWEEN 2 AND 30
```

---

## Functions Reference (228 built-in)

### Primality
| Function | Description |
|----------|-------------|
| `IS_PRIME(n)` | true if n is prime |
| `NEXT_PRIME(n)` | Smallest prime > n |
| `PREV_PRIME(n)` | Largest prime < n |
| `PRIME_GAP(p)` | Gap to next prime after p |

### Counting (requires loaded DB)
| Function | Description |
|----------|-------------|
| `PRIME_COUNT(n)` | Count of primes ≤ n |
| `NTH_PRIME(k)` | The k-th prime (1-indexed) |

### Factoring
| Function | Description |
|----------|-------------|
| `FACTORISE(n)` | Factorisation as string: "2^3 * 3 * 5" |
| `SMALLEST_FACTOR(n)` | Smallest prime factor |
| `LARGEST_FACTOR(n)` | Largest prime factor |
| `DISTINCT_PRIME_FACTORS(n)` | ω(n): count of distinct prime factors |
| `PRIME_FACTOR_COUNT(n)` | Ω(n): count with multiplicity |
| `RADICAL(n)` | Product of distinct prime factors |
| `SOPFR(n)` | Sum of prime factors with repetition |

### Divisors
| Function | Description |
|----------|-------------|
| `DIVISORS(n)` | Comma-separated list of divisors |
| `NUM_DIVISORS(n)` | τ(n): number of divisors |
| `DIVISOR_SUM(n)` | σ(n): sum of divisors |

### Classical Number Theory
| Function | Description |
|----------|-------------|
| `EULER_TOTIENT(n)` | φ(n): Euler's totient |
| `MOBIUS(n)` | μ(n): Möbius function (−1, 0, 1) |
| `CARMICHAEL_LAMBDA(n)` | λ(n): Carmichael function |
| `LIOUVILLE(n)` | λ(n): Liouville function (−1)^Ω(n) |

### Group Theory
| Function | Description |
|----------|-------------|
| `MULTIPLICATIVE_ORDER(a, n)` | Smallest k > 0 with a^k ≡ 1 (mod n) |
| `PRIMITIVE_ROOT(p)` | Smallest primitive root modulo p |
| `PRIMITIVE_ROOT_COUNT(p)` | Number of primitive roots modulo p |

### Analytic Number Theory
| Function | Description |
|----------|-------------|
| `CHEBYSHEV_THETA(x)` | θ(x) = Σ log(p) for primes p ≤ x |
| `CHEBYSHEV_PSI(x)` | ψ(x) = Σ log(p) for prime powers p^k ≤ x |

### Modular Arithmetic
| Function | Description |
|----------|-------------|
| `POWMOD(base, exp, mod)` | base^exp mod m (fast) |
| `MODINV(a, m)` | Modular inverse of a mod m |
| `JACOBI(a, n)` | Jacobi symbol (a/n) |

### Arithmetic
| Function | Description |
|----------|-------------|
| `GCD(a, b)` | Greatest common divisor |
| `LCM(a, b)` | Least common multiple |
| `INT_SQRT(n)` | Integer square root |
| `POW(a, b)` | a^b (integer) |
| `MOD(a, b)` | a mod b |
| `ABS(n)` | Absolute value |
| `LEAST(a, b)` | Smaller of two values |
| `GREATEST(a, b)` | Larger of two values |

### Real Math
| Function | Description |
|----------|-------------|
| `SQRT(x)` | Floating-point square root |
| `LN(x)` | Natural logarithm |
| `LOG2(x)` | Base-2 logarithm |
| `LOG10(x)` | Base-10 logarithm |
| `FLOOR(x)` | Floor |
| `CEIL(x)` | Ceiling |
| `ROUND(x)` | Round to nearest integer |
| `SIGN(x)` | −1, 0, or 1 |

### Trigonometry
| Function | Description |
|----------|-------------|
| `SIN(x)` | Sine (radians) |
| `COS(x)` | Cosine (radians) |
| `TAN(x)` | Tangent (radians) |
| `ASIN(x)` | Arc sine |
| `ACOS(x)` | Arc cosine |
| `ATAN(x)` | Arc tangent |
| `ATAN2(y, x)` | Two-argument arc tangent |
| `SINH(x)` | Hyperbolic sine |
| `COSH(x)` | Hyperbolic cosine |
| `TANH(x)` | Hyperbolic tangent |
| `DEGREES(x)` | Radians to degrees |
| `RADIANS(x)` | Degrees to radians |

### Exponential / Constants
| Function | Description |
|----------|-------------|
| `EXP(x)` | e^x |
| `LOG(x, base)` | Logarithm base b |
| `PI()` | π constant |
| `E()` | Euler's number |

### Combinatorics
| Function | Description |
|----------|-------------|
| `FACTORIAL(n)` | n! (n ≤ 20) |
| `BINOMIAL(n, k)` | C(n, k) |
| `FIBONACCI(n)` | n-th Fibonacci number |

### Polynomial Arithmetic
| Function | Description |
|----------|-------------|
| `POLY(c0, c1, ..., cn)` | Create polynomial c0 + c1·x + ... + cn·x^n |
| `POLY_FROM_ROOTS(r1, r2, ...)` | Create (x−r1)(x−r2)... |
| `POLY_DEGREE(p)` | Degree of polynomial |
| `POLY_LEADING_COEFFICIENT(p)` | Leading coefficient |
| `POLY_COEFFICIENT(p, i)` | Coefficient of x^i |
| `POLY_EVALUATE(p, x)` | Evaluate p(x) at integer x |
| `POLY_EVALUATE_MOD(p, x, m)` | p(x) mod m |
| `POLY_ADD(p, q)` | Addition |
| `POLY_SUBTRACT(p, q)` | Subtraction |
| `POLY_MULTIPLY(p, q)` | Multiplication |
| `POLY_DIVIDE(p, q)` | Quotient |
| `POLY_REMAINDER(p, q)` | Remainder |
| `POLY_GCD(p, q)` | Polynomial GCD |
| `POLY_DERIVATIVE(p)` | Formal derivative |
| `POLY_ROOTS_MOD(p, prime)` | All roots mod prime |
| `POLY_RESULTANT(f, g)` | Resultant |
| `POLY_DISCRIMINANT(f)` | Discriminant |

### Digit Operations
| Function | Description |
|----------|-------------|
| `DIGIT_COUNT(n)` | Number of decimal digits |
| `DIGIT_SUM(n)` | Sum of decimal digits |
| `REVERSE_DIGITS(n)` | Reverse decimal digits |
| `IS_PALINDROME(n)` | true if n is a decimal palindrome |
| `DIGITAL_ROOT(n)` | Iterated digit sum to single digit |

### Bitwise
| Function | Description |
|----------|-------------|
| `BIT_AND(a, b)` | Bitwise AND |
| `BIT_OR(a, b)` | Bitwise OR |
| `BIT_XOR(a, b)` | Bitwise XOR |
| `BIT_NOT(a)` | Bitwise NOT |
| `BIT_SHIFT_LEFT(a, k)` | Left shift by k |
| `BIT_SHIFT_RIGHT(a, k)` | Right shift by k |
| `BIT_LENGTH(n)` | Number of bits |

### Type Predicates
`IS_COMPOSITE`, `IS_SEMIPRIME`, `IS_EVEN`, `IS_ODD`, `IS_PERFECT_POWER`,
`IS_TWIN_PRIME`, `IS_MERSENNE`, `IS_FIBONACCI`, `IS_SQUARE`, `IS_CUBE`,
`IS_POWERFUL`, `IS_SQUAREFREE`, `IS_SMOOTH(n, B)`, `IS_CARMICHAEL(n)`,
`IS_PRIMITIVE_ROOT(g, p)`, `IS_PERFECT_SQUARE(n)`

### Method-Specific Factoring
| Function | Description |
|----------|-------------|
| `FACTOR_FERMAT(n [, max_steps])` | Fermat difference-of-squares |
| `FACTOR_RHO(n [, max_iter, seed])` | Pollard rho |
| `FACTOR_SQUFOF(n)` | Shanks' SQUFOF |
| `FACTOR_ECM(n [, B1, curves])` | ECM (BigNum-aware) |
| `TRIAL_SMALL_FACTOR(N, bound)` | Small factor check up to bound |

### Utility / Conversion
| Function | Description |
|----------|-------------|
| `EXTENDED_GCD(a, b)` | Returns ARRAY(gcd, x, y) where ax+by=gcd |
| `CHINESE_REMAINDER(rems, mods)` | CRT solver (array inputs) |
| `SIGMA_K(n, k)` | Generalised divisor function σ_k(n) |
| `LUCAS_V(a, e, n)` | Lucas sequence V_e(a) mod n |
| `NTH_ROOT(n, k)` | floor(n^(1/k)) for BigNum |
| `RANDOM(lo, hi)` | Random integer in [lo, hi] |
| `RANDOM_FLOAT()` | Random float in [0, 1) |
| `TO_STRING(x)`, `TO_INT(x)`, `TO_FLOAT(x)` | Type conversions |
| `TIMER_START()`, `TIMER_ELAPSED(t)` | High-resolution timing |

### File I/O
| Function | Description |
|----------|-------------|
| `FILE_WRITE_LINES(filename, array)` | Write array as lines to file |
| `FILE_READ_LINES(filename)` | Read lines from file into array |
| `FILE_APPEND_LINE(filename, value)` | Append one line to file |

### GNFS Functions
| Function | Description |
|----------|-------------|
| `GNFS_BASE_M_SELECT(N, d)` | floor(N^(1/d)) for polynomial selection |
| `GNFS_BASE_M_POLY(N, d)` | Generate polynomial pair [f, g, m] |
| `GNFS_POLY_ALPHA(f [, bound])` | Alpha-value (polynomial quality) |
| `GNFS_POLY_SKEWNESS(f)` | Optimal skewness estimate |
| `GNFS_POLY_ROTATE(f, g, j0 [, j1])` | Polynomial rotation |
| `GNFS_RATIONAL_NORM(a, b, m)` | \|a - b·m\| as BigNum |
| `GNFS_ALGEBRAIC_NORM(a, b, f)` | Algebraic norm as BigNum |
| `GNFS_ALGEBRAIC_NORM_MOD(a, b, f, p)` | Algebraic norm mod p (fast) |
| `GNFS_SIEVE_REGION_CREATE(w, h)` | Create 2D sieve region |
| `GNFS_SIEVE_SET_SPECIAL_Q(region, q, r)` | Set special-q with lattice reduction |
| `GNFS_SIEVE_INIT_NORMS(region, f, g, m)` | Fill sieve with log-norm estimates |
| `GNFS_SIEVE_RUN_SIDE(region, primes, roots, side)` | Lattice-projected sieve |
| `GNFS_SIEVE_CANDIDATES(region, rt, at)` | Scan for smooth (a,b) pairs |
| `GNFS_TRIAL_FACTOR_RATIONAL(a, b, m, primes)` | Trial-factor rational norm |
| `GNFS_TRIAL_FACTOR_ALGEBRAIC(a, b, f, primes, roots)` | Trial-factor algebraic norm |
| `GNFS_RELATION_CREATE(rfc, afc [, max])` | Create relation store |
| `GNFS_RELATION_ADD(rels, a, b, rat_exp, alg_exp [, rlp, alp])` | Add relation |
| `GNFS_FILTER_SINGLETONS(rels)` | Remove singleton LP partials |
| `GNFS_RELATION_EXPORT(rels, file)` | Export to file |
| `GNFS_SPARSE_MATRIX_BUILD(rows, cols)` | Build CSR sparse matrix |
| `GNFS_BLOCK_LANCZOS(matrix)` | Solve null space via Block Lanczos |
| `GNFS_EXTRACT_RATIONAL_SQRT(dep, a, b, m, N)` | Rational side extraction |
| `GNFS_ALGEBRAIC_SQRT(dep, rels, rp, ap, N)` | Algebraic side sqrt |
| `GNFS_EXTRACT_TRY_FACTOR(x, y, N)` | GCD(x±y, N) |

---

## Imperative Scripting

NQL supports imperative control flow for algorithms and research workflows:

```sql
-- FOR loop
FOR i FROM 1 TO 100 DO
    IF IS_PRIME(i) THEN PRINT i END IF
END FOR

-- WHILE loop
LET n = 1000
WHILE n > 1 DO
    LET f = SMALLEST_FACTOR(n)
    PRINT f
    LET n = n / f
END WHILE

-- FOR EACH over arrays
LET primes = ARRAY(2, 3, 5, 7, 11)
FOR EACH p IN primes DO
    PRINT p, ': gap = ', PRIME_GAP(p)
END FOR

-- ASSERT for testing
ASSERT IS_PRIME(97), '97 should be prime'
ASSERT GCD(12, 18) = 6
```

---

## Variables

```sql
LET x = 600851475143
FACTORISE(x)
LET target = 1000
SELECT COUNT(*) FROM PRIME WHERE p <= target
```

Variables persist for the session and can be used in any subsequent query.

---

## User-Defined Functions

```sql
CREATE FUNCTION collatz_step(n) = IF(n % 2 = 0, n / 2, 3 * n + 1)
CREATE FUNCTION triangle(n) = n * (n + 1) / 2
CREATE FUNCTION is_even(n) = (n % 2 = 0)
```

Use them like built-in functions:
```sql
SELECT n, collatz_step(n) FROM N WHERE n BETWEEN 1 AND 20
SELECT n, triangle(n) FROM N WHERE n BETWEEN 1 AND 10
```

---

## Multi-Statement

Separate statements with semicolons:
```sql
CREATE FUNCTION f(n) = n * n + 1; SELECT n, f(n), IS_PRIME(f(n)) FROM N WHERE n BETWEEN 1 AND 20
```

---

## WITH RECURSIVE

Express iterative algorithms:
```sql
WITH RECURSIVE seq AS (
    SELECT 1 AS n
    UNION ALL
    SELECT n + 1 FROM seq WHERE n < 20
)
SELECT * FROM seq
```

---

## Set Operations

```sql
SELECT p FROM PRIME WHERE p < 100
INTERSECT
SELECT n FROM PALINDROME WHERE n < 100
```

Result: primes that are also palindromes (2, 3, 5, 7, 11).

```sql
SELECT p FROM PRIME WHERE p < 50
EXCEPT
SELECT p FROM TWIN_PRIME WHERE p < 50
```

Result: primes that are NOT twin primes.

---

## Subqueries

```sql
SELECT AVG(gap) FROM (
    SELECT LEAD(p) - p AS gap FROM PRIME WHERE p BETWEEN 1 AND 1000
)
```

---

## Window Functions

```sql
SELECT p, LAG(p) AS prev, p - LAG(p) AS gap
FROM PRIME WHERE p BETWEEN 900 AND 1000
```

---

## EXISTS

```sql
SELECT n FROM N WHERE n BETWEEN 4 AND 100
  AND NOT EXISTS(SELECT p FROM PRIME WHERE p = n)
```

---

## Ordering and Limits

```sql
SELECT n, NUM_DIVISORS(n) AS d FROM N WHERE n BETWEEN 1 AND 100
ORDER BY d DESC LIMIT 10
```

---

## Wide Arithmetic (U128 / BigNum)

NQL automatically promotes to wider types when integer literals exceed 64-bit range.

### Automatic promotion rules

| Literal range | Stored as | Arithmetic tier |
|---|---|---|
| Fits in int64 (≤ 9223372036854775807) | `int64` | 64-bit signed |
| Fits in U128 (≤ 2^128 − 1) | `U128` | 128-bit unsigned |
| Larger | `BigNum` | Up to 4096-bit |

### Examples

```sql
-- U128 arithmetic (auto-promoted because literal > INT64_MAX)
query 18446744073709551616 + 1
query 340282366920938463463374607431768211455 / 3

-- BigNum arithmetic with existing functions
query IS_PRIME(340282366920938463463374607431768211507)
query POWMOD(2, 128, 340282366920938463463374607431768211507)

-- Mixed-width: narrow values auto-promote to match the widest operand
query 42 + 18446744073709551616
```

When one operand is wider than the other, the narrow value is promoted automatically. The result type is the narrowest type that fits the answer.

---

## Tips

- **Range is required**: Always use `WHERE ... BETWEEN` or comparison operators to bound your scan.
- **Default limit**: Results are capped at 10,000 rows.
- **No DB needed**: Most functions work without a loaded database. Only `PRIME_COUNT` and `NTH_PRIME` require one.
- **Case insensitive**: Keywords and function names are case-insensitive. `select`, `SELECT`, `Select` all work.
- **Comments**: Use `--` for single-line comments in multi-statement input.

---

## Related Guides

- [Documentation Index](../Documentation.md)
- [NQL Technical Documentation](NQL_Documentation.md)
- [APP Query Guide](../APP/APP_QueryGuide.md)
