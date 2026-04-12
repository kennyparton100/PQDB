/**
 * nql_quaternion.c - NQL Quaternion type.
 * Part of the CPSS Viewer amalgamation.
 *
 * Hamilton quaternions q = w + xi + yj + zk for 3D rotations and algebra.
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

/* Quaternion stored as 4-element array [w, x, y, z] */

static NqlValue quat_make(double w, double x, double y, double z) {
    NqlArray* r = nql_array_alloc(4);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_float(w));
    nql_array_push(r, nql_val_float(x));
    nql_array_push(r, nql_val_float(y));
    nql_array_push(r, nql_val_float(z));
    return nql_val_array(r);
}

#define Q_W(a) nql_val_as_float(&(a)->items[0])
#define Q_X(a) nql_val_as_float(&(a)->items[1])
#define Q_Y(a) nql_val_as_float(&(a)->items[2])
#define Q_Z(a) nql_val_as_float(&(a)->items[3])

static bool quat_check(const NqlValue* v) {
    return v->type == NQL_VAL_ARRAY && v->v.array && v->v.array->length >= 4;
}

/** QUAT(w, x, y, z) */
static NqlValue nql_fn_quat(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD4(args);
    return quat_make(nql_val_as_float(&args[0]), nql_val_as_float(&args[1]),
                     nql_val_as_float(&args[2]), nql_val_as_float(&args[3]));
}

/** QUAT_FROM_AXIS_ANGLE(axis_array, angle_radians) */
static NqlValue nql_fn_quat_from_axis(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length < 3) return nql_val_null();
    NqlArray* ax = args[0].v.array;
    double angle = nql_val_as_float(&args[1]);
    double x = nql_val_as_float(&ax->items[0]);
    double y = nql_val_as_float(&ax->items[1]);
    double z = nql_val_as_float(&ax->items[2]);
    double len = sqrt(x * x + y * y + z * z);
    if (len < 1e-15) return nql_val_null();
    x /= len; y /= len; z /= len;
    double s = sin(angle / 2.0), c = cos(angle / 2.0);
    return quat_make(c, x * s, y * s, z * s);
}

/** QUAT_ADD(a, b) */
static NqlValue nql_fn_quat_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0]) || !quat_check(&args[1])) return nql_val_null();
    NqlArray* a = args[0].v.array; NqlArray* b = args[1].v.array;
    return quat_make(Q_W(a) + Q_W(b), Q_X(a) + Q_X(b), Q_Y(a) + Q_Y(b), Q_Z(a) + Q_Z(b));
}

/** QUAT_SUB(a, b) */
static NqlValue nql_fn_quat_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0]) || !quat_check(&args[1])) return nql_val_null();
    NqlArray* a = args[0].v.array; NqlArray* b = args[1].v.array;
    return quat_make(Q_W(a) - Q_W(b), Q_X(a) - Q_X(b), Q_Y(a) - Q_Y(b), Q_Z(a) - Q_Z(b));
}

/** QUAT_MUL(a, b) -- Hamilton product */
static NqlValue nql_fn_quat_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0]) || !quat_check(&args[1])) return nql_val_null();
    NqlArray* a = args[0].v.array; NqlArray* b = args[1].v.array;
    double aw = Q_W(a), ax = Q_X(a), ay = Q_Y(a), az = Q_Z(a);
    double bw = Q_W(b), bx = Q_X(b), by = Q_Y(b), bz = Q_Z(b);
    return quat_make(
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw
    );
}

/** QUAT_CONJ(q) */
static NqlValue nql_fn_quat_conj(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0])) return nql_val_null();
    NqlArray* a = args[0].v.array;
    return quat_make(Q_W(a), -Q_X(a), -Q_Y(a), -Q_Z(a));
}

/** QUAT_NORM(q) -- magnitude */
static NqlValue nql_fn_quat_norm(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0])) return nql_val_null();
    NqlArray* a = args[0].v.array;
    return nql_val_float(sqrt(Q_W(a) * Q_W(a) + Q_X(a) * Q_X(a) + Q_Y(a) * Q_Y(a) + Q_Z(a) * Q_Z(a)));
}

