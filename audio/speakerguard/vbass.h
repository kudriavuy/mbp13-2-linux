/*
 * Psychoacoustic bass, in the spirit of Asahi Linux's bankstown: the bass
 * the woofers cannot play (below 'freq') is replaced by its harmonics,
 * which they can, and the ear fills in the fundamental.
 *
 *   low  = LR4 low-pass at freq of (L + R) / 2
 *   h    = |low| - 2/pi * env(|low|)     full-wave rectifier: 2nd, 4th, ...
 *                                        harmonics; |k x| = k |x|, so their
 *                                        level follows the bass level linearly
 *                                        (no extra distortion when loud)
 *   harm = h band-passed to freq .. 4*freq (LR4 high-pass, LR4 low-pass)
 *   out  = in + amount * harm            (same for both channels)
 * The output goes on
 * through the rest of the chain: the bass ceiling and the guard still
 * bound what reaches the woofers.
 */
#ifndef MBP_VBASS_H
#define MBP_VBASS_H

#include <math.h>
#include <string.h>

struct vb_bq { float b0, b1, b2, a1, a2; };

struct vb_state {
	float fs, freq_cur;
	struct vb_bq lp, hp, hlp;
	float zl[2][2], zh[2][2], zhl[2][2];
	float env;
};

static inline void vb_coef(struct vb_bq *q, float fs, float f, int high)
{
	const float qq = 0.70710678f;
	float w = 2.0f * (float)M_PI * f / fs, c = cosf(w), al = sinf(w) / (2.0f * qq), a0 = 1.0f + al;
	if (high) { q->b0 = q->b2 = (1.0f + c) / 2.0f / a0; q->b1 = -(1.0f + c) / a0; }
	else { q->b0 = q->b2 = (1.0f - c) / 2.0f / a0; q->b1 = (1.0f - c) / a0; }
	q->a1 = -2.0f * c / a0; q->a2 = (1.0f - al) / a0;
}

static inline float vb_run1(const struct vb_bq *q, float *z, float x)
{
	float y = q->b0 * x + z[0];
	z[0] = q->b1 * x - q->a1 * y + z[1];
	z[1] = q->b2 * x - q->a2 * y;
	return y;
}

static inline void vb_init(struct vb_state *s, float fs)
{
	memset(s, 0, sizeof(*s));
	s->fs = fs;
}

static inline void vb_run(struct vb_state *s, float freq, float amount_db,
			  const float *const in[2], float *const out[2], unsigned long n)
{
	float f = freq != freq ? 100.0f : freq < 30.0f ? 30.0f : freq > 300.0f ? 300.0f : freq;
	float amt = amount_db != amount_db ? 0.0f : powf(10.0f, (amount_db > 12.0f ? 12.0f : amount_db) / 20.0f);
	float at = 1.0f - expf(-1.0f / (0.005f * s->fs)), rt = 1.0f - expf(-1.0f / (0.060f * s->fs));
	unsigned long i;

	if (amount_db <= -60.0f) amt = 0.0f;
	if (f != s->freq_cur) {
		vb_coef(&s->lp, s->fs, f, 0);
		vb_coef(&s->hp, s->fs, f, 1);
		vb_coef(&s->hlp, s->fs, 4.0f * f, 0);
		s->freq_cur = f;
	}
	for (i = 0; i < n; i++) {
		float l = in[0][i], r = in[1][i], m, lo, a, h;
		if (l != l || l > 1e3f || l < -1e3f) l = 0.0f;
		if (r != r || r > 1e3f || r < -1e3f) r = 0.0f;
		m = 0.5f * (l + r);
		lo = vb_run1(&s->lp, s->zl[1], vb_run1(&s->lp, s->zl[0], m));
		a = fabsf(lo);
		s->env += (a - s->env) * (a > s->env ? at : rt);
		h = a - 0.63661977f * s->env;		/* DC of a rectified sine is 2/pi */
		h = vb_run1(&s->hp, s->zh[1], vb_run1(&s->hp, s->zh[0], h));
		h = vb_run1(&s->hlp, s->zhl[1], vb_run1(&s->hlp, s->zhl[0], h));
		out[0][i] = l + amt * h;
		out[1][i] = r + amt * h;
	}
	if (s->env < 1e-12f) s->env = 0.0f;
}

#endif
