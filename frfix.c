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
 */
#define _GNU_SOURCE
#include <asoundlib.h>
#include <dlfcn.h>
#include <GL/gl.h>
#include <GL/glut.h>
#include <GL/freeglut.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>

/*{{{ Audio workarounds */
/* FR-supplied data */
void *fr_audio_private;
snd_pcm_t *fr_pcm;
void (*fr_callback)(snd_async_handler_t *ahandler);

/* Overridden ALSA functions */
int snd_pcm_open(snd_pcm_t **pcm,
		const char *name,
		snd_pcm_stream_t stream,
		int mode);
int snd_pcm_hw_params_set_channels(snd_pcm_t *pcm,
		snd_pcm_hw_params_t *params,
		unsigned int val);
int snd_async_add_pcm_handler(snd_async_handler_t **handler,
		snd_pcm_t *pcm,
		snd_async_callback_t callback,
		void *private_data);
int snd_pcm_avail_delay(snd_pcm_t *pcm,
		snd_pcm_sframes_t *availp,
		snd_pcm_sframes_t *delayp);
void *snd_async_handler_get_callback_private(snd_async_handler_t *ahandler);
snd_pcm_t *snd_async_handler_get_pcm(snd_async_handler_t *ahandler);

/* Support functions for the timer workaround */
void setup_alsa_timer();
void alsa_callback_caller(int arg);

/* Open 'default', no matter what FR tells us to do.
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

/*
 * This used to use _set_rate_resample because it didn't require looking up the
 * real function, but has been changed to _set_channels.  This means that FR
 * will do all of its configuration before we start mangling settings.  Ideally
 * keeps FR's calls from failing & disabling audio.
 *
 * The number of parameters that are set has been grossly reduced.  We use the
 * smallest available buffer that's above a minimum size needed to avoid
 * glitches.  These sizes are roughly:
 * FR->ALSA: 1024
 * FR->ALSA->PA: 512
 */
int snd_pcm_hw_params_set_channels(snd_pcm_t *pcm,
		snd_pcm_hw_params_t *params,
		unsigned int val) {
	static int (*real_func)(snd_pcm_t *pcm,
		snd_pcm_hw_params_t *params,
		unsigned int val);
	snd_pcm_uframes_t buffer;

	if (!real_func) real_func = dlsym(RTLD_NEXT, "snd_pcm_hw_params_set_channels");
	real_func(pcm, params, val);

	buffer = 1024;
	snd_pcm_hw_params_set_buffer_size_min(pcm, params, &buffer);
	snd_pcm_hw_params_set_buffer_size_first(pcm, params, &buffer);
	printf("ended up with %lu buffer.\n", buffer);
	return 0;
}

/* Intercept the hook to register Fieldrunners' audio callback and use our own
 * functions instead.  Since this calls the callback more frequently than ALSA
 * would, it allows for lower latency without introducing glitches, and works
 * around PulseAudio's lack of async code.
 */
int snd_async_add_pcm_handler(snd_async_handler_t **handler,
		snd_pcm_t *pcm,
		snd_async_callback_t callback,
		void *private_data) {
	fr_callback = callback;
	fr_audio_private = private_data;
	fr_pcm = pcm;
	setup_alsa_timer();
	return 0;
}

/* The 8/30 update introduced a check to ensure that audio latency wasn't
 * getting out of hand.  Unfortunately, it may break PulseAudio if ALSA is
 * using a larger buffer than it should.  In theory, the new configuration code
 * means that buffer sizes will be reasonable, and therefore delay will never
 * be >1024, and the delay check won't cause any problems.
 *
 * Somewhere, there's bound to be a distribution with a modified PulseAudio
 * package that will break without this.  The smart money is on Debian being
 * that distro.
 */
int snd_pcm_avail_delay(snd_pcm_t *pcm,
		snd_pcm_sframes_t *availp,
		snd_pcm_sframes_t *delayp) {
	static int (*real_func)(snd_pcm_t *pcm,
		snd_pcm_sframes_t *availp,
		snd_pcm_sframes_t *delayp);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "snd_pcm_avail_delay");
	real_func(pcm, availp, delayp);
	*delayp = 0;
	return 0;
}

