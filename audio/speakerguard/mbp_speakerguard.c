/*
 * LADSPA plugins for the MacBookPro13,2 speaker filter-chain:
 *
 *   mbp_speakerguard  (guard.h) thermal model + limiter, four channels:
 *                     0,1 = pair A, 2,3 = pair B (the config decides which
 *                     speakers that is). Always the last stage.
 *   mbp_volume        the sink volume with a volume law (a table).
 *   mbp_basslimit     (basslimit.h) bass ceiling for the woofers, with a
 *                     side-chain; in both EQ modes.
 *   mbp_vbass         (vbass.h) psychoacoustic bass (harmonics), EQ layer.
 *
 *
 * Every control falls back to a deliberately over-protective default
 * (low temperature limit, low resistance, quiet ceiling): without a config
 * the speakers are quiet, never unprotected. The real values are set by
 * the filter-chain config.
 */
#include <stdlib.h>
#include "ladspa_min.h"
#include "guard.h"
#include <stdio.h>
#include "basslimit.h"
#include "vbass.h"

enum {
	P_IN0, P_IN1, P_IN2, P_IN3,
	P_OUT0, P_OUT1, P_OUT2, P_OUT3,
	/* per speaker type: A then B */
	P_A_RE, P_A_RTHC, P_A_TAUC, P_A_RTHM, P_A_TAUM, P_A_TLIM,
	P_B_RE, P_B_RTHC, P_B_TAUC, P_B_RTHM, P_B_TAUM, P_B_TLIM,
	P_VFS, P_TAMB, P_HEADROOM, P_CEIL,
	/* monitoring outputs */
	P_T0, P_T1, P_T2, P_T3, P_TGA, P_TGB, P_LGA, P_LGB,
	P_COUNT
};

#define AI (LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO)
#define AO (LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO)
#define CI (LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL)
#define CO (LADSPA_PORT_OUTPUT | LADSPA_PORT_CONTROL)
#define B  (LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE)
#define DMIN LADSPA_HINT_DEFAULT_MINIMUM
#define DMID LADSPA_HINT_DEFAULT_MIDDLE
#define DMAX LADSPA_HINT_DEFAULT_MAXIMUM

static const LADSPA_PortDescriptor pdesc[P_COUNT] = {
	AI, AI, AI, AI, AO, AO, AO, AO,
	CI, CI, CI, CI, CI, CI, CI, CI, CI, CI, CI, CI,
	CI, CI, CI, CI,
	CO, CO, CO, CO, CO, CO, CO, CO,
};

static const char *const pname[P_COUNT] = {
	"In 0", "In 1", "In 2", "In 3", "Out 0", "Out 1", "Out 2", "Out 3",
	"A Re", "A Rth coil", "A Tau coil", "A Rth magnet", "A Tau magnet", "A Tlimit",
	"B Re", "B Rth coil", "B Tau coil", "B Rth magnet", "B Tau magnet", "B Tlimit",
	"Vfs", "Tambient", "Headroom", "Ceiling",
	"Temp 0", "Temp 1", "Temp 2", "Temp 3",
	"Thermal gain A", "Thermal gain B", "Limiter gain A", "Limiter gain B",
};

#define SPK_HINTS \
	{ B | DMIN, 1.0f, 32.0f },	/* Re: low = more power estimated */ \
	{ B | DMAX, 1.0f, 500.0f },	/* Rth coil */ \
	{ B | DMIN, 0.05f, 60.0f },	/* Tau coil: short = heats faster */ \
	{ B | DMAX, 1.0f, 500.0f },	/* Rth magnet */ \
	{ B | DMIN, 1.0f, 3600.0f },	/* Tau magnet */ \
	{ B | DMIN, 60.0f, 150.0f }	/* Tlimit */