/** QUAT_NORMALIZE(q) */
static NqlValue nql_fn_quat_normalize(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0])) return nql_val_null();
    NqlArray* a = args[0].v.array;
    double n = sqrt(Q_W(a) * Q_W(a) + Q_X(a) * Q_X(a) + Q_Y(a) * Q_Y(a) + Q_Z(a) * Q_Z(a));
    if (n < 1e-15) return nql_val_null();
    return quat_make(Q_W(a) / n, Q_X(a) / n, Q_Y(a) / n, Q_Z(a) / n);
}

/** QUAT_INV(q) -- multiplicative inverse conj(q)/|q|^2 */
static NqlValue nql_fn_quat_inv(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0])) return nql_val_null();
    NqlArray* a = args[0].v.array;
    double n2 = Q_W(a) * Q_W(a) + Q_X(a) * Q_X(a) + Q_Y(a) * Q_Y(a) + Q_Z(a) * Q_Z(a);
    if (n2 < 1e-30) return nql_val_null();
    return quat_make(Q_W(a) / n2, -Q_X(a) / n2, -Q_Y(a) / n2, -Q_Z(a) / n2);
}

/** QUAT_ROTATE(q, vec3_array) -- rotate 3D vector by unit quaternion: q v q* */
static NqlValue nql_fn_quat_rotate(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0])) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array || args[1].v.array->length < 3) return nql_val_null();
    NqlArray* q = args[0].v.array; NqlArray* v = args[1].v.array;
    double qw = Q_W(q), qx = Q_X(q), qy = Q_Y(q), qz = Q_Z(q);
    double vx = nql_val_as_float(&v->items[0]);
    double vy = nql_val_as_float(&v->items[1]);
    double vz = nql_val_as_float(&v->items[2]);
    /* q * (0,v) * q* expanded */
    double t2  =  qw * qx, t3  =  qw * qy, t4  =  qw * qz;
    double t5  = -qx * qx, t6  =  qx * qy, t7  =  qx * qz;
    double t8  = -qy * qy, t9  =  qy * qz, t10 = -qz * qz;
    double rx = 2 * ((t8 + t10) * vx + (t6 - t4) * vy + (t3 + t7) * vz) + vx;
    double ry = 2 * ((t4 + t6) * vx + (t5 + t10) * vy + (t9 - t2) * vz) + vy;
    double rz = 2 * ((t7 - t3) * vx + (t2 + t9) * vy + (t5 + t8) * vz) + vz;
    NqlArray* res = nql_array_alloc(3);
    if (!res) return nql_val_null();
    nql_array_push(res, nql_val_float(rx));
    nql_array_push(res, nql_val_float(ry));
    nql_array_push(res, nql_val_float(rz));
    return nql_val_array(res);
}

/** QUAT_TO_MATRIX(q) -- 3x3 rotation matrix as flat 9-element array (row-major) */
static NqlValue nql_fn_quat_to_matrix(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0])) return nql_val_null();
    NqlArray* q = args[0].v.array;
    double w = Q_W(q), x = Q_X(q), y = Q_Y(q), z = Q_Z(q);
    double n = sqrt(w * w + x * x + y * y + z * z);
    if (n < 1e-15) return nql_val_null();
    w /= n; x /= n; y /= n; z /= n;
    NqlArray* r = nql_array_alloc(9);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_float(1 - 2*(y*y + z*z)));
    nql_array_push(r, nql_val_float(2*(x*y - w*z)));
    nql_array_push(r, nql_val_float(2*(x*z + w*y)));
    nql_array_push(r, nql_val_float(2*(x*y + w*z)));
    nql_array_push(r, nql_val_float(1 - 2*(x*x + z*z)));
    nql_array_push(r, nql_val_float(2*(y*z - w*x)));
    nql_array_push(r, nql_val_float(2*(x*z - w*y)));
    nql_array_push(r, nql_val_float(2*(y*z + w*x)));
    nql_array_push(r, nql_val_float(1 - 2*(x*x + y*y)));
    return nql_val_array(r);
}

