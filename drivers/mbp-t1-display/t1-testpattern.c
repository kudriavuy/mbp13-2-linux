/* Draw a test pattern on the Touch Bar's DRM device (appletbdrm) and hold it.
 *   sudo ./t1-testpattern [seconds]      (default 60; Ctrl+C ends)
 * Colour bars plus a white frame, so a wrong rotation or size shows. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 60, fd = -1, i;
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
	drmModeModeInfo m = con->modes[0];
	struct drm_mode_create_dumb cd = { .width = m.hdisplay, .height = m.vdisplay, .bpp = 32 };
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { perror("create dumb"); return 1; }
	uint32_t fb;
	if (drmModeAddFB(fd, m.hdisplay, m.vdisplay, 24, 32, cd.pitch, cd.handle, &fb)) { perror("addfb"); return 1; }
	struct drm_mode_map_dumb md = { .handle = cd.handle };
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { perror("map dumb"); return 1; }
	uint8_t *px = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
	if (px == MAP_FAILED) { perror("mmap"); return 1; }
	static const uint32_t bars[] = { 0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x404040 };
	for (uint32_t y = 0; y < m.vdisplay; y++)
		for (uint32_t x = 0; x < m.hdisplay; x++) {
			uint32_t c = bars[x * 8 / m.hdisplay];
			if (x < 4 || y < 4 || x >= m.hdisplay - 4u || y >= m.vdisplay - 4u) c = 0xffffff;
			((uint32_t *)(px + y * cd.pitch))[x] = c;
		}
	if (drmModeSetCrtc(fd, res->crtcs[0], fb, 0, 0, &con->connector_id, 1, &m)) { perror("setcrtc"); return 1; }
	printf("%s: %ux%u, pattern shown for %d s\n", path, m.hdisplay, m.vdisplay, secs);
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
	for (i = 0; i < secs * 10 && !stop; i++) {
		/* flush the whole frame now and then: the panel only gets damage */
		if (i % 10 == 0) { drmModeClip clip = { 0, 0, m.hdisplay, m.vdisplay }; drmModeDirtyFB(fd, fb, &clip, 1); }
		usleep(100000);
	}
	drmModeSetCrtc(fd, res->crtcs[0], 0, 0, 0, NULL, 0, NULL);
	return 0;
}