static const LADSPA_PortRangeHint phint[P_COUNT] = {
	{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
	{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
	SPK_HINTS, SPK_HINTS,
	{ B | DMAX, 1.0f, 200.0f },	/* Vfs: no config = the highest = most power */
	{ B | DMAX, 20.0f, 60.0f },	/* Tambient */
	{ B | DMID, 5.0f, 50.0f },	/* Headroom */
	{ B | DMIN, -40.0f, 0.0f },	/* Ceiling dBFS */
	{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
	{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
};

/* What the hint defaults above resolve to, used until the host connects
 * a control port. */
static float hint_value(const LADSPA_PortRangeHint *h)
{
	switch (h->HintDescriptor & 0x3C0) {
	case LADSPA_HINT_DEFAULT_0: return 0.0f;
	case DMIN: return h->LowerBound;
	case DMAX: return h->UpperBound;
	case DMID: return 0.5f * (h->LowerBound + h->UpperBound);
	}
	return 0.0f;
}

struct sg {
	LADSPA_Data *port[P_COUNT];
	LADSPA_Data fallback[P_COUNT];
	struct g_cfg cfg;
	struct g_state st;
	float fs;
};

static void load_cfg(struct sg *g)
{
	int i;
	for (i = 0; i < 2; i++) {
		LADSPA_Data *const *p = &g->port[i ? P_B_RE : P_A_RE];
		struct g_spk *s = &g->cfg.spk[i];
		s->re = *p[0]; s->rth_c = *p[1]; s->tau_c = *p[2];
		s->rth_m = *p[3]; s->tau_m = *p[4]; s->t_lim = *p[5];
	}
	g->cfg.vfs = *g->port[P_VFS];
	g->cfg.t_amb = *g->port[P_TAMB];
	g->cfg.headroom = *g->port[P_HEADROOM];
	g->cfg.ceiling_db = *g->port[P_CEIL];
}

static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long rate)
{
	struct sg *g = calloc(1, sizeof(*g));
	int p;
	(void)d;
	if (!g) return NULL;
	g->fs = (float)rate;
	for (p = 0; p < P_COUNT; p++) {
		g->fallback[p] = hint_value(&phint[p]);
		g->port[p] = &g->fallback[p];
	}
	return g;
}

static void connect_port(LADSPA_Handle h, unsigned long p, LADSPA_Data *data)
{
	struct sg *g = h;
	if (p < P_COUNT) g->port[p] = data ? data : &g->fallback[p];
}

static void activate(LADSPA_Handle h)
{
	struct sg *g = h;
	load_cfg(g);
	g_init(&g->st, &g->cfg, g->fs);
}

static void run(LADSPA_Handle h, unsigned long n)
{
	struct sg *g = h;
	const float *in[G_CH] = { g->port[P_IN0], g->port[P_IN1], g->port[P_IN2], g->port[P_IN3] };
	float *out[G_CH] = { g->port[P_OUT0], g->port[P_OUT1], g->port[P_OUT2], g->port[P_OUT3] };
	int ch;

	if (g->st.la == 0) activate(h);	/* host skipped activate() */
	load_cfg(g);
	g_run(&g->st, &g->cfg, in, out, n);
#ifdef SG_NO_MONITOR
	return;
#endif
	for (ch = 0; ch < G_CH; ch++) *g->port[P_T0 + ch] = g->st.temp[ch];
	*g->port[P_TGA] = g->st.tgain_db[0];
	*g->port[P_TGB] = g->st.tgain_db[1];
	*g->port[P_LGA] = g->st.lgain_min_db[0];
	*g->port[P_LGB] = g->st.lgain_min_db[1];
}

static void cleanup(LADSPA_Handle h) { free(h); }

static const LADSPA_Descriptor desc = {
	.UniqueID = 0x4d4253,		/* "MBS", private range unregistered */
	.Label = "mbp_speakerguard",
	.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE,	/* in-place safe: each sample is read before it is written */
	.Name = "MacBookPro13,2 speaker guard (thermal model + limiter)",
	.Maker = "mbp13-2-linux",
	.Copyright = "GPL-2.0-or-later",
	.PortCount = P_COUNT,
	.PortDescriptors = pdesc,
	.PortNames = pname,
	.PortRangeHints = phint,
	.instantiate = instantiate,
	.connect_port = connect_port,
	.activate = activate,
	.run = run,
	.cleanup = cleanup,
};

#define D0 LADSPA_HINT_DEFAULT_0

/* ---- mbp_volume (the sink volume with a volume law) ------------------- */

/* PipeWire hands the linear channel volume v (desktops show cbrt(v) as the
 * percent): "Volume L/R" via capture.volumes, scale linear. The gain in dB
 * is the law table at p = cbrt(v): 21 points for 0, 5, ... 100 %, linear in
 * between, every point clamped to <= 0 dB; v = 0 is mute. The gain glides
 * with a one-pole (VOL_SMOOTH_S) so a slider move does not click.
 * Without a config: -60 dB at full volume, quieter below. */
#define VOL_LAW 21
#define VOL_SMOOTH_S 0.02f

enum { O_INL, O_INR, O_OUTL, O_OUTR, O_VOLL, O_VOLR, O_LAW, O_VDB = O_LAW + VOL_LAW, O_COUNT };

static LADSPA_PortDescriptor odesc[O_COUNT];
static const char *oname[O_COUNT];
static LADSPA_PortRangeHint ohint[O_COUNT];
static char oname_buf[VOL_LAW][8];

struct vlp {
	LADSPA_Data *port[O_COUNT];
	LADSPA_Data fallback[O_COUNT];
	float fs, a, g[2];
	int active;
};

static float vol_law_db(struct vlp *g, float v)
{
	float p, x, lo, hi;
	int i;
	if (!(v > 0.0f)) return -1000.0f;
	p = cbrtf(v > 1.0f ? 1.0f : v);
	x = p * (VOL_LAW - 1);
	i = (int)x;
	if (i >= VOL_LAW - 1) { i = VOL_LAW - 2; x = (float)(VOL_LAW - 1); }
	lo = *g->port[O_LAW + i]; hi = *g->port[O_LAW + i + 1];
	if (lo > 0.0f) lo = 0.0f;
	if (hi > 0.0f) hi = 0.0f;
	return lo + (hi - lo) * (x - (float)i);
}

static LADSPA_Handle o_instantiate(const LADSPA_Descriptor *d, unsigned long rate)
{
	struct vlp *g = calloc(1, sizeof(*g));
	int p;
	(void)d;
	if (!g) return NULL;
	g->fs = (float)rate;
	for (p = 0; p < O_COUNT; p++) {
		g->fallback[p] = hint_value(&ohint[p]);
		g->port[p] = &g->fallback[p];
	}
	for (p = 0; p < VOL_LAW; p++)
		g->fallback[O_LAW + p] = -60.0f - 60.0f * (1.0f - p / (float)(VOL_LAW - 1));
	return g;
}

static void o_connect_port(LADSPA_Handle h, unsigned long p, LADSPA_Data *data)
{
	struct vlp *g = h;
	if (p < O_COUNT) g->port[p] = data ? data : &g->fallback[p];
}

static void o_activate(LADSPA_Handle h)
{
	struct vlp *g = h;
	g->a = expf(-1.0f / (VOL_SMOOTH_S * g->fs));
	g->g[0] = g->g[1] = 0.0f;	/* fade in from silence */
	g->active = 1;
}

static void o_run(LADSPA_Handle h, unsigned long n)
{
	struct vlp *g = h;
	int c;
	unsigned long i;
	float db = -1000.0f;
	if (!g->active) o_activate(h);
	for (c = 0; c < 2; c++) {
		const float *in = g->port[c ? O_INR : O_INL];
		float *out = g->port[c ? O_OUTR : O_OUTL];
		float v = *g->port[c ? O_VOLR : O_VOLL];
		float d = vol_law_db(g, v != v ? 0.0f : v);
		float t = d <= -999.0f ? 0.0f : powf(10.0f, d / 20.0f);
		float gc = g->g[c];
		if (d > db) db = d;
		for (i = 0; i < n; i++) {
			gc = t + (gc - t) * g->a;
			out[i] = in[i] * gc;
		}
		g->g[c] = gc;
	}
	*g->port[O_VDB] = db;
}

static const LADSPA_Descriptor odescr = {
	.UniqueID = 0x4d424f,		/* "MBO", private */
	.Label = "mbp_volume",
	.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE,	/* in-place safe */
	.Name = "MacBookPro13,2 sink volume with a volume law",
	.Maker = "mbp13-2-linux",
	.Copyright = "GPL-2.0-or-later",
	.PortCount = O_COUNT,
	.PortDescriptors = odesc,
	.PortNames = oname,
	.PortRangeHints = ohint,
	.instantiate = o_instantiate,
	.connect_port = o_connect_port,
	.activate = o_activate,
	.run = o_run,
	.cleanup = cleanup,
};

__attribute__((constructor)) static void tables_init(void)
{
	static const char *const io[] = { "In L", "In R", "Out L", "Out R", "Volume L", "Volume R" };
	int i;
	for (i = 0; i < O_COUNT; i++) {
		odesc[i] = i < 2 ? AI : i < 4 ? AO : i == O_VDB ? CO : CI;
		ohint[i] = (LADSPA_PortRangeHint){ 0, 0, 0 };
		if (i < O_LAW) oname[i] = io[i];
	}
	ohint[O_VOLL] = ohint[O_VOLR] = (LADSPA_PortRangeHint){ B | DMAX, 0.0f, 1.0f };
	for (i = 0; i < VOL_LAW; i++) {
		snprintf(oname_buf[i], sizeof(oname_buf[i]), "Law %d", i);
		oname[O_LAW + i] = oname_buf[i];
		ohint[O_LAW + i] = (LADSPA_PortRangeHint){ B | DMIN, -120.0f, 0.0f };
	}
	oname[O_VDB] = "Volume dB";
}


/* ---- mbp_basslimit --------------------------------------------------- */

enum { L_INL, L_INR, L_SCL, L_SCR, L_OUTL, L_OUTR, L_CEIL, L_SPLIT, L_LA, L_REL,
       L_HP1F, L_HP1Q, L_HP2F, L_HP2Q, L_FLOOR, L_DET, L_GMIN, L_COUNT };

static const LADSPA_PortDescriptor ldesc[L_COUNT] = { AI, AI, AI, AI, AO, AO, CI, CI, CI, CI,
						      CI, CI, CI, CI, CI, CI, CO };
static const char *const lname[L_COUNT] = {
	"In L", "In R", "Side L", "Side R", "Out L", "Out R",
	"Ceiling", "Split", "Lookahead", "Release",
	"HP1 freq", "HP1 Q", "HP2 freq", "HP2 Q", "Inverse floor", "Detect", "Gain min",
};
/* Without a config the ceiling is -40 dB: hardly any bass, never too much. */
static const LADSPA_PortRangeHint lhint[L_COUNT] = {
	{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
	{ B | DMIN, -40.0f, 16.0f },	/* ceiling dB re static EQ; capped per label below */
	{ B | D0, 0.0f, 20000.0f },	/* split Hz, 0 = whole signal (limits most) */
	{ B | DMID, 1.0f, 20.0f },	/* look-ahead ms */
	{ B | DMAX, 5.0f, 5000.0f },	/* release ms: slow = safer */
	{ B | D0, 0.0f, 10000.0f },	/* HP1 freq: 0 = none */
	{ B | DMID, 0.3f, 5.0f },	/* HP1 Q */
	{ B | D0, 0.0f, 10000.0f },	/* HP2 freq */
	{ B | DMID, 0.3f, 5.0f },	/* HP2 Q */
	{ B | DMIN, 5.0f, 2000.0f },	/* inverse floor Hz */
	{ B | D0, 0.0f, 20000.0f },	/* detect low-pass Hz, 0 = none (sees all) */
	{ 0, 0, 0 },
};

struct blp {
	LADSPA_Data *port[L_COUNT];
	LADSPA_Data fallback[L_COUNT];
	struct bl_state st;
	float fs, max_raise;
	int active;
};

static LADSPA_Handle l_instantiate(const LADSPA_Descriptor *d, unsigned long rate)
{
	struct blp *g = calloc(1, sizeof(*g));
	int p;
	if (!g) return NULL;
	g->fs = (float)rate;
	g->max_raise = d->ImplementationData ? BL_MAX_RAISE_TREBLE : BL_MAX_RAISE;
	for (p = 0; p < L_COUNT; p++) {
		g->fallback[p] = hint_value(&lhint[p]);
		g->port[p] = &g->fallback[p];
	}
	return g;
}

static void l_connect_port(LADSPA_Handle h, unsigned long p, LADSPA_Data *data)
{
	struct blp *g = h;
	if (p < L_COUNT) g->port[p] = data ? data : &g->fallback[p];
}

static void l_activate(LADSPA_Handle h)
{
	struct blp *g = h;
	bl_init(&g->st, g->fs, *g->port[L_LA]);
	g->active = 1;
}

static void l_run(LADSPA_Handle h, unsigned long n)
{
	struct blp *g = h;
	const float *in[2] = { g->port[L_INL], g->port[L_INR] };
	const float *sc[2] = { g->port[L_SCL], g->port[L_SCR] };
	float *out[2] = { g->port[L_OUTL], g->port[L_OUTR] };
	struct bl_cfg c;

	if (!g->active) l_activate(h);
	c.ceiling = *g->port[L_CEIL];
	c.max_raise = g->max_raise;
	c.split = *g->port[L_SPLIT];
	c.lookahead = *g->port[L_LA];
	c.release = *g->port[L_REL];
	c.hp_f[0] = *g->port[L_HP1F]; c.hp_q[0] = *g->port[L_HP1Q];
	c.hp_f[1] = *g->port[L_HP2F]; c.hp_q[1] = *g->port[L_HP2Q];
	c.inv_floor = *g->port[L_FLOOR];
	c.detect = *g->port[L_DET];
	bl_run(&g->st, &c, in, sc, out, n);
	*g->port[L_GMIN] = 20.0f * log10f(g->st.gmin + 1e-9f);
}

static const LADSPA_Descriptor ldescr = {
	.UniqueID = 0x4d424c,		/* "MBL", private */
	.Label = "mbp_basslimit",
	.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE,	/* in-place safe: each sample is read before it is written */
	.Name = "MacBookPro13,2 woofer bass ceiling (side-chain limiter)",
	.Maker = "mbp13-2-linux",
	.Copyright = "GPL-2.0-or-later",
	.PortCount = L_COUNT,
	.PortDescriptors = ldesc,
	.PortNames = lname,
	.PortRangeHints = lhint,
	.instantiate = l_instantiate,
	.connect_port = l_connect_port,
	.activate = l_activate,
	.run = l_run,
	.cleanup = cleanup,
};

/* ---- mbp_vbass ------------------------------------------------------ */

enum { V_INL, V_INR, V_OUTL, V_OUTR, V_FREQ, V_AMT, V_COUNT };
static const LADSPA_PortDescriptor vdesc[V_COUNT] = { AI, AI, AO, AO, CI, CI };
static const char *const vname[V_COUNT] = { "In L", "In R", "Out L", "Out R", "Freq", "Amount" };
/* Without a config: off (amount -60 dB). */
static const LADSPA_PortRangeHint vhint[V_COUNT] = {
	{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
	{ B | DMID, 30.0f, 300.0f },	/* Hz */
	{ B | DMIN, -60.0f, 12.0f },	/* dB, -60 = off */
};

struct vbp {
	LADSPA_Data *port[V_COUNT];
	LADSPA_Data fallback[V_COUNT];
	struct vb_state st;
	float fs;
	int active;
};

static LADSPA_Handle v_instantiate(const LADSPA_Descriptor *d, unsigned long rate)
{
	struct vbp *g = calloc(1, sizeof(*g));
	int p;
	(void)d;
	if (!g) return NULL;
	g->fs = (float)rate;
	for (p = 0; p < V_COUNT; p++) {
		g->fallback[p] = hint_value(&vhint[p]);
		g->port[p] = &g->fallback[p];
	}
	return g;
}

static void v_connect_port(LADSPA_Handle h, unsigned long p, LADSPA_Data *data)
{
	struct vbp *g = h;
	if (p < V_COUNT) g->port[p] = data ? data : &g->fallback[p];
}

static void v_activate(LADSPA_Handle h)
{
	struct vbp *g = h;
	vb_init(&g->st, g->fs);
	g->active = 1;
}

static void v_run(LADSPA_Handle h, unsigned long n)
{
	struct vbp *g = h;
	const float *in[2] = { g->port[V_INL], g->port[V_INR] };
	float *out[2] = { g->port[V_OUTL], g->port[V_OUTR] };
	if (!g->active) v_activate(h);
	vb_run(&g->st, *g->port[V_FREQ], *g->port[V_AMT], in, out, n);
}

static const LADSPA_Descriptor vdescr = {
	.UniqueID = 0x4d4256,		/* "MBV", private */
	.Label = "mbp_vbass",
	.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE,	/* in-place safe: each sample is read before it is written */
	.Name = "MacBookPro13,2 psychoacoustic bass (harmonics)",
	.Maker = "mbp13-2-linux",
	.Copyright = "GPL-2.0-or-later",
	.PortCount = V_COUNT,
	.PortDescriptors = vdesc,
	.PortNames = vname,
	.PortRangeHints = vhint,
	.instantiate = v_instantiate,
	.connect_port = v_connect_port,
	.activate = v_activate,
	.run = v_run,
	.cleanup = cleanup,
};

/* Same plugin for the tweeters, with its own hard cap (ImplementationData). */
static int treble_tag;
static const LADSPA_Descriptor tdescr = {
	.UniqueID = 0x4d4254,		/* "MBT", private */
	.Label = "mbp_treblelimit",
	.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE,
	.Name = "MacBookPro13,2 tweeter ceiling (side-chain limiter)",
	.Maker = "mbp13-2-linux",
	.Copyright = "GPL-2.0-or-later",
	.PortCount = L_COUNT,
	.PortDescriptors = ldesc,
	.PortNames = lname,
	.PortRangeHints = lhint,
	.ImplementationData = &treble_tag,
	.instantiate = l_instantiate,
	.connect_port = l_connect_port,
	.activate = l_activate,
	.run = l_run,
	.cleanup = cleanup,
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long i)
{
	return i == 0 ? &desc : i == 1 ? &odescr : i == 2 ? &ldescr : i == 3 ? &vdescr :
	       i == 4 ? &tdescr : NULL;
}
