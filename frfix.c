/* frfix.c: fix a few v1.0 bugs with Fieldrunners
 * Copyright (C) 2012-2014, Ryan Pennucci <decimalman@gmail.com>
 *
 * Audio: a couple functions have been overridden, forcing FR to open the
 * system default sound device, forcing a sane latency on the sound, and
 * emulating ALSA's async interface to support e.g. PulseAudio.
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

/* Useful for debugging when you have 3 sound cards installed :D */
#ifndef FRDEV
#define FRDEV "default"
#endif
/* Buffer length in ms, 10 works nicely */
#ifndef FRBUF
#define FRBUF (10)
#endif

/*{{{ Audio workarounds */
/* FR-supplied async stuff */
static void *fr_audio_private;
static snd_pcm_t *fr_pcm;
static void (*fr_callback)(snd_async_handler_t *ahandler);

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

/* Support function for the timer workaround */
static void alsa_callback_thread(void *arg);
static pthread_spinlock_t callback_lock;

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
	return real_func(pcm, FRDEV, stream, mode);
}

/*
 * This used to use _set_rate_resample because it didn't require looking up the
 * real function, but has been changed to _set_channels.  This means that FR
 * will do all of its configuration before we start mangling settings.  Ideally
 * this keeps FR's calls from failing & disabling audio.
 *
 * The number of parameters that are set has been grossly reduced.  The only
 * setting changed is buffer duration.  Buffer size and callback frequency are
 * determined by FRBUF.  Since both are adjusted together, there's not really a
 * lower limit on buffer size, but 10ms is a reasonable default.
 */
int snd_pcm_hw_params_set_channels(snd_pcm_t *pcm,
		snd_pcm_hw_params_t *params,
		unsigned int val) {
	static int (*real_func)(snd_pcm_t *pcm,
		snd_pcm_hw_params_t *params,
		unsigned int val);
	unsigned int buffer;
	int dir;

	if (!real_func) real_func = dlsym(RTLD_NEXT, "snd_pcm_hw_params_set_channels");
	if (real_func(pcm, params, val) < 0)
		printf("Unable to configure audio channels.\n");

	buffer = FRBUF * 8000;
	dir = 0;
	if (snd_pcm_hw_params_set_buffer_time_min(pcm, params, &buffer, &dir) < 0)
		printf("Unable to set minimum buffer size; got %u (%i) instead.\n", buffer, dir);
	if (snd_pcm_hw_params_set_buffer_time_first(pcm, params, &buffer, &dir) < 0)
		printf("Strangely, couldn't use first buffer size; got %u (%i) instead.\n", buffer, dir);
	return 0;
}

/* Emulate ALSA's SIGIO async interface.  We don't use signals here, since
 * pthreads' locking primitives seem to prefer being called from separate
 * threads.
 */
int snd_async_add_pcm_handler(snd_async_handler_t **handler,
		snd_pcm_t *pcm,
		snd_async_callback_t callback,
		void *private_data) {
	timer_t alsa_timer;
	struct itimerspec enable_timer = {
		.it_interval = { .tv_sec = 0, .tv_nsec = FRBUF * 2000000 },
		.it_value = { .tv_sec = 0, .tv_nsec = FRBUF * 2000000 }
	};
	struct sigevent timer_thread = {
		.sigev_notify = SIGEV_THREAD,
		.sigev_notify_function = (void *)alsa_callback_thread,
		.sigev_value = { .sival_ptr = NULL },
	};

	/* Store data that FR expects ALSA to return */
	fr_callback = callback;
	fr_audio_private = private_data;
	fr_pcm = pcm;

	if (pthread_spin_init(&callback_lock, 0)) {
		printf("Unable to initialize async lock.\n");
		return -1;
	}
	if (timer_create(CLOCK_MONOTONIC, &timer_thread, &alsa_timer) < 0) {
		printf("Unable to create timer.\n");
		return -1;
	}
	if (timer_settime(alsa_timer, 0, &enable_timer, 0) < 0) {
		printf("Unable to start timer.\n");
		return -1;
	}

	return 0;
}

