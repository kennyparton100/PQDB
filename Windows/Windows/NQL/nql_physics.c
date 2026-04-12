/**
 * nql_physics.c - NQL Physics primitives.
 * Part of the CPSS Viewer amalgamation.
 *
 * Physical constants, unit conversions, coordinate transforms,
 * kinematics, gravitational/electromagnetic calculations.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ======================================================================
 * PHYSICAL CONSTANTS (CODATA 2018)
 * ====================================================================== */

/** PHYS_CONST(name) -- returns named physical constant */
static NqlValue nql_fn_phys_const(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* name = args[0].v.sval;
    if (strcmp(name, "c") == 0 || strcmp(name, "speed_of_light") == 0) return nql_val_float(299792458.0);
    if (strcmp(name, "G") == 0 || strcmp(name, "gravitational") == 0) return nql_val_float(6.67430e-11);
    if (strcmp(name, "h") == 0 || strcmp(name, "planck") == 0) return nql_val_float(6.62607015e-34);
    if (strcmp(name, "hbar") == 0) return nql_val_float(1.054571817e-34);
    if (strcmp(name, "e") == 0 || strcmp(name, "elementary_charge") == 0) return nql_val_float(1.602176634e-19);
    if (strcmp(name, "k_B") == 0 || strcmp(name, "boltzmann") == 0) return nql_val_float(1.380649e-23);
    if (strcmp(name, "N_A") == 0 || strcmp(name, "avogadro") == 0) return nql_val_float(6.02214076e23);
    if (strcmp(name, "R") == 0 || strcmp(name, "gas_constant") == 0) return nql_val_float(8.314462618);
    if (strcmp(name, "sigma") == 0 || strcmp(name, "stefan_boltzmann") == 0) return nql_val_float(5.670374419e-8);
    if (strcmp(name, "mu_0") == 0 || strcmp(name, "vacuum_permeability") == 0) return nql_val_float(1.25663706212e-6);
    if (strcmp(name, "epsilon_0") == 0 || strcmp(name, "vacuum_permittivity") == 0) return nql_val_float(8.8541878128e-12);
    if (strcmp(name, "m_e") == 0 || strcmp(name, "electron_mass") == 0) return nql_val_float(9.1093837015e-31);
    if (strcmp(name, "m_p") == 0 || strcmp(name, "proton_mass") == 0) return nql_val_float(1.67262192369e-27);
    if (strcmp(name, "g") == 0 || strcmp(name, "earth_gravity") == 0) return nql_val_float(9.80665);
    if (strcmp(name, "au") == 0 || strcmp(name, "astronomical_unit") == 0) return nql_val_float(1.495978707e11);
    if (strcmp(name, "ly") == 0 || strcmp(name, "light_year") == 0) return nql_val_float(9.4607304725808e15);
    if (strcmp(name, "eV") == 0 || strcmp(name, "electronvolt") == 0) return nql_val_float(1.602176634e-19);
    if (strcmp(name, "alpha") == 0 || strcmp(name, "fine_structure") == 0) return nql_val_float(7.2973525693e-3);
    return nql_val_null();
}

/* ======================================================================
 * COORDINATE TRANSFORMS
 * ====================================================================== */

/** CARTESIAN_TO_POLAR(x, y) -> [r, theta] */
static NqlValue nql_fn_cart_to_polar(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double x = nql_val_as_float(&args[0]), y = nql_val_as_float(&args[1]);
    NqlArray* r = nql_array_alloc(2);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_float(sqrt(x * x + y * y)));
    nql_array_push(r, nql_val_float(atan2(y, x)));
    return nql_val_array(r);
}

/** POLAR_TO_CARTESIAN(r, theta) -> [x, y] */
static NqlValue nql_fn_polar_to_cart(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double r = nql_val_as_float(&args[0]), theta = nql_val_as_float(&args[1]);
    NqlArray* res = nql_array_alloc(2);
    if (!res) return nql_val_null();
    nql_array_push(res, nql_val_float(r * cos(theta)));
    nql_array_push(res, nql_val_float(r * sin(theta)));
    return nql_val_array(res);
}

