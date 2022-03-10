#define FRACTION_SHIFT 14
#define FRACTION_CONSTANT (1 << FRACTION_SHIFT)

typedef long long int int64_t;
typedef int64_t fixed64;

#ifdef ENABLE_FIXED_POINT

static fixed64 int32_to_fixed64(int x) {
    fixed64 _x = x;
    return _x * FRACTION_CONSTANT;
} 

static int fixed64_to_int32(fixed64 x) {
    fixed64 x_int = x / FRACTION_CONSTANT;
    return (int)(x_int & 0xFFFFFFFF); 
}

static fixed64 fixed64_add(fixed64 x1, fixed64 x2) {
    return x1 + x2;
}

static fixed64 fixed64_sub(fixed64 x1, fixed64 x2) {
    return x1 - x2;
}

static fixed64 fixed64_add_int32(fixed64 x1, int x2) {
    fixed64 x2_ = int32_to_fixed64(x2);
    return x1 + x2_;
}

static fixed64 fixed64_sub_int32(fixed64 x1, int x2) {
    fixed64 x2_ = int32_to_fixed64(x2);
    return x1 - x2_;
}

static fixed64 fixed64_mul(fixed64 x1, fixed64 x2) {
    return x1 * x2 / FRACTION_CONSTANT;
}

static fixed64 fixed64_div(fixed64 x1, fixed64 x2) {
    return x1 * FRACTION_CONSTANT / x2;
}

static fixed64 fixed64_mul_int32(fixed64 x1, int x2) {
    fixed64 x2_ = (fixed64)x2;
    return x1*x2_;
}

static fixed64 fixed64_div_int32(fixed64 x1, int x2) {
    fixed64 x2_ = (fixed64)x2;
    return x1/x2_;
}

#endif