/** QUAT_SLERP(a, b, t) -- spherical linear interpolation */
static NqlValue nql_fn_quat_slerp(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (!quat_check(&args[0]) || !quat_check(&args[1])) return nql_val_null();
    NqlArray* a = args[0].v.array; NqlArray* b = args[1].v.array;
    double t = nql_val_as_float(&args[2]);
    double dot = Q_W(a)*Q_W(b) + Q_X(a)*Q_X(b) + Q_Y(a)*Q_Y(b) + Q_Z(a)*Q_Z(b);
    double bw = Q_W(b), bx = Q_X(b), by = Q_Y(b), bz = Q_Z(b);
    if (dot < 0) { dot = -dot; bw = -bw; bx = -bx; by = -by; bz = -bz; }
    double s0, s1;
    if (dot > 0.9995) { s0 = 1 - t; s1 = t; }
    else { double theta = acos(dot); double st = sin(theta); s0 = sin((1 - t) * theta) / st; s1 = sin(t * theta) / st; }
    return quat_make(s0*Q_W(a) + s1*bw, s0*Q_X(a) + s1*bx, s0*Q_Y(a) + s1*by, s0*Q_Z(a) + s1*bz);
}

/** QUAT_TO_EULER(q) -- [roll, pitch, yaw] in radians */
static NqlValue nql_fn_quat_to_euler(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (!quat_check(&args[0])) return nql_val_null();
    NqlArray* q = args[0].v.array;
    double w = Q_W(q), x = Q_X(q), y = Q_Y(q), z = Q_Z(q);
    double sinr = 2 * (w * x + y * z);
    double cosr = 1 - 2 * (x * x + y * y);
    double roll = atan2(sinr, cosr);
    double sinp = 2 * (w * y - z * x);
    double pitch = (fabs(sinp) >= 1) ? copysign(M_PI / 2, sinp) : asin(sinp);
    double siny = 2 * (w * z + x * y);
    double cosy = 1 - 2 * (y * y + z * z);
    double yaw = atan2(siny, cosy);
    NqlArray* r = nql_array_alloc(3);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_float(roll));
    nql_array_push(r, nql_val_float(pitch));
    nql_array_push(r, nql_val_float(yaw));
    return nql_val_array(r);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_quaternion_register_functions(void) {
    nql_register_func("QUAT",               nql_fn_quat,            4, 4, "Create quaternion [w,x,y,z]");
    nql_register_func("QUAT_FROM_AXIS_ANGLE",nql_fn_quat_from_axis, 2, 2, "Quaternion from axis+angle");
    nql_register_func("QUAT_ADD",           nql_fn_quat_add,        2, 2, "Quaternion addition");
    nql_register_func("QUAT_SUB",           nql_fn_quat_sub,        2, 2, "Quaternion subtraction");
    nql_register_func("QUAT_MUL",           nql_fn_quat_mul,        2, 2, "Hamilton product");
    nql_register_func("QUAT_CONJ",          nql_fn_quat_conj,       1, 1, "Conjugate");
    nql_register_func("QUAT_NORM",          nql_fn_quat_norm,       1, 1, "Magnitude");
    nql_register_func("QUAT_NORMALIZE",     nql_fn_quat_normalize,  1, 1, "Normalize to unit quaternion");
    nql_register_func("QUAT_INV",           nql_fn_quat_inv,        1, 1, "Multiplicative inverse");
    nql_register_func("QUAT_ROTATE",        nql_fn_quat_rotate,     2, 2, "Rotate 3D vector by quaternion");
    nql_register_func("QUAT_TO_MATRIX",     nql_fn_quat_to_matrix,  1, 1, "3x3 rotation matrix (flat array)");
    nql_register_func("QUAT_SLERP",         nql_fn_quat_slerp,      3, 3, "Spherical linear interpolation");
    nql_register_func("QUAT_TO_EULER",      nql_fn_quat_to_euler,   1, 1, "Convert to [roll,pitch,yaw]");
}