/** CARTESIAN_TO_SPHERICAL(x, y, z) -> [r, theta, phi] (ISO convention) */
static NqlValue nql_fn_cart_to_sph(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    double x = nql_val_as_float(&args[0]), y = nql_val_as_float(&args[1]), z = nql_val_as_float(&args[2]);
    double r = sqrt(x * x + y * y + z * z);
    NqlArray* res = nql_array_alloc(3);
    if (!res) return nql_val_null();
    nql_array_push(res, nql_val_float(r));
    nql_array_push(res, nql_val_float((r > 1e-15) ? acos(z / r) : 0)); /* theta (polar) */
    nql_array_push(res, nql_val_float(atan2(y, x))); /* phi (azimuthal) */
    return nql_val_array(res);
}

/** SPHERICAL_TO_CARTESIAN(r, theta, phi) -> [x, y, z] */
static NqlValue nql_fn_sph_to_cart(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    double r = nql_val_as_float(&args[0]), theta = nql_val_as_float(&args[1]), phi = nql_val_as_float(&args[2]);
    NqlArray* res = nql_array_alloc(3);
    if (!res) return nql_val_null();
    nql_array_push(res, nql_val_float(r * sin(theta) * cos(phi)));
    nql_array_push(res, nql_val_float(r * sin(theta) * sin(phi)));
    nql_array_push(res, nql_val_float(r * cos(theta)));
    return nql_val_array(res);
}

/** CARTESIAN_TO_CYLINDRICAL(x, y, z) -> [rho, phi, z] */
static NqlValue nql_fn_cart_to_cyl(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    double x = nql_val_as_float(&args[0]), y = nql_val_as_float(&args[1]), z = nql_val_as_float(&args[2]);
    NqlArray* res = nql_array_alloc(3);
    if (!res) return nql_val_null();
    nql_array_push(res, nql_val_float(sqrt(x * x + y * y)));
    nql_array_push(res, nql_val_float(atan2(y, x)));
    nql_array_push(res, nql_val_float(z));
    return nql_val_array(res);
}

/** CYLINDRICAL_TO_CARTESIAN(rho, phi, z) -> [x, y, z] */
static NqlValue nql_fn_cyl_to_cart(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    double rho = nql_val_as_float(&args[0]), phi = nql_val_as_float(&args[1]), z = nql_val_as_float(&args[2]);
    NqlArray* res = nql_array_alloc(3);
    if (!res) return nql_val_null();
    nql_array_push(res, nql_val_float(rho * cos(phi)));
    nql_array_push(res, nql_val_float(rho * sin(phi)));
    nql_array_push(res, nql_val_float(z));
    return nql_val_array(res);
}

/* ======================================================================
 * KINEMATICS & FORCES
 * ====================================================================== */

/** PROJECTILE(v0, angle_rad, g [, y0]) -> [range, max_height, flight_time] */
static NqlValue nql_fn_projectile(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD3(args);
    double v0 = nql_val_as_float(&args[0]);
    double angle = nql_val_as_float(&args[1]);
    double g = nql_val_as_float(&args[2]);
    double y0 = (argc >= 4) ? nql_val_as_float(&args[3]) : 0;
    if (g <= 0) return nql_val_null();
    double vx = v0 * cos(angle), vy = v0 * sin(angle);
    double t_flight = (vy + sqrt(vy * vy + 2 * g * y0)) / g;
    double range = vx * t_flight;
    double max_h = y0 + vy * vy / (2 * g);
    NqlArray* r = nql_array_alloc(3);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_float(range));
    nql_array_push(r, nql_val_float(max_h));
    nql_array_push(r, nql_val_float(t_flight));
    return nql_val_array(r);
}

/** GRAVITY_FORCE(m1, m2, r) -> F = G*m1*m2/r^2 */
static NqlValue nql_fn_gravity(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    double m1 = nql_val_as_float(&args[0]), m2 = nql_val_as_float(&args[1]), r = nql_val_as_float(&args[2]);
    if (r == 0) return nql_val_null();
    return nql_val_float(6.67430e-11 * m1 * m2 / (r * r));
}

/** COULOMB_FORCE(q1, q2, r) -> F = k*q1*q2/r^2 */
static NqlValue nql_fn_coulomb(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    double q1 = nql_val_as_float(&args[0]), q2 = nql_val_as_float(&args[1]), r = nql_val_as_float(&args[2]);
    if (r == 0) return nql_val_null();
    double k = 8.9875517873681764e9; /* 1/(4piepsilon_0) */
    return nql_val_float(k * q1 * q2 / (r * r));
}