/* The 8/30 update introduced a check to ensure that audio latency wasn't
 * getting out of hand.  Unfortunately, it may break PulseAudio if ALSA is
 * using a larger buffer than it should.  In theory, the new configuration code
 * means that buffer sizes will be reasonable, and therefore delay will never
 * be >1024, and the delay check won't cause any problems.
 *
 * Somewhere, there's bound to be a modified PulseAudio package or stupid audio
 * drivers that will break without this.  Since it's harmless, it's staying.
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

/* Since we circumvent ALSA's async_handler stuff (and feed FR a garbage
 * pointer that shouldn't ever be used), we have to track this ourselves.
 */
void *snd_async_handler_get_callback_private(snd_async_handler_t *ahandler) {
	return fr_audio_private;
}
snd_pcm_t *snd_async_handler_get_pcm(snd_async_handler_t *ahandler) {
	return fr_pcm;
}

/* Pass a null pointer to Fieldrunners so we can segfault later.  True story.
 */
static void alsa_callback_thread(void *arg) {
	if (pthread_spin_trylock(&callback_lock))
		return;

	fr_callback(NULL);
	pthread_spin_unlock(&callback_lock);
}
/*}}}*/
/*{{{ Video workarounds & associated input workarounds */
/* FR-supplied callbacks */
static void (*fr_kbfunc)(unsigned char key, int x, int y);
static void (*fr_mousefunc)(int button, int state, int x, int y);
static void (*fr_pmotionfunc)(int x, int y);
static void (*fr_motionfunc)(int x, int y);

/* Overridden GL(UT) functions */
void glutReshapeFunc(void (*func)(int width, int height));
void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y));
void glutMouseFunc(void (*func)(int button, int state, int x, int y));
void glutPassiveMotionFunc(void (*func)(int x, int y));
void glutMotionFunc(void (*func)(int x, int y));
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

/* Intermediate callback functions; support functions */
static void handle_reshape(int w, int h);
static void faked_kbfunc(unsigned char key, int x, int y);
static void mangle_mouse(int *x, int *y);
static void faked_mousefunc(int button, int state, int x, int y);
static void faked_pmotionfunc(int x, int y);
static void faked_motionfunc(int x, int y);

/* Mouse mangler parameters */
static long act_w, act_h, act_xoff, act_yoff;
static float ptr_scale;
static int fs = 0;

/* We don't want Fieldrunners to revert our viewport settings.  Disable
 * glViewport, and look it up where it's needed.
 */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { return; }

/* handle_reshape calculates and sets an aspect-correct viewport, and stores its
 * variables for the mouse mangler.
 */
static void handle_reshape(int w, int h) {
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
static void (*fr_kbfunc)(unsigned char key, int x, int y);
static int saved_w, saved_h;
static void toggle_fullscreen(void) {
	if (fs) {
		fs = 0;
		glutReshapeWindow(saved_w, saved_h);
	} else {
		saved_w = glutGet(GLUT_WINDOW_WIDTH);
		saved_h = glutGet(GLUT_WINDOW_HEIGHT);
		fs = 1;
		glutFullScreen();
	}
}
static void faked_kbfunc(unsigned char key, int x, int y) {
	switch (key) {
	case 'f':
		toggle_fullscreen();
		break;
	case '\r':
		if (glutGetModifiers() == GLUT_ACTIVE_ALT) {
			toggle_fullscreen();
			return;
		}
		/* fall through */
	default:
		fr_kbfunc(key, x, y);
	}
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
static void mangle_mouse(int *x, int *y) {
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
static void (*fr_mousefunc)(int button, int state, int x, int y);
void faked_mousefunc(int button, int state, int x, int y) {
	mangle_mouse(&x, &y);
	fr_mousefunc(button, state, x, y);
}
static void (*fr_pmotionfunc)(int x, int y);
void faked_pmotionfunc(int x, int y) {
	mangle_mouse(&x, &y);
	fr_pmotionfunc(x, y);
}
static void (*fr_motionfunc)(int x, int y);
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
static void (*fr_specialup)(int key, int x, int y);

void glutSpecialUpFunc(void (*func)(int key, int x, int y));

static void faked_specialup(int key, int x, int y);

static void faked_specialup(int key, int x, int y) {
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
