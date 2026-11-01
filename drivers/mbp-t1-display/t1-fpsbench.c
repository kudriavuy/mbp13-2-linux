/* Frame rate of the Touch Bar (appletbdrm): time drmModeDirtyFB for damage
 * rectangles of several sizes. The driver sends each damage rectangle over
 * USB and waits for the T1's answer inside the call, so its duration is the
 * frame time. Needs the DRM device to itself (stop tiny-dfr first).
 *   sudo ./t1-fpsbench [seconds per size [px ...]]   (default 3; 2170 900 400 130)
 * Sizes are along the bar (2170 px), always the full 60 px height. */
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }
static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 3;
	int fd = -1, i;
	char path[32];
	drmModeRes *res = NULL;
	drmModeConnector *con = NULL;

	for (i = 0; i < 16 && fd < 0; i++) {
		drmVersionPtr v;
		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0) continue;
		v = drmGetVersion(fd);
		if (!v || strcmp(v->name, "appletbdrm")) { if (v) drmFreeVersion(v); close(fd); fd = -1; continue; }
		drmFreeVersion(v);
	}
	if (fd < 0) { fprintf(stderr, "no appletbdrm device (T1 in configuration 2, module loaded?)\n"); return 1; }
	res = drmModeGetResources(fd);
	for (i = 0; res && i < res->count_connectors && !con; i++) {
		con = drmModeGetConnector(fd, res->connectors[i]);
		if (con && (con->connection != DRM_MODE_CONNECTED || !con->count_modes)) { drmModeFreeConnector(con); con = NULL; }
	}
	if (!con) { fprintf(stderr, "%s: no connected connector\n", path); return 1; }
	drmModeModeInfo m = con->modes[0];	/* portrait: 60 x 2170 */
	struct drm_mode_create_dumb cd = { .width = m.hdisplay, .height = m.vdisplay, .bpp = 32 };
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { perror("create dumb"); return 1; }
	uint32_t fb;
	if (drmModeAddFB(fd, m.hdisplay, m.vdisplay, 24, 32, cd.pitch, cd.handle, &fb)) { perror("addfb"); return 1; }
	struct drm_mode_map_dumb md = { .handle = cd.handle };
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { perror("map dumb"); return 1; }
	uint8_t *px = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
	if (px == MAP_FAILED) { perror("mmap"); return 1; }
	memset(px, 0, cd.size);
	if (drmModeSetCrtc(fd, res->crtcs[0], fb, 0, 0, &con->connector_id, 1, &m)) { perror("setcrtc"); return 1; }
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

	uint32_t sizes[64] = { 2170, 900, 400, 130 };
	unsigned nsizes = 4;
	if (argc > 2)
		for (nsizes = 0; nsizes < 64 && (int)nsizes + 2 < argc; nsizes++)
			sizes[nsizes] = atoi(argv[nsizes + 2]);
	static double t[100000];
	printf("%s %ux%u, %.1f s per size\n", path, m.hdisplay, m.vdisplay, secs);
	printf("%8s %8s %8s %8s %8s %8s\n", "px", "frames", "fps", "avg ms", "p95 ms", "max ms");
	for (unsigned s = 0; s < nsizes && !stop; s++) {
		uint32_t len = sizes[s] > m.vdisplay ? m.vdisplay : sizes[s];
		uint32_t y0 = m.vdisplay - len;	/* right end of the bar, where the strip is */
		int n = 0;
		double start = now_ms(), sum = 0;
		while (!stop && now_ms() - start < secs * 1e3 && n < 100000) {
			/* a moving bright bar, like a slider knob: every frame differs */
			uint32_t knob = y0 + (uint32_t)(n * 7) % len;
			for (uint32_t y = y0; y < y0 + len; y++) {
				uint32_t c = (y >= knob && y < knob + 20) ? 0xffffff : 0x363638;
				uint32_t *row = (uint32_t *)(px + y * cd.pitch);
				for (uint32_t x = 0; x < m.hdisplay; x++) row[x] = c;
			}
			drmModeClip clip = { 0, y0, m.hdisplay, y0 + len };
			double a = now_ms();
			if (drmModeDirtyFB(fd, fb, &clip, 1)) { perror("dirtyfb"); return 1; }
			t[n] = now_ms() - a;
			sum += t[n++];
		}
		double wall = now_ms() - start;
		qsort(t, n, sizeof(*t), cmp);
		printf("%8u %8d %8.1f %8.2f %8.2f %8.2f\n", len, n, n * 1e3 / wall, sum / n,
		       t[(int)(n * 0.95)], t[n - 1]);
		fflush(stdout);
	}
	memset(px, 0, cd.size);
	drmModeClip all = { 0, 0, m.hdisplay, m.vdisplay };
	drmModeDirtyFB(fd, fb, &all, 1);
	drmModeSetCrtc(fd, res->crtcs[0], 0, 0, 0, NULL, 0, NULL);
	return 0;
}