/** ORBITAL_VELOCITY(M, r) -> v = sqrt(GM/r) */
static NqlValue nql_fn_orbital_velocity(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double M = nql_val_as_float(&args[0]), r = nql_val_as_float(&args[1]);
    if (r <= 0) return nql_val_null();
    return nql_val_float(sqrt(6.67430e-11 * M / r));
}

/** ORBITAL_PERIOD(M, r) -> T = 2pi*sqrt(r^3/(GM)) */
static NqlValue nql_fn_orbital_period(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double M = nql_val_as_float(&args[0]), r = nql_val_as_float(&args[1]);
    if (r <= 0 || M <= 0) return nql_val_null();
    return nql_val_float(2 * M_PI * sqrt(r * r * r / (6.67430e-11 * M)));
}

/** ESCAPE_VELOCITY(M, r) -> v = sqrt(2GM/r) */
static NqlValue nql_fn_escape_velocity(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double M = nql_val_as_float(&args[0]), r = nql_val_as_float(&args[1]);
    if (r <= 0) return nql_val_null();
    return nql_val_float(sqrt(2 * 6.67430e-11 * M / r));
}

/** LORENTZ_FACTOR(v) -> gamma = 1/sqrt(1 - v^2/c^2) */
static NqlValue nql_fn_lorentz(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    double v = nql_val_as_float(&args[0]);
    double c = 299792458.0;
    double beta2 = (v * v) / (c * c);
    if (beta2 >= 1.0) return nql_val_null();
    return nql_val_float(1.0 / sqrt(1.0 - beta2));
}

/** KINETIC_ENERGY(mass, velocity) -> 0.5*m*v^2 */
static NqlValue nql_fn_kinetic_energy(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double m = nql_val_as_float(&args[0]), v = nql_val_as_float(&args[1]);
    return nql_val_float(0.5 * m * v * v);
}

/** GRAVITATIONAL_PE(m, g, h) -> m*g*h */
static NqlValue nql_fn_grav_pe(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    return nql_val_float(nql_val_as_float(&args[0]) * nql_val_as_float(&args[1]) * nql_val_as_float(&args[2]));
}

/** SPRING_FORCE(k, x) -> F = -kx */
static NqlValue nql_fn_spring(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    return nql_val_float(-nql_val_as_float(&args[0]) * nql_val_as_float(&args[1]));
}

/** PENDULUM_PERIOD(length, g) -> T = 2pi*sqrt(L/g) */
static NqlValue nql_fn_pendulum(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double L = nql_val_as_float(&args[0]), g = nql_val_as_float(&args[1]);
    if (L <= 0 || g <= 0) return nql_val_null();
    return nql_val_float(2 * M_PI * sqrt(L / g));
}

/** DOPPLER(f0, v_source, v_observer [, v_sound]) -> observed frequency */
static NqlValue nql_fn_doppler(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD3(args);
    double f0 = nql_val_as_float(&args[0]);
    double vs = nql_val_as_float(&args[1]);
    double vo = nql_val_as_float(&args[2]);
    double c = (argc >= 4) ? nql_val_as_float(&args[3]) : 343.0; /* default: speed of sound in air */
    double denom = c - vs;
    if (fabs(denom) < 1e-15) return nql_val_null();
    return nql_val_float(f0 * (c + vo) / denom);
}

/** SCHWARZSCHILD_RADIUS(M) -> r_s = 2GM/c^2 */
static NqlValue nql_fn_schwarzschild(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    double M = nql_val_as_float(&args[0]);
    double c = 299792458.0;
    return nql_val_float(2 * 6.67430e-11 * M / (c * c));
}

/** WAVELENGTH_TO_FREQUENCY(wavelength) -> f = c/lambda */
static NqlValue nql_fn_wave_freq(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    double lam = nql_val_as_float(&args[0]);
    if (lam <= 0) return nql_val_null();
    return nql_val_float(299792458.0 / lam);
}

