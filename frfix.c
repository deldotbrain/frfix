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
 * cause audio glitches but is otherwise harmless.  44 works well for me, but
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
/* Check if ALSA is in danger of exhausting its buffer, and call Fieldrunners'
 * callback if so.
 */
void (*fr_callback)(snd_async_handler_t *ahandler);
void fake_callback(snd_async_handler_t *ahandler) {
	snd_pcm_sframes_t avail, delay;
	snd_pcm_t *pcm = snd_async_handler_get_pcm(ahandler);
	/* Even though we don't need it, we still read available samples.  If
	 * we don't, apparently ALSA doesn't synchronize its buffers with the
	 * hardware, so snd_pcm_delay() returns garbage (4096 always) and our
	 * delay checks are meaningless.
	 */
	snd_pcm_avail_delay(pcm, &avail, &delay);
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
#include <GL/freeglut.h>

long act_w, act_h, act_xoff, act_yoff;
float ptr_scale;
char fs = 0;

/* init_manglers calculates and sets an aspect-correct viewport, and stores its
 * variables for the mouse manglers.
 */
void init_manglers(int w, int h) {
	static void (*glvp)(GLint x, GLint y, GLsizei width, GLsizei height);
	if (!glvp) glvp = dlsym(RTLD_NEXT, "glViewport");
	if (w == h * 16/9) {
		act_w = w;
		act_h = h;
		act_xoff = 0;
		act_yoff = 0;
	} else if (w < h * 16/9) {
		act_w = w;
		act_h = w * 9/16;
		act_xoff = 0;
		act_yoff = (h - act_h) / 2;
	} else {
		act_w = h * 16/9;
		act_h = h;
		act_xoff = (w - act_w) / 2;
		act_yoff = 0;
	}
	ptr_scale = ((1280.0/act_w)>(720.0/act_h)?(1280.0/act_w):(720.0/act_h));
	glvp(act_xoff, act_yoff, act_w, act_h);
}
/* Override attempts to install a reshape handler and install our own instead. */
void glutReshapeFunc(void (*func)(int width, int height)) {
	static void (*real_func)(void (*func)(int width, int height));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutReshapeFunc");
	real_func(init_manglers);
}
/* We don't want these affecting our display.  Just ignore them instead. */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { return; }
void glutReshapeWindow(int width, int height) { return; }

/* Add 'q'->quit and 'f'->toggle fullscreen keys, otherwise call Fieldrunners'
 * keyboard callback.
 */
void (*fr_kbfunc)(unsigned char key, int x, int y);
void faked_kbfunc(unsigned char key, int x, int y) {
	static void (*glrw)(int width, int height);
	if (!glrw) glrw = dlsym(RTLD_NEXT, "glutReshapeWindow");
	switch (key) {
	case 'q':
		glutLeaveMainLoop();
		break;
	case 'f':
		if (fs) {
			glrw(1280,720);
			fs = 0;
		} else {
			glutFullScreen();
			fs = 1;
		}
		break;
	default:
		fr_kbfunc(key, x, y);
	}
}

/* Intercept calls for keyboard callbacks, and inject our function to check for
 * extra keybindings.
 */
void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y)) {
	static void (*real_func)(void (*func)(unsigned char key, int x, int y));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutKeyboardFunc");
	fr_kbfunc = func;
	real_func(faked_kbfunc);
}

/* Mangle & bounds-check the pointer.  It should line up with the desktop when
 * windowed, and never disappear into the letterbox when fullscreen.
 */
void mmangle(int *x, int *y) {
	*x = *x - (fs?0:act_xoff);
	*y = *y - (fs?0:act_yoff);
	*x = *x * ptr_scale;
	*y = *y * ptr_scale;
	if (fs) {
		if (*x > 1280) {
			glutWarpPointer(act_w, *y / ptr_scale);
			*x = 1280;
		}
		if (*y > 720) {
			glutWarpPointer(*x / ptr_scale, act_h);
			*y = 720;
		}
	}
}
/* Mangle the mouse so the cursor lines up with the desktop */
void (*fr_mousefunc)(int button, int state, int x, int y);
void faked_mousefunc(int button, int state, int x, int y) {
	mmangle(&x, &y);
	fr_mousefunc(button, state, x, y);
}
void (*fr_pmotionfunc)(int x, int y);
void faked_pmotionfunc(int x, int y) {
	mmangle(&x, &y);
	fr_pmotionfunc(x, y);
}
void (*fr_motionfunc)(int x, int y);
void faked_motionfunc(int x, int y) {
	mmangle(&x, &y);
	fr_motionfunc(x, y);
}

/* Intercept calls for mouse callbacks, and inject our manglers */
void glutMouseFunc(void (*func)(int button, int state, int x, int y)) {
	static void (*real_func)(void (*func)(int button, int state, int x, int y));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutMouseFunc");
	fr_mousefunc = func;
	real_func(faked_mousefunc);
}
void glutPassiveMotionFunc(void (*func)(int x, int y)) {
	static void (*real_func)(void (*func)(int x, int y));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutPassiveMotionFunc");
	fr_pmotionfunc = func;
	real_func(faked_pmotionfunc);
}
void glutMotionFunc(void (*func)(int x, int y)) {
	static void (*real_func)(void (*func)(int x, int y));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutMotionFunc");
	fr_motionfunc = func;
	real_func(faked_motionfunc);
}
#endif
