/* frfix.c: fix sound issues with Fieldrunners
 * This overrides a couple functions, forcing FR to open the system default
 * sound device, and forces a sane amount of delay on the sound (compile
 * without -DFIXDELAY=n to see what I mean).
 *
 * To build:
 *  $ gcc -DFIXDELAY=1024 -fPIC -shared -m32 `pkg-config --cflags alsa` frfix.c -o frfix.so
 * To launch Fieldrunners (in Bash anyway):
 *  $ LD_PRELOAD=/path/to/frfix.so /path/to/Fieldrunners
 *
 * Changing -DFIXDELAY=n when building adjusts sound latency; reducing it may
 * cause audio glitches but is otherwise harmless.  48 works well for me, but
 * 1024 is a safe default: it's guaranteed not to be smaller than the default
 * period size on any reasonable driver, (mostly) regardless of sampling rate.
 */
#define _GNU_SOURCE
#include <asoundlib.h>
#include <dlfcn.h>
#include <stdio.h>

/* Fieldrunners opens 'plughw:0,0' by default.  Instead, let's open 'default'
 * like users expect.  'default' will automatically map to PulseAudio, the
 * system dmix, or whatever the user has configured as their default; 'default'
 * will almost always play nice with other applications.
 */
int snd_pcm_open(snd_pcm_t **pcm,
		const char *name,
		snd_pcm_stream_t stream,
		int mode) {

	static int (*real_func)
		(snd_pcm_t **pcm,
		const char *name,
		snd_pcm_stream_t stream,
		int mode);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "snd_pcm_open");
	return real_func(pcm, "default", stream, mode);
}

#ifdef FIXDELAY
/* This will be the actual callback function that Fieldrunners registers.
 * It'll never be called by ALSA, only by our intermediate callback.
 */
void (*fr_callback)(snd_async_handler_t *ahandler);

/* This is our fake callback.  It checks if ALSA is in danger of exhausting its
 * buffer, and calls Fieldrunners' callback if so.
 */
void fake_callback(snd_async_handler_t *ahandler) {
	snd_pcm_sframes_t avail, delay;
	snd_pcm_t *pcm = snd_async_handler_get_pcm(ahandler);
	/* Even though we don't need it, we still read available samples.  If
	 * we don't, apparently ALSA doesn't synchronize its buffers with the
	 * hardware, so snd_pcm_delay() returns garbage (4096 always) and our
	 * delay checks are meaningless.
	 */
	snd_pcm_avail_delay(pcm, &avail, &delay);
#ifdef LOGDELAY
	printf("delay: %li\n", delay);
#endif
	if (delay < (FIXDELAY)) fr_callback(ahandler);
}

/* Intercept the hook to register Fieldrunners' audio callback, and replace it
 * with our intermediate function instead.
 */
int snd_async_add_pcm_handler(snd_async_handler_t **handler,
		snd_pcm_t *pcm,
		snd_async_callback_t callback,
		void *private_data) {

	static int (*real_func)
		(snd_async_handler_t **handler,
		snd_pcm_t *pcm,
		snd_async_callback_t callback,
		void *private_data);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "snd_async_add_pcm_handler");

	fr_callback = callback;
	return real_func(handler, pcm, fake_callback, private_data);
}
#endif

#ifdef CHANGERES
#include <GL/gl.h>
#include <GL/glut.h>
long act_w, act_h, act_xoff, act_yoff;
void init_manglers(int w, int h) {
	if (w == h * 16/9) {
		act_w = w;
		act_h = h;
		act_yoff = 0;
		act_xoff = 0;
	} else if (w < h * 16/9) {
		act_w = w;
		act_h = w * 9/16;
		act_yoff = (h - act_h) / 2;
		act_xoff = 0;
	} else {
		act_w = h * 16/9;
		act_h = h;
		act_yoff = 0;
		act_xoff = (w - act_w) / 2;
	}
	glViewport(0, 0, 0, 0); /* these will be overridden anyway */
}
#ifdef FULLSCREEN
/* Ignore CreateWindow calls, since EnterGameMode will create a window for us. */
int glutCreateWindow(const char *title) {
	int retval;
	/*char *gmstr;
	gmstr = malloc(64);
	snprintf(gmstr, 64, "width=%i height=%i", newres[0], newres[1]);
	glutGameModeString(gmstr);
	free(gmstr);*/
	glutReshapeFunc(0);
	glutGameModeString("");
	if ((retval = glutEnterGameMode()) > 0)
		init_manglers(
			glutGameModeGet(GLUT_GAME_MODE_WIDTH),
			glutGameModeGet(GLUT_GAME_MODE_HEIGHT));
	return retval;
}
#else
/*
void glutInitWindowSize(int width, int height) {
	static void (*real_func)(int width, int height);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutInitWindowSize");
	real_func(newres[0], newres[1]);
	init_manglers(newres[0], newres[1]);
}
*/
void glutReshapeFunc(void (*func)(int width, int height)) {
	static void (*real_func)(void (*func)(int width, int height));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutReshapeFunc");
	real_func(init_manglers);
}
void glutReshapeWindow(int width, int height) { /* nop nop nop nop */ return; }
#endif
/* Ignore given values and use our calculated values instead.  This supports
 * arbitrary resolutions.
 */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
	static void (*real_func)(GLint x, GLint y, GLsizei width, GLsizei height);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glViewport");
	real_func(act_xoff, act_yoff, act_w, act_h);
}

/* Mangle the mouse so the cursor lines up with the desktop */
void (*fr_mousefunc)(int button, int state, int x, int y);
void faked_mousefunc(int button, int state, int x, int y) {
	fr_mousefunc(button, state, x-act_xoff, y-act_yoff);
}
void (*fr_motionfunc)(int x, int y);
void faked_motionfunc(int x, int y) {
	fr_motionfunc(x-act_xoff, y-act_yoff);
}

/* Intercept calls for mouse callbacks, and inject our manglers */
void glutMouseFunc(void (*func)(int button, int state, int x, int y)) {
	static void (*real_func)(void (*func)(int button, int state, int x, int y));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutMouseFunc");
	printf("Injecting mouse wrangler\n");
	fr_mousefunc = func;
	real_func(faked_mousefunc);
}
void glutPassiveMotionFunc(void (*func)(int x, int y)) {
	static void (*real_func)(void (*func)(int x, int y));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutPassiveMotionFunc");
	printf("Injecting motion wrangler\n");
	fr_motionfunc = func;
	real_func(faked_motionfunc);
}
#endif