/** FREQUENCY_TO_WAVELENGTH(freq) -> lambda = c/f */
static NqlValue nql_fn_freq_wave(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    double f = nql_val_as_float(&args[0]);
    if (f <= 0) return nql_val_null();
    return nql_val_float(299792458.0 / f);
}

/** PHOTON_ENERGY(frequency) -> E = hf */
static NqlValue nql_fn_photon_energy(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    return nql_val_float(6.62607015e-34 * nql_val_as_float(&args[0]));
}

/** DE_BROGLIE(mass, velocity) -> lambda = h/(mv) */
static NqlValue nql_fn_de_broglie(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double mv = nql_val_as_float(&args[0]) * nql_val_as_float(&args[1]);
    if (fabs(mv) < 1e-50) return nql_val_null();
    return nql_val_float(6.62607015e-34 / mv);
}

/** IDEAL_GAS(n_moles, T, V) -> P = nRT/V */
static NqlValue nql_fn_ideal_gas(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    double n = nql_val_as_float(&args[0]), T = nql_val_as_float(&args[1]), V = nql_val_as_float(&args[2]);
    if (V <= 0) return nql_val_null();
    return nql_val_float(n * 8.314462618 * T / V);
}

/* ======================================================================
 * UNIT CONVERSION
 * ====================================================================== */

