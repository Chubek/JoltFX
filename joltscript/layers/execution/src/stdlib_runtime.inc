/* Included by the typed VM: intrinsic values share its invocation arena/budget. */
#define PCRE2_CODE_UNIT_WIDTH 8
#include <inttypes.h>
#include <pcre2.h>
#define PI 3.14159265358979323846264338327950288
static bool named(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}
static size_t items(run *r, value v)
{
    if (v.value.type < 12 || v.value.type > 15) {
        r->status = JOLT_ERR_TYPE;
        return 0;
    }
    return v.object ? v.object->count : 0;
}
static double num(run *r, value v)
{
    if (!numeric((uint32_t) v.value.type) || v.fn) {
        r->status = JOLT_ERR_TYPE;
        return 0;
    }
    return real(v.value);
}
static int64_t index_value(run *r, value v)
{
    if (!integer((uint32_t) v.value.type) ||
        (!signed_type(v.value.type) && v.value.as.unsigned_integer > INT64_MAX)) {
        r->status = JOLT_ERR_TYPE;
        return 0;
    }
    return signed_type(v.value.type) ? v.value.as.integer : (int64_t) v.value.as.unsigned_integer;
}
static value at(run *r, value v, size_t i)
{
    if (i >= items(r, v)) {
        r->status = JOLT_ERR_ARGUMENT;
        return zero(0);
    }
    return v.object->items[i];
}
static double component(run *r, value v, size_t i)
{
    return num(r, at(r, v, i));
}
static value numbers(run *r, const double *data, size_t count)
{
    value v = collection(r, 13, count);
    for (size_t i = 0; i < count && !r->status; ++i)
        v.object->items[i] = number(r, data[i]);
    return v;
}
static value section(run *r, value v, size_t start, size_t end)
{
    size_t count = items(r, v);
    if (start > end || end > count) {
        r->status = JOLT_ERR_ARGUMENT;
        return zero(0);
    }
    value out = collection(r, 13, end - start);
    if (!r->status && end > start)
        memcpy(out.object->items, v.object->items + start, (end - start) * sizeof(value));
    return out;
}
static double clamp01(double x)
{
    return fmin(1., fmax(0., x));
}
static double modulo(double a, double b)
{
    double r = fmod(a, b);
    return r < 0 ? r + fabs(b) : r;
}
static double bounce(double t)
{
    if (t < 1 / 2.75)
        return 7.5625 * t * t;
    if (t < 2 / 2.75) {
        t -= 1.5 / 2.75;
        return 7.5625 * t * t + 0.75;
    }
    if (t < 2.5 / 2.75) {
        t -= 2.25 / 2.75;
        return 7.5625 * t * t + 0.9375;
    }
    t -= 2.625 / 2.75;
    return 7.5625 * t * t + 0.984375;
}
static double ease_in(const char *kind, double t)
{
    if (named(kind, "quad"))
        return t * t;
    if (named(kind, "cubic"))
        return t * t * t;
    if (named(kind, "quart"))
        return t * t * t * t;
    if (named(kind, "quint"))
        return t * t * t * t * t;
    if (named(kind, "sine"))
        return 1 - cos(t * PI / 2);
    if (named(kind, "expo"))
        return t == 0 ? 0 : pow(2, 10 * t - 10);
    if (named(kind, "circ"))
        return 1 - sqrt(1 - t * t);
    if (named(kind, "back"))
        return 2.70158 * t * t * t - 1.70158 * t * t;
    if (named(kind, "bounce"))
        return 1 - bounce(1 - t);
    if (named(kind, "elastic"))
        return t == 0 || t == 1 ? t : -pow(2, 10 * t - 10) * sin((10 * t - 10.75) * (2 * PI / 3));
    return t;
}
static double easing(const char *name, double t)
{
    if (strncmp(name, "ease-in-out-", 12) == 0)
        return t < 0.5 ? ease_in(name + 12, 2 * t) / 2 : 1 - ease_in(name + 12, 2 - 2 * t) / 2;
    if (strncmp(name, "ease-out-", 9) == 0)
        return 1 - ease_in(name + 9, 1 - t);
    if (strncmp(name, "ease-in-", 8) == 0)
        return ease_in(name + 8, t);
    return t;
}
static double cubic(double t, double a, double b, double c, double d, bool bezier)
{
    if (bezier) {
        double u = 1 - t;
        return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d;
    }
    return 0.5 * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t * t +
                  (-a + 3 * b - 3 * c + d) * t * t * t);
}
static value math_native(run *r, const char *name, const value *a, unsigned count)
{
    double x = count ? num(r, a[0]) : 0, y = count > 1 ? num(r, a[1]) : 0,
           z = count > 2 ? num(r, a[2]) : 0;
    if (r->status)
        return zero(0);
    double result = 0;
    if (named(name, "PI"))
        result = PI;
    else if (named(name, "TWO-PI"))
        result = 2 * PI;
    else if (named(name, "HALF-PI"))
        result = PI / 2;
    else if (named(name, "E"))
        result = exp(1);
    else if (named(name, "SQRT2"))
        result = sqrt(2);
    else if (named(name, "SQRT1_2"))
        result = sqrt(0.5);
    else if (named(name, "sin"))
        result = sin(x);
    else if (named(name, "cos"))
        result = cos(x);
    else if (named(name, "tan"))
        result = tan(x);
    else if (named(name, "asin"))
        result = asin(x);
    else if (named(name, "acos"))
        result = acos(x);
    else if (named(name, "atan"))
        result = atan(x);
    else if (named(name, "atan2"))
        result = atan2(x, y);
    else if (named(name, "exp"))
        result = exp(x);
    else if (named(name, "log"))
        result = log(x);
    else if (named(name, "log10"))
        result = log10(x);
    else if (named(name, "log2"))
        result = log2(x);
    else if (named(name, "exp2"))
        result = exp2(x);
    else if (named(name, "pow"))
        result = pow(x, y);
    else if (named(name, "sqrt"))
        result = sqrt(x);
    else if (named(name, "cbrt"))
        result = cbrt(x);
    else if (named(name, "hypot"))
        result = hypot(x, y);
    else if (named(name, "floor"))
        result = floor(x);
    else if (named(name, "ceil"))
        result = ceil(x);
    else if (named(name, "round"))
        result = round(x);
    else if (named(name, "trunc"))
        result = trunc(x);
    else if (named(name, "abs"))
        result = fabs(x);
    else if (named(name, "sign"))
        result = x < 0 ? -1 : x > 0 ? 1 : 0;
    else if (named(name, "min"))
        result = fmin(x, y);
    else if (named(name, "max"))
        result = fmax(x, y);
    else if (named(name, "mod"))
        result = modulo(x, y);
    else if (named(name, "clamp")) {
        if (y > z)
            r->status = JOLT_ERR_ARGUMENT;
        result = fmin(fmax(x, y), z);
    } else if (named(name, "lerp"))
        result = x + z * (y - x);
    else if (named(name, "smoothstep")) {
        if (x >= y)
            r->status = JOLT_ERR_ARGUMENT;
        double t = clamp01((z - x) / (y - x));
        result = t * t * (3 - 2 * t);
    } else if (named(name, "map-range"))
        result = num(r, a[3]) + (x - y) / (z - y) * (num(r, a[4]) - num(r, a[3]));
    else if (named(name, "deg->rad"))
        result = x * PI / 180;
    else if (named(name, "rad->deg"))
        result = x * 180 / PI;
    else if (strncmp(name, "ease-", 5) == 0)
        result = easing(name, x);
    else {
        r->status = JOLT_ERR_NOT_FOUND;
        return zero(0);
    }
    return number(r, result);
}
static value geometry_native(run *r, const char *name, const value *a, unsigned count)
{
    if (named(name, "point2d") || named(name, "point3d") || named(name, "vec2") ||
        named(name, "vec3") || named(name, "vec4") || named(name, "quat")) {
        double data[4];
        for (unsigned i = 0; i < count; ++i)
            data[i] = num(r, a[i]);
        return numbers(r, data, count);
    }
    if (named(name, "quat-identity")) {
        const double data[] = {0, 0, 0, 1};
        return numbers(r, data, 4);
    }
    size_t n = items(r, a[0]);
    if (n > 64 || !n) {
        r->status = JOLT_ERR_ARGUMENT;
        return zero(0);
    }
    double x[64] = {0}, y[64] = {0}, out[64] = {0};
    if (strncmp(name, "bbox", 4) == 0) {
        unsigned dim = named(name, "bbox2d") ? 2u : 3u;
        for (unsigned j = 0; j < dim; ++j) {
            out[j] = INFINITY;
            out[j + dim] = -INFINITY;
        }
        for (size_t i = 0; i < n && !r->status; ++i) {
            value p = at(r, a[0], i);
            if (items(r, p) != dim) {
                r->status = JOLT_ERR_ARGUMENT;
                break;
            }
            for (unsigned j = 0; j < dim; ++j) {
                double q = component(r, p, j);
                out[j] = fmin(out[j], q);
                out[j + dim] = fmax(out[j + dim], q);
            }
        }
        return numbers(r, out, dim * 2);
    }
    for (size_t i = 0; i < n; ++i)
        x[i] = component(r, a[0], i);
    bool second_sequence = count > 1 && a[1].value.type >= 12 && a[1].value.type <= 15;
    size_t ny = second_sequence ? items(r, a[1]) : 0;
    if (ny > 64) {
        r->status = JOLT_ERR_ARGUMENT;
        return zero(0);
    }
    for (size_t i = 0; i < ny; ++i)
        y[i] = component(r, a[1], i);
    if (r->status)
        return zero(0);
    if (strncmp(name, "point2d", 7) == 0 && n != 2)
        r->status = JOLT_ERR_ARGUMENT;
    if (strncmp(name, "point3d", 7) == 0 && n != 3)
        r->status = JOLT_ERR_ARGUMENT;
    if (strstr(name, "-x") && strncmp(name, "point", 5) == 0)
        return number(r, x[0]);
    if (strstr(name, "-y") && strncmp(name, "point", 5) == 0)
        return number(r, x[1]);
    if (strstr(name, "-z") && strncmp(name, "point", 5) == 0)
        return number(r, x[2]);
    if (named(name, "mat2-det")) {
        if (n != 4)
            r->status = JOLT_ERR_ARGUMENT;
        return number(r, x[0] * x[3] - x[1] * x[2]);
    }
    if (named(name, "mat-mul") || named(name, "mat2-mul") || named(name, "mat-transpose")) {
        int64_t dim =
            named(name, "mat2-mul") ? 2 : index_value(r, a[named(name, "mat-transpose") ? 1 : 2]);
        if (dim < 1 || dim > 8 || n != (size_t) (dim * dim) ||
            (!named(name, "mat-transpose") && ny != n)) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        for (size_t i = 0; i < (size_t) dim; ++i)
            for (size_t j = 0; j < (size_t) dim; ++j) {
                if (named(name, "mat-transpose"))
                    out[i * (size_t) dim + j] = x[j * (size_t) dim + i];
                else
                    for (size_t k = 0; k < (size_t) dim; ++k)
                        out[i * (size_t) dim + j] +=
                            x[i * (size_t) dim + k] * y[k * (size_t) dim + j];
            }
        return numbers(r, out, n);
    }
    if (named(name, "dot") || named(name, "vec-dot") || strstr(name, "-dist")) {
        if (n != ny)
            r->status = JOLT_ERR_ARGUMENT;
        double sum = 0;
        for (size_t i = 0; i < n; ++i)
            sum += strstr(name, "-dist") ? (x[i] - y[i]) * (x[i] - y[i]) : x[i] * y[i];
        return number(r, strstr(name, "-dist") ? sqrt(sum) : sum);
    }
    if (named(name, "length") || named(name, "vec-length") || strstr(name, "normalize")) {
        double norm = 0;
        for (size_t i = 0; i < n; ++i)
            norm = hypot(norm, x[i]);
        if (!strstr(name, "normalize"))
            return number(r, norm);
        for (size_t i = 0; i < n; ++i)
            out[i] = norm ? x[i] / norm : x[i];
        return numbers(r, out, n);
    }
    if (named(name, "cross") || named(name, "vec-cross")) {
        if (n != 3 || ny != 3)
            r->status = JOLT_ERR_ARGUMENT;
        for (unsigned i = 0; i < 3; ++i)
            out[i] = x[(i + 1) % 3] * y[(i + 2) % 3] - x[(i + 2) % 3] * y[(i + 1) % 3];
        return numbers(r, out, 3);
    }
    if (strstr(name, "-add") || strstr(name, "-sub") || strstr(name, "-lerp")) {
        if (n != ny)
            r->status = JOLT_ERR_ARGUMENT;
        for (size_t i = 0; i < n; ++i)
            out[i] = strstr(name, "-add")   ? x[i] + y[i]
                     : strstr(name, "-sub") ? x[i] - y[i]
                                            : x[i] + num(r, a[2]) * (y[i] - x[i]);
        return numbers(r, out, n);
    }
    if (strstr(name, "-scale")) {
        for (size_t i = 0; i < n; ++i)
            out[i] = x[i] * num(r, a[1]);
        return numbers(r, out, n);
    }
    if (strncmp(name, "translate", 9) == 0 || strncmp(name, "scale", 5) == 0) {
        if (n != count - 1)
            r->status = JOLT_ERR_ARGUMENT;
        for (size_t i = 0; i < n && !r->status; ++i)
            out[i] =
                strncmp(name, "scale", 5) == 0 ? x[i] * num(r, a[i + 1]) : x[i] + num(r, a[i + 1]);
        return numbers(r, out, n);
    }
    if (strncmp(name, "rotate", 6) == 0) {
        bool two = named(name, "rotate2d");
        if (n != (two ? 2u : 3u))
            r->status = JOLT_ERR_ARGUMENT;
        double angle = num(r, a[1]), c = cos(angle), s = sin(angle);
        memcpy(out, x, n * sizeof(double));
        unsigned i = 0, j = 1;
        if (named(name, "rotate3d-x")) {
            i = 1;
            j = 2;
        } else if (named(name, "rotate3d-y")) {
            i = 2;
            j = 0;
        }
        out[i] = x[i] * c - x[j] * s;
        out[j] = x[i] * s + x[j] * c;
        return numbers(r, out, n);
    }
    if (named(name, "quat-from-axis-angle")) {
        if (n != 3)
            r->status = JOLT_ERR_ARGUMENT;
        double norm = hypot(hypot(x[0], x[1]), x[2]), half = num(r, a[1]) / 2;
        for (unsigned i = 0; i < 3; ++i)
            out[i] = norm ? x[i] * sin(half) / norm : 0;
        out[3] = norm ? cos(half) : 1;
        return numbers(r, out, 4);
    }
    if (named(name, "quat-mul")) {
        if (n != 4 || ny != 4)
            r->status = JOLT_ERR_ARGUMENT;
        out[0] = x[3] * y[0] + x[0] * y[3] + x[1] * y[2] - x[2] * y[1];
        out[1] = x[3] * y[1] + x[1] * y[3] + x[2] * y[0] - x[0] * y[2];
        out[2] = x[3] * y[2] + x[2] * y[3] + x[0] * y[1] - x[1] * y[0];
        out[3] = x[3] * y[3] - x[0] * y[0] - x[1] * y[1] - x[2] * y[2];
        return numbers(r, out, 4);
    }
    if (named(name, "quat-rotate")) {
        if (n != 4 || ny != 3)
            r->status = JOLT_ERR_ARGUMENT;
        double norm = 0;
        for (unsigned i = 0; i < 4; ++i)
            norm += x[i] * x[i];
        if (norm == 0) {
            r->status = JOLT_ERR_NUMERIC;
            return zero(0);
        }
        double cross[3];
        for (unsigned i = 0; i < 3; ++i)
            cross[i] = x[(i + 1) % 3] * y[(i + 2) % 3] - x[(i + 2) % 3] * y[(i + 1) % 3];
        for (unsigned i = 0; i < 3; ++i)
            out[i] = y[i] + 2 *
                                (x[3] * cross[i] + x[(i + 1) % 3] * cross[(i + 2) % 3] -
                                 x[(i + 2) % 3] * cross[(i + 1) % 3]) /
                                norm;
        return numbers(r, out, 3);
    }
    if (strstr(name, "-spline")) {
        if (n != ny || items(r, a[2]) != n || items(r, a[3]) != n)
            r->status = JOLT_ERR_ARGUMENT;
        for (size_t i = 0; i < n && !r->status; ++i)
            out[i] = cubic(num(r, a[4]), x[i], y[i], component(r, a[2], i), component(r, a[3], i),
                           named(name, "bezier-spline"));
        return numbers(r, out, n);
    }
    if (strncmp(name, "point-in-bbox", 13) == 0) {
        if (n != (named(name, "point-in-bbox2d") ? 2u : 3u) || ny != 2 * n) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        bool inside = true;
        for (size_t i = 0; i < n; ++i)
            inside = inside && x[i] >= y[i] && x[i] <= y[i + n];
        return boolean(inside);
    }
    r->status = JOLT_ERR_NOT_FOUND;
    return zero(0);
}
static value graphics_native(run *r, const char *name, const value *a, unsigned count)
{
    if (named(name, "convolve") || named(name, "box-filter")) {
        size_t n = items(r, a[0]);
        if (!n || (count == 2 && items(r, a[1]) != n))
            r->status = JOLT_ERR_ARGUMENT;
        double sum = 0, weights = 0;
        for (size_t i = 0; i < n && !r->status; ++i) {
            double w = count == 2 ? component(r, a[1], i) : 1;
            sum += component(r, a[0], i) * w;
            weights += w;
        }
        return number(r, sum / weights);
    }
    double x[8] = {0}, out[4] = {0, 0, 0, 1};
    for (unsigned i = 0; i < count; ++i)
        x[i] = num(r, a[i]);
    if (named(name, "gaussian-weight")) {
        if (x[1] <= 0)
            r->status = JOLT_ERR_ARGUMENT;
        return number(r, exp(-x[0] * x[0] / (2 * x[1] * x[1])) / (x[1] * sqrt(2 * PI)));
    }
    if (strncmp(name, "blend-", 6) == 0) {
        double s = x[0], d = x[1], v = s;
        if (named(name, "blend-multiply"))
            v = s * d;
        else if (named(name, "blend-screen"))
            v = 1 - (1 - s) * (1 - d);
        else if (named(name, "blend-overlay"))
            v = d < 0.5 ? 2 * s * d : 1 - 2 * (1 - s) * (1 - d);
        else if (named(name, "blend-darken"))
            v = fmin(s, d);
        else if (named(name, "blend-lighten"))
            v = fmax(s, d);
        else if (named(name, "blend-difference"))
            v = fabs(s - d);
        return number(r, v);
    }
    if (named(name, "rgb->hsv") || named(name, "rgb->hsl")) {
        double hi = fmax(x[0], fmax(x[1], x[2])), lo = fmin(x[0], fmin(x[1], x[2])),
               delta = hi - lo;
        double h = delta == 0   ? 0
                   : hi == x[0] ? modulo((x[1] - x[2]) / delta, 6)
                   : hi == x[1] ? (x[2] - x[0]) / delta + 2
                                : (x[0] - x[1]) / delta + 4;
        double light = (hi + lo) / 2;
        out[0] = h / 6;
        out[1] = named(name, "rgb->hsv") ? (hi == 0 ? 0 : delta / hi)
                                         : (delta == 0 ? 0 : delta / (1 - fabs(2 * light - 1)));
        out[2] = named(name, "rgb->hsv") ? hi : light;
        return numbers(r, out, 3);
    }
    if (named(name, "hsv->rgb") || named(name, "hsl->rgb")) {
        double h = modulo(x[0], 1) * 6,
               c = (named(name, "hsv->rgb") ? x[2] : 1 - fabs(2 * x[2] - 1)) * x[1],
               v = c * (1 - fabs(modulo(h, 2) - 1));
        double m = x[2] - (named(name, "hsv->rgb") ? c : c / 2);
        unsigned sector = (unsigned) floor(h);
        double rgb[6][3] = {{c, v, 0}, {v, c, 0}, {0, c, v}, {0, v, c}, {v, 0, c}, {c, 0, v}};
        for (unsigned i = 0; i < 3; ++i)
            out[i] = rgb[sector][i] + m;
        return numbers(r, out, 3);
    }
    if (named(name, "alpha-composite")) {
        out[3] = x[3] + x[7] * (1 - x[3]);
        for (unsigned i = 0; i < 3; ++i)
            out[i] = out[3] ? (x[i] * x[3] + x[i + 4] * x[7] * (1 - x[3])) / out[3] : 0;
        return numbers(r, out, 4);
    }
    double gray = 0.2126 * x[0] + 0.7152 * x[1] + 0.0722 * x[2];
    for (unsigned i = 0; i < 3; ++i) {
        if (named(name, "brightness"))
            out[i] = clamp01(x[i] * x[3]);
        else if (named(name, "contrast"))
            out[i] = clamp01((x[i] - 0.5) * x[3] + 0.5);
        else if (named(name, "saturation"))
            out[i] = clamp01(gray + (x[i] - gray) * x[3]);
        else if (named(name, "invert"))
            out[i] = 1 - x[i];
        else if (named(name, "grayscale"))
            out[i] = gray;
        else if (named(name, "sepia")) {
            const double coeff[3][3] = {{.393, .769, .189}, {.349, .686, .168}, {.272, .534, .131}};
            out[i] = clamp01(coeff[i][0] * x[0] + coeff[i][1] * x[1] + coeff[i][2] * x[2]);
        } else if (named(name, "gamma-correct"))
            out[i] = pow(x[i], x[3]);
        else if (named(name, "gamma-decode"))
            out[i] = x[i] <= .04045 ? x[i] / 12.92 : pow((x[i] + .055) / 1.055, 2.4);
        else if (named(name, "gamma-encode"))
            out[i] = x[i] <= .0031308 ? 12.92 * x[i] : 1.055 * pow(x[i], 1 / 2.4) - .055;
        else if (named(name, "color-temperature")) {
            if (x[3] < 1000 || x[3] > 40000)
                r->status = JOLT_ERR_ARGUMENT;
            double t = x[3] / 100;
            double channel = i == 0   ? (t <= 66 ? 255 : 329.698727446 * pow(t - 60, -.1332047592))
                             : i == 1 ? (t <= 66 ? 99.4708025861 * log(t) - 161.1195681661
                                                 : 288.1221695283 * pow(t - 60, -.0755148492))
                                      : (t >= 66   ? 255
                                         : t <= 19 ? 0
                                                   : 138.5177312231 * log(t - 10) - 305.0447927307);
            out[i] = x[i] * clamp01(channel / 255);
        } else
            r->status = JOLT_ERR_NOT_FOUND;
    }
    if (named(name, "invert"))
        out[3] = x[3];
    return numbers(r, out, named(name, "color-temperature") ? 3 : 4);
}
static value time_native(run *r, const char *name, const value *a, unsigned depth)
{
    if (named(name, "keyframe-value")) {
        size_t n = items(r, a[0]);
        double t = num(r, a[1]), lo = -INFINITY, hi = INFINITY, lv = 0, hv = 0;
        if (!n)
            return number(r, 0);
        for (size_t i = 0; i < n && !r->status; ++i) {
            value pair = at(r, a[0], i);
            if (items(r, pair) != 2)
                r->status = JOLT_ERR_ARGUMENT;
            double at_time = component(r, pair, 0), v = component(r, pair, 1);
            if (at_time <= t && at_time >= lo) {
                lo = at_time;
                lv = v;
            }
            if (at_time > t && at_time < hi) {
                hi = at_time;
                hv = v;
            }
        }
        return number(r, !isfinite(lo)   ? hv
                         : !isfinite(hi) ? lv
                                         : lv + (hv - lv) * (t - lo) / (hi - lo));
    }
    double t = num(r, a[0]);
    if (strncmp(name, "ease-", 5) == 0)
        return number(r, easing(name, t));
    if (named(name, "linear-curve"))
        return number(r, t);
    if (named(name, "fps->frame-time") || named(name, "frame-time->fps")) {
        if (t <= 0)
            r->status = JOLT_ERR_ARGUMENT;
        return number(r, 1 / t);
    }
    if (named(name, "seconds->frames"))
        return number(r, t * num(r, a[1]));
    if (named(name, "frames->seconds"))
        return number(r, t / num(r, a[1]));
    if (named(name, "bezier-curve") || named(name, "catmull-rom-curve"))
        return number(r, cubic(t, num(r, a[1]), num(r, a[2]), num(r, a[3]), num(r, a[4]),
                               named(name, "bezier-curve")));
    if (named(name, "animate")) {
        if (t <= 0)
            r->status = JOLT_ERR_ARGUMENT;
        value arg = number(r, clamp01(num(r, a[1]) / t));
        return invoke(r, a[2], &arg, 1, depth + 1);
    }
    if (named(name, "ping-pong")) {
        t = modulo(t, 2);
        return number(r, t < 1 ? t : 2 - t);
    }
    double duration = num(r, a[1]);
    if (duration <= 0)
        r->status = JOLT_ERR_ARGUMENT;
    return number(r, named(name, "loop-time") ? modulo(t, duration) : floor(t / duration));
}
static bool predicate(run *r, value v)
{
    if (v.value.type == 0)
        return false;
    if (!truth_type((uint32_t) v.value.type) || v.fn) {
        r->status = JOLT_ERR_TYPE;
        return false;
    }
    return truth(v);
}
static value generator(run *r, unsigned mode, const value *data, size_t count)
{
    value v = collection(r, 13, count);
    if (!r->status && count)
        memcpy(v.object->items, data, count * sizeof(value));
    v.fn = T_NATIVE + (uint32_t) STD_COUNT + mode;
    v.value.type = (jolt_type_t) T_FUNCTION;
    return v;
}
static value sequence_next(run *r, value fn, unsigned depth)
{
    if (r->status || !charge(r, 1) || depth >= JT_DEPTH) {
        r->status = JOLT_ERR_BUDGET;
        return zero(0);
    }
    value *s = fn.object->items;
    unsigned mode = fn.fn - T_NATIVE - (uint32_t) STD_COUNT;
    if (mode == 1) {
        uint32_t seed = (uint32_t) s[0].value.as.unsigned_integer;
        seed = seed * 1664525u + 1013904223u;
        s[0].value.as.unsigned_integer = seed;
        return number(r, (double) seed / 4294967296.0);
    }
    if (mode == 2) {
        if (s[2].value.as.unsigned_integer) {
            value previous = s[1];
            s[1] = invoke(r, s[0], &previous, 1, depth + 1);
        }
        s[2] = boolean(true);
        return s[1];
    }
    if (mode == 3)
        return invoke(r, s[0], NULL, 0, depth + 1);
    if (mode == 4) {
        size_t n = items(r, s[0]);
        if (!n)
            return zero(0);
        size_t i = (size_t) s[1].value.as.integer;
        s[1] = sint((int64_t) ((i + 1) % n));
        return at(r, s[0], i);
    }
    if (mode == 5) {
        int64_t cur = s[0].value.as.integer, end = s[1].value.as.integer,
                step = s[2].value.as.integer;
        if (s[3].value.as.unsigned_integer || (step > 0 ? cur >= end : cur <= end))
            return zero(0);
        if ((step > 0 && cur > INT64_MAX - step) || (step < 0 && cur < INT64_MIN - step))
            s[3] = boolean(true);
        else
            s[0] = sint(cur + step);
        return sint(cur);
    }
    if (mode == 6) {
        if (s[2].value.as.unsigned_integer)
            return zero(0);
        value next_state = invoke(r, s[0], &s[1], 1, depth + 1);
        if (next_state.value.type == 0) {
            s[2] = boolean(true);
            return zero(0);
        }
        if (items(r, next_state) != 2) {
            r->status = JOLT_ERR_TYPE;
            return zero(0);
        }
        s[1] = at(r, next_state, 1);
        return at(r, next_state, 0);
    }
    r->status = JOLT_ERR_BYTECODE;
    return zero(0);
}
static int map_index(run *r, value map, value key)
{
    size_t n = items(r, map);
    for (size_t i = 0; i < n && !r->status; i += 2)
        if (equal(r, map.object->items[i], key, 0))
            return (int) i;
    return -1;
}
static value assoc(run *r, value map, value key, value val)
{
    int i = map_index(r, map, key);
    size_t n = items(r, map);
    value out = collection(r, 14, n + (i < 0 ? 2u : 0u));
    if (!r->status) {
        if (n)
            memcpy(out.object->items, map.object->items, n * sizeof(value));
        size_t pos = i < 0 ? n : (size_t) i;
        out.object->items[pos] = key;
        out.object->items[pos + 1] = val;
    }
    return out;
}
static bool member(run *r, value coll, value item)
{
    size_t n = items(r, coll);
    for (size_t i = 0; i < n && !r->status; ++i)
        if (equal(r, at(r, coll, i), item, 0))
            return true;
    return false;
}
static value collections_native(run *r, const char *name, const value *a, unsigned count,
                                unsigned depth)
{
    if (named(name, "list") || named(name, "vector") || named(name, "set")) {
        value out = collection(r,
                               named(name, "list")  ? 12u
                               : named(name, "set") ? 15u
                                                    : 13u,
                               count);
        size_t used = 0;
        for (unsigned i = 0; i < count && !r->status; ++i) {
            bool seen = false;
            if (named(name, "set"))
                for (size_t j = 0; j < used; ++j)
                    if (equal(r, out.object->items[j], a[i], 0))
                        seen = true;
            if (!seen)
                out.object->items[used++] = a[i];
        }
        if (!r->status)
            out.object->count = used;
        return out;
    }
    if (named(name, "hash-map")) {
        if (count % 2) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        value out = collection(r, 14, 0);
        for (unsigned i = 0; i < count && !r->status; i += 2)
            out = assoc(r, out, a[i], a[i + 1]);
        return out;
    }
    if (named(name, "iterate")) {
        value state[] = {a[0], a[1], boolean(false)};
        return generator(r, 2, state, 3);
    }
    if (named(name, "repeatedly"))
        return generator(r, 3, a, 1);
    if (named(name, "cycle")) {
        value state[] = {a[0], sint(0)};
        return generator(r, 4, state, 2);
    }
    if (named(name, "lazy-seq")) {
        value state[] = {a[0], zero(0), boolean(false)};
        return generator(r, 6, state, 3);
    }
    if (named(name, "range-seq")) {
        int64_t step = count == 3 ? index_value(r, a[2]) : 1;
        if (!step)
            r->status = JOLT_ERR_ARGUMENT;
        value state[] = {sint(index_value(r, a[0])), sint(index_value(r, a[1])), sint(step),
                         boolean(false)};
        return generator(r, 5, state, 4);
    }
    if (named(name, "take") || named(name, "drop")) {
        int64_t wanted = index_value(r, a[0]);
        if (wanted < 0 || wanted > 4096) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        if (a[1].fn) {
            value out = collection(r, 13, named(name, "take") ? (size_t) wanted : 0);
            size_t used = 0;
            for (int64_t i = 0; i < wanted && !r->status; ++i) {
                value v = invoke(r, a[1], NULL, 0, depth + 1);
                if (v.value.type == 0 && !v.fn)
                    break;
                if (named(name, "take"))
                    out.object->items[used++] = v;
            }
            if (named(name, "drop"))
                return a[1];
            if (!r->status)
                out.object->count = used;
            return out;
        }
        size_t n = items(r, a[1]), k = (size_t) wanted < n ? (size_t) wanted : n;
        return named(name, "take") ? section(r, a[1], 0, k) : section(r, a[1], k, n);
    }
    if (named(name, "length") || named(name, "empty?") || named(name, "not-empty?")) {
        size_t n = items(r, a[0]);
        if (a[0].value.type == 14)
            n /= 2;
        return named(name, "length") ? sint((int64_t) n)
                                     : boolean(named(name, "empty?") ? n == 0 : n != 0);
    }
    if (named(name, "nth") || named(name, "vec-get") || named(name, "first") ||
        named(name, "last") || named(name, "vec-peek")) {
        size_t n = items(r, a[0]);
        int64_t i = count == 2 ? index_value(r, a[1]) : named(name, "first") ? 0 : (int64_t) n - 1;
        if (i < 0) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        return at(r, a[0], (size_t) i);
    }
    if (named(name, "rest") || named(name, "butlast") || named(name, "vec-pop") ||
        named(name, "slice")) {
        size_t n = items(r, a[0]);
        int64_t start = named(name, "rest")    ? (n ? 1 : 0)
                        : named(name, "slice") ? index_value(r, a[1])
                                               : 0;
        int64_t end = named(name, "slice")  ? (count == 3 ? index_value(r, a[2]) : (int64_t) n)
                      : named(name, "rest") ? (int64_t) n
                                            : (int64_t) n - 1;
        if (start < 0 || end < 0) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        return section(r, a[0], (size_t) start, (size_t) end);
    }
    if (named(name, "vec-set") || named(name, "vec-push")) {
        size_t n = items(r, a[0]);
        bool append = count == 2;
        int64_t i = append ? (int64_t) n : index_value(r, a[1]);
        if (i < 0 || (!append && (size_t) i >= n)) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        value out = collection(r, 13, n + (append ? 1u : 0u));
        if (!r->status) {
            if (n)
                memcpy(out.object->items, a[0].object->items, n * sizeof(value));
            out.object->items[i] = a[count - 1];
        }
        return out;
    }
    if (named(name, "concat") || named(name, "interleave")) {
        size_t total = 0, longest = 0;
        for (unsigned i = 0; i < count; ++i) {
            size_t n = items(r, a[i]);
            total += n;
            if (n > longest)
                longest = n;
        }
        value out = collection(r, 13, total);
        size_t k = 0;
        if (named(name, "concat")) {
            for (unsigned i = 0; i < count && !r->status; ++i)
                for (size_t j = 0; j < items(r, a[i]); ++j)
                    out.object->items[k++] = at(r, a[i], j);
        } else
            for (size_t j = 0; j < longest && !r->status; ++j)
                for (unsigned i = 0; i < count; ++i)
                    if (j < items(r, a[i]))
                        out.object->items[k++] = at(r, a[i], j);
        return out;
    }
    if (named(name, "interpose")) {
        size_t n = items(r, a[1]);
        value out = collection(r, 13, n ? 2 * n - 1 : 0);
        for (size_t i = 0; i < n && !r->status; ++i) {
            out.object->items[2 * i] = at(r, a[1], i);
            if (i + 1 < n)
                out.object->items[2 * i + 1] = a[0];
        }
        return out;
    }
    if (named(name, "partition") || named(name, "partition-all")) {
        int64_t width = index_value(r, a[0]);
        size_t n = items(r, a[1]);
        if (width <= 0 || width > 4096) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        size_t w = (size_t) width, groups = named(name, "partition") ? n / w : (n + w - 1) / w;
        value out = collection(r, 13, groups);
        for (size_t i = 0; i < groups && !r->status; ++i)
            out.object->items[i] = section(r, a[1], i * w, (i + 1) * w < n ? (i + 1) * w : n);
        return out;
    }
    if (named(name, "get") || named(name, "map-get")) {
        int i = map_index(r, a[0], a[1]);
        return i < 0 ? (count == 3 ? a[2] : zero(0)) : at(r, a[0], (size_t) i + 1);
    }
    if (named(name, "assoc"))
        return assoc(r, a[0], a[1], a[2]);
    if (named(name, "contains?"))
        return boolean(a[0].value.type == 14 ? map_index(r, a[0], a[1]) >= 0
                                             : member(r, a[0], a[1]));
    if (named(name, "dissoc")) {
        int found = map_index(r, a[0], a[1]);
        if (found < 0)
            return a[0];
        size_t n = items(r, a[0]);
        value out = collection(r, 14, n - 2);
        size_t k = 0;
        for (size_t i = 0; i < n && !r->status; ++i)
            if (i != (size_t) found && i != (size_t) found + 1)
                out.object->items[k++] = at(r, a[0], i);
        return out;
    }
    if (named(name, "map-get-in")) {
        value out = a[0];
        size_t n = items(r, a[1]);
        for (size_t i = 0; i < n && !r->status; ++i) {
            if (out.value.type != 14)
                return zero(0);
            int pos = map_index(r, out, at(r, a[1], i));
            if (pos < 0)
                return zero(0);
            out = at(r, out, (size_t) pos + 1);
        }
        return out;
    }
    if (named(name, "map-keys") || named(name, "map-vals")) {
        size_t n = items(r, a[0]) / 2;
        value out = collection(r, 13, n);
        for (size_t i = 0; i < n && !r->status; ++i)
            out.object->items[i] = at(r, a[0], i * 2 + (named(name, "map-vals") ? 1u : 0u));
        return out;
    }
    if (named(name, "map-merge") || named(name, "map-invert")) {
        value out = collection(r, 14, 0);
        for (unsigned i = 0; i < count && !r->status; ++i) {
            size_t n = items(r, a[i]);
            for (size_t j = 0; j < n && !r->status; j += 2)
                out = assoc(r, out, at(r, a[i], j + (named(name, "map-invert") ? 1u : 0u)),
                            at(r, a[i], j + (named(name, "map-invert") ? 0u : 1u)));
        }
        return out;
    }
    if (named(name, "zipmap")) {
        size_t n = items(r, a[0]), other = items(r, a[1]);
        if (n > other)
            n = other;
        value out = collection(r, 14, 0);
        for (size_t i = 0; i < n && !r->status; ++i)
            out = assoc(r, out, at(r, a[0], i), at(r, a[1], i));
        return out;
    }
    if (named(name, "frequencies") || named(name, "group-by")) {
        value coll = a[count - 1], out = collection(r, 14, 0);
        size_t n = items(r, coll);
        for (size_t i = 0; i < n && !r->status; ++i) {
            value item = at(r, coll, i),
                  key = named(name, "frequencies") ? item : invoke(r, a[0], &item, 1, depth + 1);
            int found = map_index(r, out, key);
            value old = found < 0 ? zero(0) : at(r, out, (size_t) found + 1), val;
            if (named(name, "frequencies"))
                val = sint(found < 0 ? 1 : old.value.as.integer + 1);
            else {
                size_t len = found < 0 ? 0 : items(r, old);
                val = collection(r, 13, len + 1);
                if (!r->status) {
                    if (len)
                        memcpy(val.object->items, old.object->items, len * sizeof(value));
                    val.object->items[len] = item;
                }
            }
            out = assoc(r, out, key, val);
        }
        return out;
    }
    if (strncmp(name, "set-", 4) == 0) {
        size_t n = items(r, a[0]), m = items(r, a[1]);
        value out = collection(r, 15, n + (named(name, "set-union") ? m : 0));
        size_t used = 0;
        for (size_t i = 0; i < n + m && !r->status; ++i) {
            if (i >= n && !named(name, "set-union"))
                break;
            value item = i < n ? at(r, a[0], i) : at(r, a[1], i - n);
            bool include = named(name, "set-union") ||
                           member(r, a[1], item) == named(name, "set-intersection");
            bool seen = false;
            for (size_t j = 0; j < used; ++j)
                if (equal(r, out.object->items[j], item, 0))
                    seen = true;
            if (include && !seen)
                out.object->items[used++] = item;
        }
        if (!r->status)
            out.object->count = used;
        return out;
    }
    if (named(name, "apply") || named(name, "apply-kv")) {
        size_t n = items(r, a[1]);
        if (n > 32) {
            r->status = JOLT_ERR_BUDGET;
            return zero(0);
        }
        return invoke(r, a[0], a[1].object ? a[1].object->items : NULL, (unsigned) n, depth + 1);
    }
    if (named(name, "reduce") || named(name, "reduce-kv")) {
        value out = a[1];
        size_t n = items(r, a[2]);
        bool kv = named(name, "reduce-kv");
        for (size_t i = 0; i < n && !r->status; i += kv ? 2u : 1u) {
            value args[3] = {out, at(r, a[2], i), zero(0)};
            if (kv)
                args[2] = at(r, a[2], i + 1);
            out = invoke(r, a[0], args, kv ? 3u : 2u, depth + 1);
        }
        return out;
    }
    if (named(name, "map")) {
        size_t n = 4096;
        for (unsigned i = 1; i < count; ++i) {
            size_t len = items(r, a[i]);
            if (len < n)
                n = len;
        }
        value out = collection(r, 13, n);
        for (size_t i = 0; i < n && !r->status; ++i) {
            value args[32];
            for (unsigned j = 1; j < count; ++j)
                args[j - 1] = at(r, a[j], i);
            out.object->items[i] = invoke(r, a[0], args, count - 1, depth + 1);
        }
        return out;
    }
    if (named(name, "sort")) {
        size_t n = items(r, a[0]);
        value out = section(r, a[0], 0, n);
        for (size_t i = 1; i < n && !r->status; ++i) {
            value current = out.object->items[i];
            size_t j = i;
            while (j && !r->status) {
                value pair[] = {current, out.object->items[j - 1]};
                if (!predicate(r, invoke(r, a[1], pair, 2, depth + 1)))
                    break;
                out.object->items[j] = out.object->items[j - 1];
                --j;
            }
            out.object->items[j] = current;
        }
        return out;
    }
    size_t n = items(r, a[1]);
    value out = collection(r, 13, n);
    size_t used = 0;
    bool dropping = true, all = true, any = false;
    for (size_t i = 0; i < n && !r->status; ++i) {
        value item = at(r, a[1], i), args[2] = {sint((int64_t) i), item};
        value result = invoke(r, a[0], named(name, "map-indexed") ? args : &item,
                              named(name, "map-indexed") ? 2u : 1u, depth + 1);
        bool yes = (named(name, "keep") || named(name, "map-indexed")) ? result.value.type != 0
                                                                       : predicate(r, result);
        any = any || yes;
        all = all && yes;
        if (named(name, "some") && yes)
            return boolean(true);
        if (named(name, "every?") && !yes)
            return boolean(false);
        if (named(name, "take-while") && !yes)
            break;
        if (named(name, "drop-while")) {
            if (!yes)
                dropping = false;
            if (!dropping) {
                if (!r->status)
                    out.object->items[used++] = item;
                for (size_t j = i + 1; j < n && !r->status; ++j)
                    out.object->items[used++] = at(r, a[1], j);
                break;
            }
            continue;
        }
        bool include = named(name, "map-indexed") || named(name, "take-while") ||
                       (named(name, "remove") ? !yes : yes);
        if (include && !r->status)
            out.object->items[used++] =
                (named(name, "keep") || named(name, "map-indexed")) ? result : item;
    }
    if (named(name, "some") || named(name, "every?"))
        return boolean(named(name, "some") ? any : all);
    if (!r->status)
        out.object->count = used;
    return out;
}
typedef struct {
    char *data;
    size_t size, capacity;
} text_builder;
static void append(run *r, text_builder *b, const char *text, size_t len)
{
    if (r->status || !charge(r, len))
        return;
    if (len > 1024u * 1024u - b->size) {
        r->status = JOLT_ERR_BUDGET;
        return;
    }
    if (b->size + len > b->capacity) {
        size_t cap = b->capacity ? b->capacity : 128;
        while (cap < b->size + len)
            cap *= 2;
        char *p = run_alloc(r, cap);
        if (!p)
            return;
        if (b->size)
            memcpy(p, b->data, b->size);
        b->data = p;
        b->capacity = cap;
    }
    if (len)
        memcpy(b->data + b->size, text, len);
    b->size += len;
}
static value built_string(run *r, text_builder b)
{
    return string_value(r, b.data, b.size);
}
static size_t rune(const char *s, size_t len, size_t at, uint32_t *cp)
{
    if (at >= len) {
        *cp = 0;
        return at;
    }
    unsigned char c = (unsigned char) s[at++];
    unsigned n = c < 128 ? 0u : c < 224 ? 1u : c < 240 ? 2u : 3u;
    *cp = n ? c & ((1u << (6 - n)) - 1u) : c;
    while (n-- && at < len)
        *cp = (*cp << 6) | ((unsigned char) s[at++] & 63u);
    return at;
}
static size_t rune_count(const char *s, size_t len)
{
    size_t count = 0, at = 0;
    uint32_t cp;
    while (at < len) {
        at = rune(s, len, at, &cp);
        ++count;
    }
    return count;
}
static size_t rune_offset(const char *s, size_t len, size_t index)
{
    size_t at = 0;
    uint32_t cp;
    while (index-- && at < len)
        at = rune(s, len, at, &cp);
    return at;
}
static bool whitespace(uint32_t cp)
{
    return (cp >= 9 && cp <= 13) || cp == 32 || cp == 0x85 || cp == 0xa0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 || cp == 0x202f ||
           cp == 0x205f || cp == 0x3000;
}
static size_t search_text(run *r, const char *s, size_t len, const char *needle, size_t n,
                          size_t start)
{
    if (n > len || start > len - n)
        return SIZE_MAX;
    for (size_t i = start; i <= len - n && !r->status; ++i) {
        if (!charge(r, n ? n : 1))
            return SIZE_MAX;
        if (!n || !memcmp(s + i, needle, n))
            return i;
    }
    return SIZE_MAX;
}
static void format_value(run *r, text_builder *b, value v, unsigned precision, bool fixed)
{
    char buffer[384];
    int len = 0;
    switch (v.value.type) {
    case 0:
        append(r, b, "()", 2);
        return;
    case 1:
    case 2:
        len = snprintf(buffer, sizeof(buffer), "%" PRId64, v.value.as.integer);
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        len = snprintf(buffer, sizeof(buffer), "%" PRIu64, v.value.as.unsigned_integer);
        break;
    case 7:
    case 8:
        len = fixed ? snprintf(buffer, sizeof(buffer), "%.*f", (int) precision, v.value.as.number)
                    : snprintf(buffer, sizeof(buffer), "%.17g", v.value.as.number);
        break;
    case 9:
        append(r, b, v.value.as.unsigned_integer ? "true" : "false",
               v.value.as.unsigned_integer ? 4 : 5);
        return;
    case 10:
        len = (int) encode(buffer, (uint32_t) v.value.as.unsigned_integer);
        break;
    case 11:
        append(r, b, v.value.as.string.data, v.value.as.string.size);
        return;
    default:
        r->status = JOLT_ERR_TYPE;
        return;
    }
    if (len < 0 || (size_t) len >= sizeof(buffer)) {
        r->status = JOLT_ERR_BUDGET;
        return;
    }
    append(r, b, buffer, (size_t) len);
}
static void *regex_alloc(PCRE2_SIZE size, void *user)
{
    return run_alloc(user, size);
}
static void regex_free(void *p, void *user)
{
    (void) p;
    (void) user;
}
static int regex_callout(pcre2_callout_block *block, void *user)
{
    (void) block;
    return charge(user, 1) ? 0 : PCRE2_ERROR_CALLOUT;
}
static value regex_native(run *r, const char *name, const value *a)
{
    const char *pattern = a[0].value.as.string.data, *subject = a[1].value.as.string.data;
    size_t pn = a[0].value.as.string.size, sn = a[1].value.as.string.size;
    if (pn > 1024 || !charge(r, pn + sn + 1)) {
        r->status = JOLT_ERR_BUDGET;
        return zero(0);
    }
    pcre2_general_context *general = pcre2_general_context_create(regex_alloc, regex_free, r);
    if (!general)
        return zero(0);
    pcre2_compile_context *compile = pcre2_compile_context_create(general);
    if (!compile)
        return zero(0);
    int error;
    PCRE2_SIZE offset;
    pcre2_code *code =
        pcre2_compile((PCRE2_SPTR) pattern, pn, PCRE2_UTF | PCRE2_UCP | PCRE2_AUTO_CALLOUT, &error,
                      &offset, compile);
    if (!code) {
        if (!r->status)
            r->status = JOLT_ERR_SYNTAX;
        return zero(0);
    }
    pcre2_match_context *context = pcre2_match_context_create(general);
    pcre2_match_data *data = pcre2_match_data_create_from_pattern(code, general);
    if (!context || !data)
        return zero(0);
    pcre2_set_match_limit(context, (uint32_t) (r->steps < 100000 ? r->steps + 1 : 100000));
    pcre2_set_depth_limit(context, 128);
    pcre2_set_heap_limit(context, 1024);
    pcre2_set_callout(context, regex_callout, r);
    value out = zero(0);
    int result;
    if (named(name, "regex-replace")) {
        PCRE2_SIZE size = 1024u * 1024u;
        char *buffer = run_alloc(r, size);
        if (!buffer)
            return out;
        result = pcre2_substitute(code, (PCRE2_SPTR) subject, sn, 0,
                                  PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED, data,
                                  context, (PCRE2_SPTR) a[2].value.as.string.data,
                                  a[2].value.as.string.size, (PCRE2_UCHAR *) buffer, &size);
        if (result >= 0)
            out = string_value(r, buffer, size);
    } else {
        result = pcre2_match(code, (PCRE2_SPTR) subject, sn, 0, 0, data, context);
        if (named(name, "regex-match?"))
            out = boolean(result >= 0);
        else if (result >= 0) {
            PCRE2_SIZE *positions = pcre2_get_ovector_pointer(data);
            out = string_value(r, subject + positions[0], positions[1] - positions[0]);
        }
    }
    if (result < 0 && result != PCRE2_ERROR_NOMATCH && !r->status)
        r->status = (result == PCRE2_ERROR_MATCHLIMIT || result == PCRE2_ERROR_DEPTHLIMIT ||
                     result == PCRE2_ERROR_HEAPLIMIT || result == PCRE2_ERROR_NOMEMORY)
                        ? JOLT_ERR_BUDGET
                        : JOLT_ERR_ARGUMENT;
    pcre2_match_data_free(data);
    pcre2_match_context_free(context);
    pcre2_code_free(code);
    pcre2_compile_context_free(compile);
    pcre2_general_context_free(general);
    return out;
}
static value strings_native(run *r, const char *name, const value *a, unsigned count)
{
    if (strncmp(name, "regex-", 6) == 0)
        return regex_native(r, name, a);
    if (named(name, "whitespace?"))
        return boolean(whitespace((uint32_t) a[0].value.as.unsigned_integer));
    text_builder out = {0};
    if (named(name, "format-int") || named(name, "format-float")) {
        if (named(name, "format-int") && !integer((uint32_t) a[0].value.type)) {
            r->status = JOLT_ERR_TYPE;
            return zero(0);
        }
        int64_t precision = count == 2 ? index_value(r, a[1]) : 0;
        if (precision < 0 || precision > 17)
            r->status = JOLT_ERR_ARGUMENT;
        value v = a[0];
        if (count == 2)
            v = number(r, num(r, v));
        format_value(r, &out, v, (unsigned) precision, count == 2);
        return built_string(r, out);
    }
    if (named(name, "str-concat")) {
        for (unsigned i = 0; i < count; ++i)
            append(r, &out, a[i].value.as.string.data, a[i].value.as.string.size);
        return built_string(r, out);
    }
    const char *s = a[0].value.as.string.data;
    size_t len = a[0].value.as.string.size;
    if (!charge(r, len + 1))
        return zero(0);
    if (named(name, "str-join")) {
        size_t n = items(r, a[1]);
        for (size_t i = 0; i < n && !r->status; ++i) {
            value item = at(r, a[1], i);
            if (item.value.type != 11) {
                r->status = JOLT_ERR_TYPE;
                break;
            }
            if (i)
                append(r, &out, s, len);
            append(r, &out, item.value.as.string.data, item.value.as.string.size);
        }
        return built_string(r, out);
    }
    if (named(name, "str-length"))
        return sint((int64_t) rune_count(s, len));
    if (named(name, "str-empty?") || named(name, "str-not-empty?"))
        return boolean(named(name, "str-empty?") ? !len : len != 0);
    if (named(name, "char-at") || named(name, "substring")) {
        int64_t start = index_value(r, a[1]);
        if (start == INT64_MAX) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        int64_t end = count == 3 ? index_value(r, a[2]) : start + 1;
        size_t n = rune_count(s, len);
        if (start < 0 || end < start || (uint64_t) end > n) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        size_t begin = rune_offset(s, len, (size_t) start),
               finish = rune_offset(s, len, (size_t) end);
        if (named(name, "char-at")) {
            uint32_t cp;
            rune(s, len, begin, &cp);
            value v = zero(10);
            v.value.as.unsigned_integer = cp;
            return v;
        }
        return string_value(r, s + begin, finish - begin);
    }
    if (strncmp(name, "str-trim", 8) == 0) {
        size_t begin = 0, end = len, at_byte = 0, last = 0;
        uint32_t cp;
        if (!named(name, "str-trim-right"))
            while (begin < len) {
                size_t next_byte = rune(s, len, begin, &cp);
                if (!whitespace(cp))
                    break;
                begin = next_byte;
            }
        if (!named(name, "str-trim-left")) {
            at_byte = begin;
            last = begin;
            while (at_byte < len) {
                at_byte = rune(s, len, at_byte, &cp);
                if (!whitespace(cp))
                    last = at_byte;
            }
            end = last;
        }
        return string_value(r, s + begin, end - begin);
    }
    if (named(name, "str-upper") || named(name, "str-lower") || named(name, "str-capitalize")) {
        value v = string_value(r, s, len);
        if (r->status)
            return v;
        char *p = (char *) v.value.as.string.data;
        for (size_t i = 0; i < len; ++i) {
            bool upper = named(name, "str-upper") || (named(name, "str-capitalize") && i == 0);
            if (upper && p[i] >= 'a' && p[i] <= 'z')
                p[i] = (char) (p[i] - 32);
            if (!upper && p[i] >= 'A' && p[i] <= 'Z')
                p[i] = (char) (p[i] + 32);
        }
        return v;
    }
    if (named(name, "str-reverse")) {
        size_t end = len;
        while (end) {
            size_t begin = end - 1;
            while (begin && ((unsigned char) s[begin] & 0xc0) == 0x80)
                --begin;
            append(r, &out, s + begin, end - begin);
            end = begin;
        }
        return built_string(r, out);
    }
    if (named(name, "str-repeat") || named(name, "str-pad-left") || named(name, "str-pad-right")) {
        int64_t times = index_value(r, a[1]);
        if (times < 0 || times > 1048576) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        if (named(name, "str-repeat")) {
            for (int64_t i = 0; i < times && !r->status; ++i) {
                if (!charge(r, 1))
                    break;
                append(r, &out, s, len);
            }
            return built_string(r, out);
        }
        size_t n = rune_count(s, len), target = (size_t) times;
        if (target <= n)
            return a[0];
        const char *padding = a[2].value.as.string.data;
        size_t plen = a[2].value.as.string.size;
        if (rune_count(padding, plen) != 1) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        if (named(name, "str-pad-right"))
            append(r, &out, s, len);
        for (size_t i = n; i < target && !r->status; ++i)
            append(r, &out, padding, plen);
        if (named(name, "str-pad-left"))
            append(r, &out, s, len);
        return built_string(r, out);
    }
    if (strncmp(name, "parse-", 6) == 0) {
        if (!len || len > 127 || memchr(s, 0, len)) {
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        char buffer[128];
        memcpy(buffer, s, len);
        buffer[len] = 0;
        if (named(name, "parse-bool")) {
            for (size_t i = 0; i < len; ++i)
                if (buffer[i] >= 'A' && buffer[i] <= 'Z')
                    buffer[i] = (char) (buffer[i] + 32);
            if (named(buffer, "true") || named(buffer, "t") || named(buffer, "yes") ||
                named(buffer, "1"))
                return boolean(true);
            if (named(buffer, "false") || named(buffer, "f") || named(buffer, "no") ||
                named(buffer, "0"))
                return boolean(false);
            r->status = JOLT_ERR_ARGUMENT;
            return zero(0);
        }
        for (size_t i = 0; i < len; ++i)
            if (!strchr(named(name, "parse-int") ? "0123456789+-" : "0123456789+-.eE", buffer[i])) {
                r->status = JOLT_ERR_ARGUMENT;
                return zero(0);
            }
        char *end;
        errno = 0;
        if (named(name, "parse-int")) {
            int64_t v = strtoll(buffer, &end, 10);
            if (errno || end != buffer + len || end == buffer)
                r->status = JOLT_ERR_ARGUMENT;
            return sint(v);
        }
        double v = strtod(buffer, &end);
        if (errno || end != buffer + len || end == buffer || !isfinite(v))
            r->status = JOLT_ERR_ARGUMENT;
        return number(r, v);
    }
    if (named(name, "format")) {
        unsigned sequential = 1;
        for (size_t i = 0; i < len && !r->status;) {
            if (s[i] == '{' && i + 1 < len && s[i + 1] == '{') {
                append(r, &out, "{", 1);
                i += 2;
                continue;
            }
            if (s[i] == '}' && i + 1 < len && s[i + 1] == '}') {
                append(r, &out, "}", 1);
                i += 2;
                continue;
            }
            if (s[i] == '{') {
                ++i;
                unsigned arg = 0;
                bool indexed = false;
                while (i < len && s[i] >= '0' && s[i] <= '9') {
                    indexed = true;
                    arg = arg * 10u + (unsigned) (s[i++] - '0');
                    if (arg > 32)
                        break;
                }
                if (i >= len || s[i++] != '}') {
                    r->status = JOLT_ERR_ARGUMENT;
                    break;
                }
                arg = indexed ? arg + 1 : sequential++;
                if (arg >= count) {
                    r->status = JOLT_ERR_ARGUMENT;
                    break;
                }
                format_value(r, &out, a[arg], 0, false);
            } else if (s[i] == '}') {
                r->status = JOLT_ERR_ARGUMENT;
                break;
            } else
                append(r, &out, s + i++, 1);
        }
        return built_string(r, out);
    }
    const char *other = a[1].value.as.string.data;
    size_t olen = a[1].value.as.string.size;
    if (named(name, "str-compare") || named(name, "str-equals?") || named(name, "str-less?") ||
        named(name, "str-greater?")) {
        int cmp = (len && olen) ? memcmp(s, other, len < olen ? len : olen) : 0;
        if (!cmp)
            cmp = len < olen ? -1 : len > olen ? 1 : 0;
        if (named(name, "str-compare")) {
            value v = zero(1);
            v.value.as.integer = cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
            return v;
        }
        return boolean(named(name, "str-equals?") ? cmp == 0
                       : named(name, "str-less?") ? cmp < 0
                                                  : cmp > 0);
    }
    if (named(name, "str-starts-with?") || named(name, "str-ends-with?"))
        return boolean(
            olen <= len &&
            (!olen || !memcmp(s + (named(name, "str-ends-with?") ? len - olen : 0), other, olen)));
    if (named(name, "str-contains?"))
        return boolean(search_text(r, s, len, other, olen, 0) != SIZE_MAX);
    if (!olen) {
        r->status = JOLT_ERR_ARGUMENT;
        return zero(0);
    }
    if (named(name, "str-split")) {
        size_t start = 0, n = 1, pos;
        while ((pos = search_text(r, s, len, other, olen, start)) != SIZE_MAX && !r->status) {
            ++n;
            start = pos + olen;
        }
        value v = collection(r, 13, n);
        start = 0;
        size_t i = 0;
        while (!r->status && (pos = search_text(r, s, len, other, olen, start)) != SIZE_MAX) {
            v.object->items[i++] = string_value(r, s + start, pos - start);
            start = pos + olen;
        }
        if (!r->status)
            v.object->items[i] = string_value(r, s + start, len - start);
        return v;
    }
    size_t start = 0, pos;
    while (!r->status && (pos = search_text(r, s, len, other, olen, start)) != SIZE_MAX) {
        append(r, &out, s + start, pos - start);
        append(r, &out, a[2].value.as.string.data, a[2].value.as.string.size);
        start = pos + olen;
        if (named(name, "str-replace"))
            break;
    }
    append(r, &out, s + start, len - start);
    return built_string(r, out);
}
static value native(run *r, unsigned id, const value *a, unsigned count, unsigned depth)
{
    if (r->status || !charge(r, 1) || depth >= JT_DEPTH) {
        if (!r->status)
            r->status = JOLT_ERR_BUDGET;
        return zero(0);
    }
    if (id >= STD_COUNT) {
        r->status = JOLT_ERR_BYTECODE;
        return zero(0);
    }
    const std_entry *entry = &stdlib[id];
    if (count < entry->min || count > entry->max) {
        r->status = JOLT_ERR_ARGUMENT;
        return zero(0);
    }
    for (unsigned i = 0; i < count; ++i) {
        unsigned expected = entry->types[i < entry->params ? i : entry->params - 1];
        if (!assignable(expected, a[i].fn ? T_FUNCTION : (unsigned) a[i].value.type)) {
            r->status = JOLT_ERR_TYPE;
            return zero(0);
        }
    }
    const char *name = entry->name;
    if (named(name, "random-seed"))
        return generator(r, 1, a, 1);
    if (entry->group == 0) {
        if (named(name, "dot") || named(name, "length") || named(name, "normalize") ||
            named(name, "cross") || strncmp(name, "mat2-", 5) == 0)
            return geometry_native(r, name, a, count);
        return math_native(r, name, a, count);
    }
    if (entry->group == 1)
        return graphics_native(r, name, a, count);
    if (entry->group == 2)
        return geometry_native(r, name, a, count);
    if (entry->group == 3)
        return time_native(r, name, a, depth);
    if (entry->group == 4)
        return collections_native(r, name, a, count, depth);
    return strings_native(r, name, a, count);
}
