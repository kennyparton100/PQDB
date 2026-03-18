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

## Functions Reference (67 built-in)

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

### Combinatorics
| Function | Description |
|----------|-------------|
| `FACTORIAL(n)` | n! (n ≤ 20) |
| `BINOMIAL(n, k)` | C(n, k) |
| `FIBONACCI(n)` | n-th Fibonacci number |

### Digit Operations
| Function | Description |
|----------|-------------|
| `DIGIT_COUNT(n)` | Number of decimal digits |
| `DIGIT_SUM(n)` | Sum of decimal digits |
| `REVERSE_DIGITS(n)` | Reverse decimal digits |
| `IS_PALINDROME(n)` | true if n is a decimal palindrome |

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
`IS_POWERFUL`, `IS_SQUAREFREE`, `IS_SMOOTH(n, B)`

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

## Tips

- **Range is required**: Always use `WHERE ... BETWEEN` or comparison operators to bound your scan.
- **Default limit**: Results are capped at 10,000 rows.
- **No DB needed**: Most functions work without a loaded database. Only `PRIME_COUNT` and `NTH_PRIME` require one.
- **Case insensitive**: Keywords and function names are case-insensitive. `select`, `SELECT`, `Select` all work.
- **Comments**: Use `--` for single-line comments in multi-statement input.