/** UNIT_CONVERT(value, from_unit, to_unit) */
static NqlValue nql_fn_unit_convert(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    double val = nql_val_as_float(&args[0]);
    if (args[1].type != NQL_VAL_STRING || args[2].type != NQL_VAL_STRING) return nql_val_null();
    const char* from = args[1].v.sval;
    const char* to = args[2].v.sval;

    /* Temperature */
    if (strcmp(from, "C") == 0 && strcmp(to, "F") == 0) return nql_val_float(val * 9.0/5.0 + 32);
    if (strcmp(from, "F") == 0 && strcmp(to, "C") == 0) return nql_val_float((val - 32) * 5.0/9.0);
    if (strcmp(from, "C") == 0 && strcmp(to, "K") == 0) return nql_val_float(val + 273.15);
    if (strcmp(from, "K") == 0 && strcmp(to, "C") == 0) return nql_val_float(val - 273.15);
    if (strcmp(from, "F") == 0 && strcmp(to, "K") == 0) return nql_val_float((val - 32) * 5.0/9.0 + 273.15);
    if (strcmp(from, "K") == 0 && strcmp(to, "F") == 0) return nql_val_float((val - 273.15) * 9.0/5.0 + 32);

    /* Length */
    if (strcmp(from, "m") == 0 && strcmp(to, "ft") == 0) return nql_val_float(val / 0.3048);
    if (strcmp(from, "ft") == 0 && strcmp(to, "m") == 0) return nql_val_float(val * 0.3048);
    if (strcmp(from, "km") == 0 && strcmp(to, "mi") == 0) return nql_val_float(val / 1.609344);
    if (strcmp(from, "mi") == 0 && strcmp(to, "km") == 0) return nql_val_float(val * 1.609344);
    if (strcmp(from, "m") == 0 && strcmp(to, "km") == 0) return nql_val_float(val / 1000.0);
    if (strcmp(from, "km") == 0 && strcmp(to, "m") == 0) return nql_val_float(val * 1000.0);
    if (strcmp(from, "in") == 0 && strcmp(to, "cm") == 0) return nql_val_float(val * 2.54);
    if (strcmp(from, "cm") == 0 && strcmp(to, "in") == 0) return nql_val_float(val / 2.54);

    /* Mass */
    if (strcmp(from, "kg") == 0 && strcmp(to, "lb") == 0) return nql_val_float(val * 2.20462);
    if (strcmp(from, "lb") == 0 && strcmp(to, "kg") == 0) return nql_val_float(val / 2.20462);

    /* Energy */
    if (strcmp(from, "J") == 0 && strcmp(to, "eV") == 0) return nql_val_float(val / 1.602176634e-19);
    if (strcmp(from, "eV") == 0 && strcmp(to, "J") == 0) return nql_val_float(val * 1.602176634e-19);

    /* Time */
    if (strcmp(from, "s") == 0 && strcmp(to, "min") == 0) return nql_val_float(val / 60.0);
    if (strcmp(from, "min") == 0 && strcmp(to, "s") == 0) return nql_val_float(val * 60.0);
    if (strcmp(from, "hr") == 0 && strcmp(to, "s") == 0) return nql_val_float(val * 3600.0);
    if (strcmp(from, "s") == 0 && strcmp(to, "hr") == 0) return nql_val_float(val / 3600.0);

    return nql_val_null(); /* unsupported conversion */
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_physics_register_functions(void) {
    /* Constants & units */
    nql_register_func("PHYS_CONST",             nql_fn_phys_const,       1, 1, "Physical constant by name (c, G, h, k_B, ...)");
    nql_register_func("UNIT_CONVERT",           nql_fn_unit_convert,     3, 3, "Unit conversion (val, from, to)");
    /* Coordinate transforms */
    nql_register_func("CARTESIAN_TO_POLAR",     nql_fn_cart_to_polar,    2, 2, "[x,y] -> [r,theta]");
    nql_register_func("POLAR_TO_CARTESIAN",     nql_fn_polar_to_cart,    2, 2, "[r,theta] -> [x,y]");
    nql_register_func("CARTESIAN_TO_SPHERICAL", nql_fn_cart_to_sph,      3, 3, "[x,y,z] -> [r,theta,phi]");
    nql_register_func("SPHERICAL_TO_CARTESIAN", nql_fn_sph_to_cart,      3, 3, "[r,theta,phi] -> [x,y,z]");
    nql_register_func("CARTESIAN_TO_CYLINDRICAL",nql_fn_cart_to_cyl,     3, 3, "[x,y,z] -> [rho,phi,z]");
    nql_register_func("CYLINDRICAL_TO_CARTESIAN",nql_fn_cyl_to_cart,     3, 3, "[rho,phi,z] -> [x,y,z]");
    /* Mechanics */
    nql_register_func("PROJECTILE",             nql_fn_projectile,       3, 4, "Projectile: [range, max_height, time]");
    nql_register_func("GRAVITY_FORCE",          nql_fn_gravity,          3, 3, "Newton gravity: G*m1*m2/r^2");
    nql_register_func("COULOMB_FORCE",          nql_fn_coulomb,          3, 3, "Coulomb: k*q1*q2/r^2");
    nql_register_func("KINETIC_ENERGY",         nql_fn_kinetic_energy,   2, 2, "0.5*m*v^2");
    nql_register_func("GRAVITATIONAL_PE",       nql_fn_grav_pe,          3, 3, "m*g*h");
    nql_register_func("SPRING_FORCE",           nql_fn_spring,           2, 2, "Hooke's law: -kx");
    nql_register_func("PENDULUM_PERIOD",        nql_fn_pendulum,         2, 2, "2pisqrt(L/g)");
    nql_register_func("DOPPLER",                nql_fn_doppler,          3, 4, "Doppler shift");
    /* Orbital */
    nql_register_func("ORBITAL_VELOCITY",       nql_fn_orbital_velocity, 2, 2, "sqrt(GM/r)");
    nql_register_func("ORBITAL_PERIOD",         nql_fn_orbital_period,   2, 2, "2pisqrt(r^3/GM)");
    nql_register_func("ESCAPE_VELOCITY",        nql_fn_escape_velocity,  2, 2, "sqrt(2GM/r)");
    /* Relativistic & quantum */
    nql_register_func("LORENTZ_FACTOR",         nql_fn_lorentz,          1, 1, "gamma = 1/sqrt(1-v^2/c^2)");
    nql_register_func("SCHWARZSCHILD_RADIUS",   nql_fn_schwarzschild,    1, 1, "r_s = 2GM/c^2");
    nql_register_func("WAVELENGTH_TO_FREQUENCY",nql_fn_wave_freq,        1, 1, "f = c/lambda");
    nql_register_func("FREQUENCY_TO_WAVELENGTH",nql_fn_freq_wave,        1, 1, "lambda = c/f");
    nql_register_func("PHOTON_ENERGY",          nql_fn_photon_energy,    1, 1, "E = hf");
    nql_register_func("DE_BROGLIE",             nql_fn_de_broglie,       2, 2, "lambda = h/(mv)");
    nql_register_func("IDEAL_GAS",              nql_fn_ideal_gas,        3, 3, "P = nRT/V");
}
