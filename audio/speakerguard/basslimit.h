/*
 * Bass ceiling for the woofers: a look-ahead limiter on the low part of the
 * woofer signal, driven by a side-chain.
 *
 * The graph feeds the side-chain with the woofer signal run through the
 * inverse of the static macOS peaking EQ (bass part); the inverse of the
 * high-pass in front of it (two 2nd order sections) is done here, as a
 * stable approximation (poles at 'inverse floor' Hz instead of 0 Hz; the
 * floor sets the gain at DC, so it must not be too low: with 5 Hz under a
 * 63/80 Hz high-pass that was +92 dB and float noise read as bass. Run in
 * double precision.) A 0 dBFS sine through
 * the plain static EQ gives a 0 dBFS side-chain at every frequency, so a
 * 0 dB ceiling here means: whatever comes before (compressor, EQ off), the
 * woofers never get more bass than the static macOS EQ gives a full-scale
 * sine. Works the same with the EQ on and off.
 *
 * Only the part below 'split' (LR8: low and high part are in phase, so the
 * high part must be far down where the bass is cut: -64 dB at 0.4 * split;
 * with LR4 it was -32 dB and added up to 4 dB back) is limited, the rest passes, so the
 * woofer mids do not pump with the bass. The side-chain is low-passed
 * (LR4) at 'detect': the ceiling only looks at the bass. The ceiling can
 * be raised above the static EQ, at most BL_MAX_RAISE dB.
 *
 * split <= 0: no split, the gain acts on the whole signal; detect <= 0: no
 * side-chain low-pass (used for the tweeters, ceiling on all frequencies). Audio is delayed by the look-ahead (both parts), the gain is
 * the min-filtered and boxcar-averaged required gain (no overshoot on the
 * side-chain peaks), release is linear in dB-free gain units.
 */
#ifndef MBP_BASSLIMIT_H
#define MBP_BASSLIMIT_H

#include <math.h>
#include <string.h>

#define BL_LA_MAX 1024
#define BL_MAX_RAISE 15.0f	/* bass: dB above the static EQ, hard limit (user's rule;
				   +9 until 2026-10-18, +15 as an emergency
				   ceiling behind macOS's BuzzKill) */
#define BL_MAX_RAISE_TREBLE 16.0f /* tweeters: the macOS chain itself goes ~+15.4 */

struct bl_cfg {
	float ceiling;		/* dBFS on the side-chain */
	float max_raise;	/* hard cap of the ceiling, set by the plugin label */
	float hp_f[2], hp_q[2];	/* high-pass sections to invert on the side-chain (f 0 = none) */
	float inv_floor;	/* Hz */
	float split;		/* Hz, gain applies below */
	float detect;		/* Hz, side-chain low-pass */
	float lookahead;	/* ms */
	float release;		/* ms, full scale */
};

struct bl_bq { float b0, b1, b2, a1, a2; };
struct bl_bqd { double b0, b1, b2, a1, a2; };

struct bl_state {
	float fs, split_cur, detect_cur;
	int la;
	long n;
	struct bl_bq lp[2], hp[2], slp;		/* LR8 = two Butterworth 4th order */
	struct bl_bqd ihp[2];
	float ihp_key[2][3];
	float za[2][4][2], zb[2][4][2], zs[2][2][2];	/* audio lp, audio hp (4 stages), side lp */
	double zi[2][2][2];				/* side inverse high-pass */
	float dlo[2][BL_LA_MAX], dhi[2][BL_LA_MAX];
	float req[BL_LA_MAX], box[BL_LA_MAX];
	long mq[BL_LA_MAX];
	int mh, mn;
	double bsum;
	float g;
	float gmin;		/* monitoring: lowest gain of the last run */
};

static inline float bl_clampf(float v, float lo, float hi)
{
	return v != v ? lo : v < lo ? lo : v > hi ? hi : v;
}

