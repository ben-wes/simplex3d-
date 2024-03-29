#include <math.h>
#include <string.h>
#include "m_pd.h"

static t_class *simplex3d_tilde_class;

typedef struct _simplex3d_tilde {
    t_object  x_obj;
    t_outlet *x_out;
    int p[256];
} t_simplex3d_tilde;

unsigned int lcg_next(unsigned int *seed) {
    const unsigned int a = 1664525;
    const unsigned int c = 1013904223;
    *seed = (a * (*seed) + c); // Modulo 2^32 is implicit due to unsigned int overflow
    return *seed;
}

void shuffle(int *array, int n, unsigned int seed) {
    for (int i = n - 1; i > 0; i--) {
        unsigned int rand = lcg_next(&seed);
        int j = rand % (i + 1);
        int temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}

void initPermutation(t_simplex3d_tilde *x, unsigned int seed) {
    int basePermutation[256];
    for (int i = 0; i < 256; i++) {
        basePermutation[i] = i;
    }
    shuffle(basePermutation, 256, seed);
    for (int i = 0; i < 256; i++) {
        x->p[i] = basePermutation[i];
    }
}

static inline int fastfloor(float x) {
    return x > 0 ? (int)x : (int)x - 1;
}

static inline uint8_t hash(t_simplex3d_tilde *x, int i) {
    return x->p[(uint8_t)i]; // Assuming x->p is your permutation array within t_simplex3d_tilde
}

static inline t_float grad(int hash, t_float x, t_float y, t_float z) {
    int h = hash & 15;
    t_float u = h < 8 ? x : y;
    t_float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) ? -u : u) + ((h & 2) == 0 ? v : -v);
}

// Gradient vectors for 3D simplex noise
// static int grad3[][3] = {
//     {1,1,0}, {-1,1,0}, {1,-1,0}, {-1,-1,0},
//     {1,0,1}, {-1,0,1}, {1,0,-1}, {-1,0,-1},
//     {0,1,1}, {0,-1,1}, {0,1,-1}, {0,-1,-1}
// };

static inline t_float dot(int g[], t_float x, t_float y, t_float z) {
    return g[0] * x + g[1] * y + g[2] * z;
}

