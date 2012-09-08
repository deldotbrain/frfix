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
#include <time.h>
#include <signal.h>

/*{{{ Audio workaround */
/* Fieldrunners opens 'plughw:0,0' by default.  Instead, let's open 'default'
 * like users expect.  'default' will automatically map to PulseAudio, the
 * system dmix, or whatever the user has configured as their default; 'default'
 * will almost always play nice with other applications.
 */
void *fr_audio_private;
snd_pcm_t *fr_pcm;
void (*fr_callback)(snd_async_handler_t *ahandler);

int snd_pcm_open(snd_pcm_t **pcm,
		const char *name,
		snd_pcm_stream_t stream,
		int mode);
int snd_async_add_pcm_handler(snd_async_handler_t **handler,
		snd_pcm_t *pcm,
		snd_async_callback_t callback,
		void *private_data);
int snd_pcm_avail_delay(snd_pcm_t *pcm,
		snd_pcm_sframes_t *availp,
		snd_pcm_sframes_t *delayp);
void *snd_async_handler_get_callback_private(snd_async_handler_t *ahandler);
snd_pcm_t *snd_async_handler_get_pcm(snd_async_handler_t *ahandler);
int snd_pcm_hw_params_set_rate_resample(snd_pcm_t *pcm,
		snd_pcm_hw_params_t *params,
		unsigned int val);

void setup_alsa_timer();
void alsa_callback_caller(union sigval sv);

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

/* Since FR calls this function with its default value, let's cannibalize it &
 * fix the buffer size with it.
 */
int snd_pcm_hw_params_set_rate_resample(snd_pcm_t *pcm,
		snd_pcm_hw_params_t *params,
		unsigned int val) {
	/* FR only works with 1024 samples per period. */
	snd_pcm_uframes_t period_size = 1024;
	/* FR needs >= 8192 samples to play back smoothly. */
	unsigned int periods_min = 8, periods_max = 12;
	snd_pcm_uframes_t buffer_min, buffer_max;
	int dir;
	/* FR only plays audio correctly with a period size of 1024.  Force it,
	 * and set an appropriate number of periods in order to keep playback
	 * smooth but not induce undue delay.
	 *
	 * If any of these fail, sound will not play.  I should probably check
	 * their return values...
	 */
	dir = 0;
	snd_pcm_hw_params_set_period_size_near(pcm, params, &period_size, &dir);
	dir = 0;
	snd_pcm_hw_params_set_periods_min(pcm, params, &periods_min, &dir);
	dir = 0;
	snd_pcm_hw_params_set_periods_max(pcm, params, &periods_max, &dir);
	/* Size our buffer appropriately for our periods. */
	buffer_min = period_size * periods_min;
	buffer_max = period_size * periods_max;
	snd_pcm_hw_params_set_buffer_size_min(pcm, params, &buffer_min);
	snd_pcm_hw_params_set_buffer_size_max(pcm, params, &buffer_max);
	return 0;
}

/* Intercept the hook to register Fieldrunners' audio callback since it may
 * fail.  In the event of a failure, resort to setting up a timer that calls
 * the callback fast enough to keep ALSA's buffers full.
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
	fr_audio_private = private_data;
	fr_pcm = pcm;
	if (real_func(handler, pcm, fr_callback, private_data) != 0) {
		printf("ALSA won't do callbacks...enabling workaround.\n");
		/* Start a timer to regularly call our handler. */
		setup_alsa_timer();
	}
	return 0;
}

/* The 8/30 update included a previous fix that happens to break compatibility
 * with PulseAudio.  Give the test a dummy value so that FR's audio callback
 * always runs.  That (delay < X) test was definitely the wrong way to fix the
 * latency.  Sorry for misleading you guys at Subatomic.  :(
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
 */
void setup_alsa_timer() {
	struct sigevent sev;
	timer_t alsa_timer;
	struct itimerspec enable_timer = {
		.it_interval = { .tv_sec = 0, .tv_nsec = 10000000 },
		.it_value = { .tv_sec = 0, .tv_nsec = 10000000 }
	};
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_value.sival_ptr = NULL;
	sev.sigev_notify_function = alsa_callback_caller;
	sev.sigev_notify_attributes = NULL;
	if (timer_create(CLOCK_MONOTONIC, &sev, &alsa_timer) == 0) {
		timer_settime(alsa_timer, 0, &enable_timer, 0);
	} else printf("Unable to create audio timer.  Audio won't work.\n");
}
/* Dummy function to call our audio callback */
void alsa_callback_caller(union sigval sv) { fr_callback(NULL); }
/*}}}*/
/*{{{ Video workarounds & associated input workarounds */
long act_w, act_h, act_xoff, act_yoff;
float ptr_scale;
char fs = 0;
void (*fr_kbfunc)(unsigned char key, int x, int y);
void (*fr_mousefunc)(int button, int state, int x, int y);
void (*fr_pmotionfunc)(int x, int y);
void (*fr_motionfunc)(int x, int y);

void glutReshapeFunc(void (*func)(int width, int height));
void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y));
void glutMouseFunc(void (*func)(int button, int state, int x, int y));
void glutPassiveMotionFunc(void (*func)(int x, int y));
void glutMotionFunc(void (*func)(int x, int y));
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

void handle_reshape(int w, int h);
void faked_kbfunc(unsigned char key, int x, int y);
void mangle_mouse(int *x, int *y);
void faked_mousefunc(int button, int state, int x, int y);
void faked_pmotionfunc(int x, int y);
void faked_motionfunc(int x, int y);

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