static inline void bl_lp(struct bl_bq *q, float fs, float f)
{
	const float qq = 0.70710678f;
	float w = 2.0f * (float)M_PI * f / fs, c = cosf(w), al = sinf(w) / (2.0f * qq), a0 = 1.0f + al;
	q->b0 = q->b2 = (1.0f - c) / 2.0f / a0; q->b1 = (1.0f - c) / a0;
	q->a1 = -2.0f * c / a0; q->a2 = (1.0f - al) / a0;
}

/* Butterworth 4th order = sections with Q 0.5412 and 1.3066; LR8 runs each twice. */
static inline void bl_design(struct bl_state *s, float f)
{
	static const float qs[2] = { 0.54119610f, 1.30656296f };
	int k;
	for (k = 0; k < 2; k++) {
		float w = 2.0f * (float)M_PI * f / s->fs, c = cosf(w), al = sinf(w) / (2.0f * qs[k]), a0 = 1.0f + al;
		s->lp[k].b0 = s->lp[k].b2 = (1.0f - c) / 2.0f / a0; s->lp[k].b1 = (1.0f - c) / a0;
		s->hp[k].b0 = s->hp[k].b2 = (1.0f + c) / 2.0f / a0; s->hp[k].b1 = -(1.0f + c) / a0;
		s->lp[k].a1 = s->hp[k].a1 = -2.0f * c / a0;
		s->lp[k].a2 = s->hp[k].a2 = (1.0f - al) / a0;
	}
	s->split_cur = f;
}

/* 1 / HP(s) for HP(s) = s^2 / (s^2 + w/q s + w^2), with the double pole of
 * the inverse moved from 0 to wf: (s^2 + w/q s + w^2) / (s + wf)^2. Bilinear,
 * prewarped. f <= 0: pass-through. */
static inline void bl_inv_hp(struct bl_bqd *q, float fs, float f, float qq, float floor_hz)
{
	double K = 2.0 * fs, w, wf, a0;
	if (f <= 0.0f) { q->b0 = 1; q->b1 = q->b2 = q->a1 = q->a2 = 0; return; }
	w = K * tan(M_PI * f / fs);
	wf = K * tan(M_PI * floor_hz / fs);
	a0 = K * K + 2 * wf * K + wf * wf;
	q->b0 = (K * K + w / qq * K + w * w) / a0;
	q->b1 = (-2 * K * K + 2 * w * w) / a0;
	q->b2 = (K * K - w / qq * K + w * w) / a0;
	q->a1 = (-2 * K * K + 2 * wf * wf) / a0;
	q->a2 = (K * K - 2 * wf * K + wf * wf) / a0;
}

static inline double bl_bqd_run(const struct bl_bqd *q, double *z, double x)
{
	double y = q->b0 * x + z[0];
	z[0] = q->b1 * x - q->a1 * y + z[1];
	z[1] = q->b2 * x - q->a2 * y;
	return y;
}

static inline float bl_bq_run(const struct bl_bq *q, float *z, float x)
{
	float y = q->b0 * x + z[0];
	z[0] = q->b1 * x - q->a1 * y + z[1];
	z[1] = q->b2 * x - q->a2 * y;
	return y;
}

static inline void bl_init(struct bl_state *s, float fs, float lookahead_ms)
{
	int i;
	memset(s, 0, sizeof(*s));
	s->fs = fs;
	s->la = (int)(fs * bl_clampf(lookahead_ms, 1.0f, 20.0f) / 1000.0f);
	if (s->la < 2) s->la = 2;
	if (s->la > BL_LA_MAX) s->la = BL_LA_MAX;
	for (i = 0; i < BL_LA_MAX; i++) s->req[i] = s->box[i] = 1.0f;
	s->ihp_key[0][0] = s->ihp_key[1][0] = -1.0f;
	s->bsum = s->la;
	s->g = 1.0f;
}

