/*
 * Speaker guard DSP core: a feed-forward thermal model and a look-ahead
 * peak limiter for the four internal speakers of the MacBookPro13,2.
 *
 * The SSM3515 amplifiers report no speaker current or voltage (no IV sense),
 * so, like macOS (DspThermalSpeakerProtection4ch), the voice coil temperature
 * is estimated from the signal itself:
 *
 *   P      = (y * vfs)^2 / re           y: output sample, vfs: peak volts at 0 dBFS
 *   dTm'   = (P * rth_m - dTm) / tau_m   magnet (slow)
 *   dTc'   = (P * rth_c - dTc) / tau_c   coil over the magnet (fast)
 *   T_coil = t_amb + dTm + dTc
 *
 * Using the DC resistance re overestimates the power wherever the impedance
 * is higher, so the estimate errs on the hot side. Above (t_lim - headroom -
 * KNEE) the gain is reduced by SLOPE dB per degree; at t_lim it drops to
 * MAX_ATTEN. Both channels of a pair (left/right of one speaker type) always
 * get the same gain so the stereo image does not shift.
 *
 * The limiter keeps every output sample at or below the ceiling: the
 * required gain is min-filtered over the look-ahead window and then
 * averaged over the same window, with the audio delayed by LA - 1 samples,
 * so the gain is already down when the peak arrives (no overshoot).
 *
 * Plain C, no allocation after init, safe for the real-time thread.
 */
#ifndef MBP_GUARD_H
#define MBP_GUARD_H

#include <math.h>
#include <string.h>

#define G_CH		4	/* 0,1: pair A; 2,3: pair B */
#define G_LA_MAX	512	/* look-ahead samples, max (2 ms at 192 kHz) */
#define G_KNEE		10.0f	/* degC below the control point where reduction starts */
#define G_SLOPE		1.5f	/* dB per degC above the knee */
#define G_MAX_ATTEN	40.0f	/* dB */

struct g_spk {			/* one speaker type */
	float re;		/* ohm, voice coil DC resistance */
	float rth_c, tau_c;	/* K/W, s: coil */
	float rth_m, tau_m;	/* K/W, s: magnet */
	float t_lim;		/* degC, never to be reached */
};

struct g_cfg {
	struct g_spk spk[2];	/* [0] pair A (ch 0,1), [1] pair B (ch 2,3) */
	float vfs;		/* V peak at 0 dBFS */
	float t_amb;		/* degC, assumed inside the chassis */
	float headroom;		/* degC kept below t_lim */
	float ceiling_db;	/* limiter ceiling, dBFS */
};

struct g_state {
	float fs;
	int la;				/* look-ahead window, samples */
	long n;				/* samples processed */
	/* thermal */
	double dtc[G_CH], dtm[G_CH];
	float tgain[2];			/* smoothed thermal gain per pair (linear) */
	/* limiter, per pair; the audio is delayed by la - 1 */
	float z[G_CH][G_LA_MAX];	/* thermally scaled input, delay line */
	float req[2][G_LA_MAX];		/* required gain per sample */
	long mq[2][G_LA_MAX];		/* sliding-min deque: sample numbers */
	int mh[2], mn[2];
	float box[2][G_LA_MAX];		/* sliding-min output, boxcar ring */
	double bsum[2];
	float lgain[2];
	/* monitoring, updated every run */
	float temp[G_CH];		/* degC */
	float tgain_db[2], lgain_min_db[2];
};

static inline float g_db2lin(float db) { return powf(10.0f, db / 20.0f); }

static inline float g_clampf(float v, float lo, float hi)
{
	return v != v ? hi : v < lo ? lo : v > hi ? hi : v;	/* NaN -> hi */
}

