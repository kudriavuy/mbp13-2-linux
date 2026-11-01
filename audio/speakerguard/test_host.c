/* Minimal LADSPA host: loads the plugin like PipeWire does, with the
 * fallback defaults (no controls connected) and with test controls.
 * Usage: ./test_host ./mbp_speakerguard.so */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include "ladspa_min.h"

int main(int argc, char **argv)
{
	void *lib = dlopen(argc > 1 ? argv[1] : "./mbp_speakerguard.so", RTLD_NOW);
	LADSPA_Descriptor *(*get)(unsigned long);
	const LADSPA_Descriptor *d;
	float buf[4][1024], ctl[64], peak = 0;
	unsigned long p;
	int pass, i, ch, n_in = 0, n_out = 0, fail = 0;
	if (!lib) { printf("dlopen: %s\n", dlerror()); return 1; }
	get = (LADSPA_Descriptor *(*)(unsigned long))dlsym(lib, "ladspa_descriptor");
	if (!get || !(d = get(0))) { puts("no descriptor"); return 1; }
	printf("%s (%s), %lu ports\n", d->Name, d->Label, d->PortCount);
	for (pass = 0; pass < 2; pass++) {
		LADSPA_Handle h = d->instantiate(d, 48000);
		n_in = n_out = 0;
		for (p = 0; p < d->PortCount; p++) {
			int pd = d->PortDescriptors[p];
			if (pd & LADSPA_PORT_AUDIO)
				d->connect_port(h, p, buf[(pd & LADSPA_PORT_INPUT) ? n_in++ : n_out++]);
			else if (pass == 1) {
				ctl[p] = 0;	/* outputs */
				d->connect_port(h, p, &ctl[p]);
			}
		}
		if (pass == 1) {	/* plausible values, ceiling -3 dBFS */
			float v[] = { 6, 60, 1.5, 60, 40, 120, 4, 30, 2.5, 30, 100, 130, 10, 45, 10, -3 };
			for (i = 0; i < 16; i++) ctl[8 + i] = v[i];
		}
		d->activate(h);
		peak = 0;
		for (i = 0; i < 48000 * 30 / 1024; i++) {
			int k;
			for (ch = 0; ch < 4; ch++) for (k = 0; k < 1024; k++) buf[ch][k] = sinf(k * 0.3f);
			d->run(h, 1024);	/* in place: in and out are the same buffers */
			for (ch = 0; ch < 4; ch++) for (k = 0; k < 1024; k++)
				if (fabsf(buf[ch][k]) > peak) peak = fabsf(buf[ch][k]);
		}
		printf("%s: 30 s full-scale sine -> peak %.2f dBFS", pass ? "configured" : "fallback  ",
		       20 * log10f(peak));
		if (pass) printf(", T %.1f/%.1f/%.1f/%.1f degC, thermal %.1f/%.1f dB",
				 ctl[24], ctl[25], ctl[26], ctl[27], ctl[28], ctl[29]);
		puts("");
		if (peak > (pass ? 0.7080f : 0.1001f)) fail = 1;
		d->cleanup(h);
	}
	puts(fail ? "FAILED" : "host ok");
	return fail;
}
