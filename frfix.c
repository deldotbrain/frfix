/* frfix.c: fix a few v1.0 bugs with Fieldrunners
 *
 * Audio: a couple functions have been overridden, forcing FR to open the
 * system default sound device, and forcing a sane latency on the sound.
 *
 * Video: a couple functions have been intercepted to allow resolution changing
 * and fullscreen support.
 *
 * Misc: a keyboard-handler has been intercepted to keep the game from crashing
 * when Shift, Ctrl, or Alt are pressed.
 *
 * To build:
 *  $ gcc -fPIC -shared -m32 `pkg-config --cflags alsa` frfix.c -o frfix.so
 * To launch Fieldrunners (in Bash anyway):
 *  $ LD_PRELOAD=/path/to/frfix.so /path/to/Fieldrunners
 */
#define _GNU_SOURCE
#include <asoundlib.h>
#include <dlfcn.h>
#include <GL/gl.h>
#include <GL/glut.h>
#include <GL/freeglut.h>

/*{{{ Audio workaround */
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
	/* 1024 is almost always safe.  On direct hw access (which ALSA hasn't
	 * done since 2005 or so), period should be 1024 in almost all cases.
	 * With 48kHz dmix, it should be 940.  This should never let underruns
	 * happen.
	 */
	if (delay < 1024) fr_callback(ahandler);
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
/*}}}*/
/*{{{ Video workarounds & associated input workarounds */
long act_w, act_h, act_xoff, act_yoff;
float ptr_scale;
char fs = 0;

/* We don't want Fieldrunners to revert our viewport settings. */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { return; }
/* handle_reshape calculates and sets an aspect-correct viewport, and stores its
 * variables for the mouse mangler.
 */
void handle_reshape(int w, int h) {
	static void (*glvp)(GLint x, GLint y, GLsizei width, GLsizei height);
	if (!glvp) glvp = dlsym(RTLD_NEXT, "glViewport");
	act_w = w;
	act_h = h;
	act_xoff = 0;
	act_yoff = 0;
	ptr_scale = 1280.0/w;
	if (w < h * 16/9) {
		act_h = w * 9/16;
		act_yoff = (h - act_h) / 2;
	} else if (w > h * 16/9) {
		act_w = h * 16/9;
		act_xoff = (w - act_w) / 2;
		ptr_scale = 720.0/h;
	}
	/* glViewport() is NOP'd, use the real thing */
	glvp(act_xoff, act_yoff, act_w, act_h);
	/* Check if we're fullscreened and need letterbox workarounds */
	fs = ((glutGet(GLUT_SCREEN_WIDTH) == glutGet(GLUT_WINDOW_WIDTH)) &&
		(glutGet(GLUT_SCREEN_HEIGHT) == glutGet(GLUT_WINDOW_HEIGHT)));
}
/* Intercept attempts to install a reshape handler and install our own instead. */
void glutReshapeFunc(void (*func)(int width, int height)) {
	static void (*real_func)(void (*func)(int width, int height));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutReshapeFunc");
	real_func(handle_reshape);
}

/* 'f'->toggle fullscreen key, otherwise call Fieldrunners' keyboard callback.
 */
void (*fr_kbfunc)(unsigned char key, int x, int y);
void faked_kbfunc(unsigned char key, int x, int y) {
	if (key == 'f') {
		if (fs) glutReshapeWindow(1280,720);
		else glutFullScreen();
	} else fr_kbfunc(key, x, y);
}
/* Intercept calls for keyboard callbacks and inject our function to check for
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
void mangle_mouse(int *x, int *y) {
	*x = (*x - (fs?0:act_xoff)) * ptr_scale;
	*y = (*y - (fs?0:act_yoff)) * ptr_scale;
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
	mangle_mouse(&x, &y);
	fr_mousefunc(button, state, x, y);
}
void (*fr_pmotionfunc)(int x, int y);
void faked_pmotionfunc(int x, int y) {
	mangle_mouse(&x, &y);
	fr_pmotionfunc(x, y);
}
void (*fr_motionfunc)(int x, int y);
void faked_motionfunc(int x, int y) {
	mangle_mouse(&x, &y);
	fr_motionfunc(x, y);
}
/* Intercept calls for mouse callbacks and inject our manglers */
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
/*}}}*/
/*{{{ SpecialUp crash workaround */
/* Catch the release of Shift, Ctrl, Alt.  If Fieldrunners' callback catches
 * these, it asserts false and everything blows up.
 * It's not actually documented that these keys are ever returned by this
 * callback.  WTF freeglut?
 */
void (*fr_specialup)(int key, int x, int y);
void faked_specialup(int key, int x, int y) {
	if ((key < 112) || (key > 117)) fr_specialup(key, x, y);
}
/* Intercept calls for special keypress handlers and inject our handler */
void glutSpecialUpFunc(void (*func)(int key, int x, int y)) {
	static void (*real_func)(void (*func)(int key, int x, int y));
	real_func = dlsym(RTLD_NEXT, "glutSpecialUpFunc");
	fr_specialup = func;
	real_func(faked_specialup);
}
/*}}}*/

/* vim:fdm=marker
 */