static inline void bl_run(struct bl_state *s, const struct bl_cfg *cfg,
			  const float *const in[2], const float *const side[2],
			  float *const out[2], unsigned long n)
{
	const int W = s->la, D = s->la - 1;
	float ceil = powf(10.0f, bl_clampf(cfg->ceiling, -60.0f, cfg->max_raise) / 20.0f);
	const int nodet = !(cfg->detect > 0.0f);
	float detect = nodet ? 1000.0f : bl_clampf(cfg->detect, 100.0f, s->fs * 0.4f);
	float rel = 1.0f / (bl_clampf(cfg->release, 5.0f, 5000.0f) / 1000.0f * s->fs);
	const int full = !(cfg->split > 0.0f);			/* NaN too: full band */
	float split = full ? 1000.0f : bl_clampf(cfg->split, 100.0f, s->fs * 0.4f);
	unsigned long i;
	int ch;

	if (split != s->split_cur) bl_design(s, split);
	if (detect != s->detect_cur) { bl_lp(&s->slp, s->fs, detect); s->detect_cur = detect; }
	for (ch = 0; ch < 2; ch++) {
		float f = bl_clampf(cfg->hp_f[ch], 0.0f, 10000.0f), q = bl_clampf(cfg->hp_q[ch], 0.3f, 5.0f);
		float fl = bl_clampf(cfg->inv_floor, 5.0f, 2000.0f);
		if (f != s->ihp_key[ch][0] || q != s->ihp_key[ch][1] || fl != s->ihp_key[ch][2]) {
			bl_inv_hp(&s->ihp[ch], s->fs, f, q, fl);
			s->ihp_key[ch][0] = f; s->ihp_key[ch][1] = q; s->ihp_key[ch][2] = fl;
		}
	}
	s->gmin = 1.0f;
	for (i = 0; i < n; i++) {
		const long t = s->n++;
		const int wp = (int)(t % BL_LA_MAX), rp = (int)((t + BL_LA_MAX - D) % BL_LA_MAX);
		const int bp = (int)(t % W);
		float r = 1.0f, m, a, g;

		for (ch = 0; ch < 2; ch++) {
			float x = in[ch][i], sc = side[ch][i], pk;
			if (x != x || x > 1e3f || x < -1e3f) x = 0.0f;
			if (sc != sc || sc > 1e3f || sc < -1e3f) sc = 1e3f;	/* garbage: clamp hard */
			if (full) {
				s->dlo[ch][wp] = x;
				s->dhi[ch][wp] = 0.0f;
			} else {
				float l = x, h = x;
				int st;
				for (st = 0; st < 4; st++) {
					l = bl_bq_run(&s->lp[st & 1], s->za[ch][st], l);
					h = bl_bq_run(&s->hp[st & 1], s->zb[ch][st], h);
				}
				s->dlo[ch][wp] = l;
				s->dhi[ch][wp] = h;
			}
			/* both inverse high-pass sections, then the split low-pass */
			sc = (float)bl_bqd_run(&s->ihp[1], s->zi[ch][1], bl_bqd_run(&s->ihp[0], s->zi[ch][0], sc));
			pk = nodet ? fabsf(sc) : fabsf(bl_bq_run(&s->slp, s->zs[ch][1], bl_bq_run(&s->slp, s->zs[ch][0], sc)));
			if (pk * r > ceil) r = ceil / pk;
		}
		/* sliding minimum of r over W samples, then boxcar average */
		s->req[wp] = r;
		while (s->mn && s->mq[s->mh] <= t - W) { s->mh = (s->mh + 1) % BL_LA_MAX; s->mn--; }
		while (s->mn && s->req[s->mq[(s->mh + s->mn - 1) % BL_LA_MAX] % BL_LA_MAX] >= r) s->mn--;
		s->mq[(s->mh + s->mn) % BL_LA_MAX] = t;
		s->mn++;
		m = s->req[s->mq[s->mh] % BL_LA_MAX];
		s->bsum += (double)m - s->box[bp];
		s->box[bp] = m;
		a = (float)(s->bsum / W);
		g = s->g + rel;
		s->g = g < a ? g : a;
		if (s->g < s->gmin) s->gmin = s->g;
		for (ch = 0; ch < 2; ch++) out[ch][i] = s->dlo[ch][rp] * s->g + s->dhi[ch][rp];
	}
	{	/* re-sum the boxcar: no drift */
		int j; double sum = 0;
		for (j = 0; j < W; j++) sum += s->box[j];
		s->bsum = sum;
	}
}

#endif
