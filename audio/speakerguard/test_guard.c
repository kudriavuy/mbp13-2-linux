/* Offline test of the guard core: worst-case signals, checks the invariants.
 * Build: cc -O2 -o test_guard test_guard.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include "guard.h"

/* Generic test parameters of a small speaker; the real
 * ones come from the local config and are not part of the source. */
static struct g_cfg cfg = {
	.spk = { { 6.0f, 60.0f, 1.5f, 60.0f, 40.0f, 120.0f },
		 { 4.0f, 30.0f, 2.5f, 30.0f, 100.0f, 130.0f } },
	.vfs = 10.0f, .t_amb = 45.0f, .headroom = 10.0f, .ceiling_db = -3.0f,
};

static float frand(void) { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

static int run(const char *name, int secs, float amp, int kind)
{
	static struct g_state s;
	enum { B = 256 };
	float buf[G_CH][B], *io[G_CH];
	float fs = 48000, peak = 0, tmax[2] = { 0, 0 }, ceil = g_db2lin(cfg.ceiling_db);
	long blocks = (long)(secs * fs / B), b;
	int ch, i, fail = 0;
	for (ch = 0; ch < G_CH; ch++) io[ch] = buf[ch];
	g_init(&s, &cfg, fs);
	for (b = 0; b < blocks; b++) {
		for (i = 0; i < B; i++) {
			long t = b * B + i;
			float v;
			switch (kind) {
			case 0: v = amp * frand(); break;			/* white noise */
			case 1: v = amp * sinf(2 * M_PI * 3000 * t / fs); break;	/* sine */
			case 2: v = (t % 4800 == 0) ? amp : 0; break;		/* clicks */
			default: v = amp * (frand() > 0 ? 1 : -1); break;	/* square noise */
			}
			for (ch = 0; ch < G_CH; ch++) buf[ch][i] = v;
		}
		g_run(&s, &cfg, (const float *const *)io, io, B);
		for (ch = 0; ch < G_CH; ch++)
			for (i = 0; i < B; i++) if (fabsf(buf[ch][i]) > peak) peak = fabsf(buf[ch][i]);
		for (ch = 0; ch < G_CH; ch++) if (s.temp[ch] > tmax[ch / 2]) tmax[ch / 2] = s.temp[ch];
	}
	if (peak > ceil * 1.0001f) fail = 1;
	if (tmax[0] > cfg.spk[0].t_lim - cfg.headroom || tmax[1] > cfg.spk[1].t_lim - cfg.headroom) fail = 1;
	printf("%-28s peak %6.2f dBFS (ceil %.1f)  Tmax A %5.1f / B %5.1f  final T %5.1f/%5.1f  tgain %5.1f/%5.1f dB  %s\n",
	       name, 20 * log10f(peak + 1e-9f), cfg.ceiling_db, tmax[0], tmax[1], s.temp[0], s.temp[2],
	       s.tgain_db[0], s.tgain_db[1], fail ? "FAIL" : "ok");
	return fail;
}

int main(void)
{
	int f = 0;
	f |= run("white noise 0 dBFS, 600 s", 600, 1.0f, 0);
	f |= run("square noise 0 dBFS, 600 s", 600, 1.0f, 3);
	f |= run("sine 3 kHz 0 dBFS, 600 s", 600, 1.0f, 1);
	f |= run("sine 3 kHz -20 dBFS, 600 s", 600, 0.1f, 1);
	f |= run("clicks +20 dBFS, 10 s", 10, 10.0f, 2);
	f |= run("quiet noise -30 dBFS, 60 s", 60, 0.0316f, 0);
	puts(f ? "FAILED" : "all ok");
	return f;
}