/* Since we may be circumventing ALSA's async_handler stuff (and feeding FR a
 * garbage pointer that shouldn't ever be used), it's easier to track this
 * ourselves.
 */
void *snd_async_handler_get_callback_private(snd_async_handler_t *ahandler) {
	return fr_audio_private;
}
snd_pcm_t *snd_async_handler_get_pcm(snd_async_handler_t *ahandler) {
	return fr_pcm;
}

/* Set up a timer to fire every 10ms, calling alsa_callback_caller().  If we're
 * using PulseAudio, this is the only way to regularly call FR's audio
 * callback.
 *
 * Using signals should be marginally more effective than threads.
 */
void alsa_callback_caller(int arg) { fr_callback(NULL); }

void setup_alsa_timer() {
	timer_t alsa_timer;
	struct itimerspec enable_timer = {
		.it_interval = { .tv_sec = 0, .tv_nsec = 10000000 },
		.it_value = { .tv_sec = 0, .tv_nsec = 10000000 }
	};

	signal(SIGALRM, &alsa_callback_caller);
	timer_create(CLOCK_MONOTONIC, NULL, &alsa_timer);
	timer_settime(alsa_timer, 0, &enable_timer, 0);
}
/*}}}*/
/*{{{ Video workarounds & associated input workarounds */
/* FR-supplied callbacks */
void (*fr_kbfunc)(unsigned char key, int x, int y);
void (*fr_mousefunc)(int button, int state, int x, int y);
void (*fr_pmotionfunc)(int x, int y);
void (*fr_motionfunc)(int x, int y);

/* Overridden GL(UT) functions */
void glutReshapeFunc(void (*func)(int width, int height));
void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y));
void glutMouseFunc(void (*func)(int button, int state, int x, int y));
void glutPassiveMotionFunc(void (*func)(int x, int y));
void glutMotionFunc(void (*func)(int x, int y));
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

/* Intermediate callback functions; support functions */
void handle_reshape(int w, int h);
void faked_kbfunc(unsigned char key, int x, int y);
void mangle_mouse(int *x, int *y);
void faked_mousefunc(int button, int state, int x, int y);
void faked_pmotionfunc(int x, int y);
void faked_motionfunc(int x, int y);

/* Mouse mangler parameters */
long act_w, act_h, act_xoff, act_yoff;
float ptr_scale;
char fs = 0;

/* We don't want Fieldrunners to revert our viewport settings.  Disable
 * glViewport, and look it up where it's needed.
 */
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
	glvp(act_xoff, act_yoff, act_w, act_h);
	/* Check if we're fullscreened and need letterbox workarounds.  There's
	 * not really a good way to check this, but the check we use is a good
	 * compromise.
	 */
	fs = ((glutGet(GLUT_SCREEN_WIDTH) == glutGet(GLUT_WINDOW_WIDTH)) &&
		(glutGet(GLUT_SCREEN_HEIGHT) == glutGet(GLUT_WINDOW_HEIGHT)));
}
/* Intercept attempts to install a reshape handler and install our own instead. */
void glutReshapeFunc(void (*func)(int width, int height)) {
	static void (*real_func)(void (*func)(int width, int height));
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutReshapeFunc");
	real_func(handle_reshape);
}

/* Our keyboard handler, used for new keybindings:
 * 'f'->toggle fullscreen key, otherwise call Fieldrunners' keyboard callback.
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
/* Mangle the mouse so the cursor lines up with the desktop, then call FR's
 * associated callback with the adjusted coordinates.
 */
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
 *
 * It's not actually documented that these keys are ever returned by this
 * callback, and is specifically recommended to use a different function to
 * check for them.  WTF freeglut?
 */
void (*fr_specialup)(int key, int x, int y);

void glutSpecialUpFunc(void (*func)(int key, int x, int y));

void faked_specialup(int key, int x, int y);

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