// 3D simplex noise function
t_float simplex3d(t_simplex3d_tilde *x, t_float xin, t_float yin, t_float zin) {
    t_float n0, n1, n2, n3;  // Noise contributions from the four simplex corners

    // Skewing and unskewing factors for 3 dimensions
    static t_float F3 = 1.0 / 3.0;
    static t_float G3 = 1.0 / 6.0;

    // Skew the input space to determine which simplex cell we're in
    t_float s = (xin + yin + zin) * F3;  // Very nice and simple skew factor for 3D
    int i = fastfloor(xin + s);
    int j = fastfloor(yin + s);
    int k = fastfloor(zin + s);
    t_float t = (i + j + k) * G3;
    t_float X0 = i - t;  // Unskew the cell origin back to (x, y, z) space
    t_float Y0 = j - t;
    t_float Z0 = k - t;
    t_float x0 = xin - X0;  // The x, y, z distances from the cell origin
    t_float y0 = yin - Y0;
    t_float z0 = zin - Z0;
    // Determine which simplex we are in.
    int i1, j1, k1;  // Offsets for second corner of simplex in (i, j, k) coords
    int i2, j2, k2;  // Offsets for third corner of simplex in (i, j, k) coords
    if (x0 >= y0) {
        if (y0 >= z0) {
            i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0;  // X Y Z order
        } else if (x0 >= z0) {
            i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1;  // X Z Y order
        } else {
            i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1;  // Z X Y order
        }
    } else {  // x0 < y0
        if (y0 < z0) {
            i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1;  // Z Y X order
        } else if (x0 < z0) {
            i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1;  // Y Z X order
        } else {
            i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0;  // Y X Z order
        }
    }
    // A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
    // a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), and
    // a step of (0,0,1) in (i,j,k) means a step of (-c,-c,1-c) in (x,y,z), where
    // c = 1/6.

    t_float x1 = x0 - i1 + G3; // Offsets for second corner in (x,y,z) coords
    t_float y1 = y0 - j1 + G3;
    t_float z1 = z0 - k1 + G3;
    t_float x2 = x0 - i2 + 2.0 * G3; // Offsets for third corner in (x,y,z) coords
    t_float y2 = y0 - j2 + 2.0 * G3;
    t_float z2 = z0 - k2 + 2.0 * G3;
    t_float x3 = x0 - 1.0 + 3.0 * G3; // Offsets for last corner in (x,y,z) coords
    t_float y3 = y0 - 1.0 + 3.0 * G3;
    t_float z3 = z0 - 1.0 + 3.0 * G3;

    int gi0 = hash(x, i + hash(x, j + hash(x, k)));
    int gi1 = hash(x, i + i1 + hash(x, j + j1 + hash(x, k + k1)));
    int gi2 = hash(x, i + i2 + hash(x, j + j2 + hash(x, k + k2)));
    int gi3 = hash(x, i + 1 + hash(x, j + 1 + hash(x, k + 1)));

    // Calculate the contribution from the four corners
    // t_float t0 = 0.6 - x0*x0 - y0*y0 - z0*z0;
    // if (t0 < 0) n0 = 0.0;
    // else {
    //     t0 *= t0;
    //     n0 = t0 * t0 * dot(grad3[gi0], x0, y0, z0);
    // }
    // t_float t1 = 0.6 - x1*x1 - y1*y1 - z1*z1;
    // if (t1 < 0) n1 = 0.0;
    // else {
    //     t1 *= t1;
    //     n1 = t1 * t1 * dot(grad3[gi1], x1, y1, z1);
    // }
    // t_float t2 = 0.6 - x2*x2 - y2*y2 - z2*z2;
    // if (t2 < 0) n2 = 0.0;
    // else {
    //     t2 *= t2;
    //     n2 = t2 * t2 * dot(grad3[gi2], x2, y2, z2);
    // }
    // t_float t3 = 0.6 - x3*x3 - y3*y3 - z3*z3;
    // if (t3 < 0) n3 = 0.0;
    // else {
    //     t3 *= t3;
    //     n3 = t3 * t3 * dot(grad3[gi3], x3, y3, z3);
    // }
    t_float t0 = 0.6 - x0*x0 - y0*y0 - z0*z0;
    if (t0 < 0) {
        n0 = 0.0;
    } else {
        t0 *= t0;
        n0 = t0 * t0 * grad(gi0, x0, y0, z0);
    }
    t_float t1 = 0.6 - x1*x1 - y1*y1 - z1*z1;
    if (t1 < 0) {
        n1 = 0.0;
    } else {
        t1 *= t1;
        n1 = t1 * t1 * grad(gi1, x1, y1, z1);
    }
    t_float t2 = 0.6 - x2*x2 - y2*y2 - z2*z2;
    if (t2 < 0) {
        n2 = 0.0;
    } else {
        t2 *= t2;
        n2 = t2 * t2 * grad(gi2, x2, y2, z2);
    }
    t_float t3 = 0.6 - x3*x3 - y3*y3 - z3*z3;
    if (t3 < 0) {
        n3 = 0.0;
    } else {
        t3 *= t3;
        n3 = t3 * t3 * grad(gi3, x3, y3, z3);
    }
    // Add contributions from each corner to get the final noise value.
    // The result is scaled to stay just inside [-1,1]
    return 32.0 * (n0 + n1 + n2 + n3);
}

static t_int *simplex3d_tilde_perform(t_int *w) {
    t_simplex3d_tilde *x = (t_simplex3d_tilde *)(w[1]);
    t_float *in1 = (t_float *)(w[2]);
    t_float *in2 = (t_float *)(w[3]);
    t_float *in3 = (t_float *)(w[4]);
    t_sample *out = (t_sample *)(w[5]);
    int n = (int)(w[6]);

    while (n--) {
        t_float x_coord = *(in1++);
        t_float y_coord = *(in2++);
        t_float z_coord = *(in3++);
        *out++ = simplex3d(x, x_coord, y_coord, z_coord);
    }
    return (w + 7);
}

void simplex3d_tilde_dsp(t_simplex3d_tilde *x, t_signal **sp) {
    dsp_add(simplex3d_tilde_perform, 6, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[0]->s_n);
}

void *simplex3d_tilde_new(void) {
    t_simplex3d_tilde *x = (t_simplex3d_tilde *)pd_new(simplex3d_tilde_class);
    initPermutation(x, 0);

    inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);

    x->x_out = outlet_new(&x->x_obj, &s_signal);

    return (void *)x;
}

void simplex3d_tilde_setup(void) {
    simplex3d_tilde_class = class_new(gensym("simplex3d~"),
                                     (t_newmethod)simplex3d_tilde_new,
                                     0,
                                     sizeof(t_simplex3d_tilde),
                                     CLASS_DEFAULT,
                                     A_NULL);
    class_addmethod(simplex3d_tilde_class, (t_method)simplex3d_tilde_dsp, gensym("dsp"), A_CANT, 0);
    CLASS_MAINSIGNALIN(simplex3d_tilde_class, t_simplex3d_tilde, x_obj);
}