/* Keep a config within sane, protective bounds whatever the host passed. */
static inline void g_sanitize(struct g_cfg *c)
{
	int i;
	for (i = 0; i < 2; i++) {
		struct g_spk *s = &c->spk[i];
		s->re    = g_clampf(s->re, 1.0f, 32.0f);
		s->rth_c = g_clampf(s->rth_c, 1.0f, 500.0f);
		s->tau_c = g_clampf(s->tau_c, 0.05f, 60.0f);
		s->rth_m = g_clampf(s->rth_m, 1.0f, 500.0f);
		s->tau_m = g_clampf(s->tau_m, 1.0f, 3600.0f);
		s->t_lim = g_clampf(s->t_lim, 60.0f, 150.0f);
	}
	c->vfs        = g_clampf(c->vfs, 1.0f, 200.0f);	/* follows the amps DAC volume (mbp-audio dac) */
	c->t_amb      = g_clampf(c->t_amb, 20.0f, 60.0f);
	c->headroom   = g_clampf(c->headroom, 5.0f, 50.0f);
	c->ceiling_db = g_clampf(c->ceiling_db, -40.0f, 0.0f);
}

/* Start warm: the magnet at half its allowed rise, since a restart of the
 * audio server says nothing about how hot the speakers still are. */
static inline void g_init(struct g_state *s, const struct g_cfg *cfg, float fs)
{
	struct g_cfg cc = *cfg, *c = &cc;
	int ch;
	g_sanitize(c);
	memset(s, 0, sizeof(*s));
	s->fs = fs;
	s->la = (int)(fs * 0.002f);
	if (s->la < 2) s->la = 2;
	if (s->la > G_LA_MAX) s->la = G_LA_MAX;
	for (ch = 0; ch < G_CH; ch++) {
		const struct g_spk *p = &c->spk[ch / 2];
		float rise = p->t_lim - c->headroom - G_KNEE - c->t_amb;
		s->dtm[ch] = rise > 0 ? 0.5 * rise * p->rth_m / (p->rth_c + p->rth_m) : 0;
		s->temp[ch] = c->t_amb + (float)s->dtm[ch];
	}
	s->tgain[0] = s->tgain[1] = 1.0f;
	s->lgain[0] = s->lgain[1] = 1.0f;
	for (ch = 0; ch < 2; ch++) {
		int i;
		for (i = 0; i < G_LA_MAX; i++) s->req[ch][i] = s->box[ch][i] = 1.0f;
		s->bsum[ch] = s->la;
	}
}

static inline float g_thermal_target(const struct g_state *s, const struct g_cfg *c, int pair)
{
	const struct g_spk *p = &c->spk[pair];
	float t = s->temp[2 * pair] > s->temp[2 * pair + 1] ? s->temp[2 * pair] : s->temp[2 * pair + 1];
	float knee = p->t_lim - c->headroom - G_KNEE;
	float att;
	if (t >= p->t_lim) return g_db2lin(-G_MAX_ATTEN);
	if (t <= knee) return 1.0f;
	att = (t - knee) * G_SLOPE;
	return g_db2lin(-(att < G_MAX_ATTEN ? att : G_MAX_ATTEN));
}

/* in[ch][i] -> out[ch][i]; in and out may alias. */
static inline void g_run(struct g_state *s, const struct g_cfg *cfg_in,
			 const float *const in[G_CH], float *const out[G_CH], unsigned long n)
{
	struct g_cfg c = *cfg_in;
	float ceil, ka[2], kc[2], km[2], tg[2], lmin[2], tsm, rel;
	const int W = s->la, D = s->la - 1;
	unsigned long i;
	int ch, pr;

	g_sanitize(&c);
	ceil = g_db2lin(c.ceiling_db);
	for (pr = 0; pr < 2; pr++) {
		ka[pr] = c.vfs * c.vfs / c.spk[pr].re;		/* W at full scale DC */
		kc[pr] = 1.0f / (c.spk[pr].tau_c * s->fs);
		km[pr] = 1.0f / (c.spk[pr].tau_m * s->fs);
		/* The temperature moves over seconds: the target gain is set once
		 * per block (at most ~43 ms) and approached smoothly per sample. */
		tg[pr] = g_thermal_target(s, &c, pr);
		lmin[pr] = 1.0f;
	}
	tsm = 1.0f / (0.010f * s->fs);		/* thermal gain smoothing, 10 ms */
	rel = 1.0f / (0.100f * s->fs);		/* limiter release: full scale in 100 ms */

	for (i = 0; i < n; i++) {
		const long t = s->n++;
		const int wp = (int)(t % G_LA_MAX);		/* write slot */
		const int rp = (int)((t + G_LA_MAX - D) % G_LA_MAX);	/* delayed slot */
		const int bp = (int)(t % W);
		float y[G_CH];

		for (pr = 0; pr < 2; pr++) {
			float r = 1.0f, m, a, g;
			int k;

			s->tgain[pr] += (tg[pr] - s->tgain[pr]) * tsm;
			for (k = 2 * pr; k < 2 * pr + 2; k++) {
				float v = in[k][i], pk;
				if (v != v || v > 1e3f || v < -1e3f) v = 0.0f;	/* NaN/garbage */
				v *= s->tgain[pr];
				s->z[k][wp] = v;
				pk = fabsf(v);
				if (pk * r > ceil) r = ceil / pk;
			}
			/* sliding minimum of r over the last W samples */
			s->req[pr][wp] = r;
			while (s->mn[pr] && s->mq[pr][s->mh[pr]] <= t - W) {
				s->mh[pr] = (s->mh[pr] + 1) % G_LA_MAX; s->mn[pr]--;
			}
			while (s->mn[pr]) {
				int back = (s->mh[pr] + s->mn[pr] - 1) % G_LA_MAX;
				if (s->req[pr][s->mq[pr][back] % G_LA_MAX] < r) break;
				s->mn[pr]--;
			}
			s->mq[pr][(s->mh[pr] + s->mn[pr]) % G_LA_MAX] = t;
			s->mn[pr]++;
			m = s->req[pr][s->mq[pr][s->mh[pr]] % G_LA_MAX];
			/* boxcar average of the minimum over the same W samples */
			s->bsum[pr] += (double)m - s->box[pr][bp];
			s->box[pr][bp] = m;
			a = (float)(s->bsum[pr] / W);
			g = s->lgain[pr] + rel;
			s->lgain[pr] = g < a ? g : a;
			if (s->lgain[pr] < lmin[pr]) lmin[pr] = s->lgain[pr];
		}

		for (ch = 0; ch < G_CH; ch++) {
			pr = ch / 2;
			y[ch] = s->z[ch][rp] * s->lgain[pr];
			/* thermal model on what really goes to the speaker */
			{
				double p = (double)y[ch] * y[ch] * ka[pr];
				s->dtm[ch] += (p * c.spk[pr].rth_m - s->dtm[ch]) * km[pr];
				s->dtc[ch] += (p * c.spk[pr].rth_c - s->dtc[ch]) * kc[pr];
				if (s->dtm[ch] < 1e-12) s->dtm[ch] = 0;	/* no denormals */
				if (s->dtc[ch] < 1e-12) s->dtc[ch] = 0;
				s->temp[ch] = c.t_amb + (float)(s->dtm[ch] + s->dtc[ch]);
			}
		}
		for (ch = 0; ch < G_CH; ch++) out[ch][i] = y[ch];
	}
	for (pr = 0; pr < 2; pr++) {
		int j; double sum = 0;
		for (j = 0; j < W; j++) sum += s->box[pr][j];	/* re-sum: no drift */
		s->bsum[pr] = sum;
		s->tgain_db[pr] = 20.0f * log10f(s->tgain[pr] + 1e-9f);
		s->lgain_min_db[pr] = 20.0f * log10f(lmin[pr] + 1e-9f);
	}
}

#endif
