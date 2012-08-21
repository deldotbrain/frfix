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

#ifdef FULLSCREEN
#include <GL/glut.h>
#if 0
int glutCreateWindow(const char *title) {
	int retval;
	static int (*real_func)(const char *title);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glutCreateWindow");
	if ((retval = real_func(title)) > 0) {
		printf("Going fullscreen!\n");
		glutFullScreen();
	}
	return retval;
}
void glutReshapeWindow(int width, int height) { /* nop nop nop nop */ return; }
#endif
#include <GL/gl.h>
int glutCreateWindow(const char *title) {
	int retval;
	/*glutGameModeString("1280x720");*/
	/*glutGameModeString("width~1280 height~720");*/
	glutGameModeString("1920x1080");
	retval = glutEnterGameMode();
	printf("createwindow will return %i\n", retval);
	return retval;
}
#if 0
void glOrtho(GLdouble left, GLdouble right,
		GLdouble bottom, GLdouble top,
		GLdouble zNear, GLdouble zFar) {
	static void (*real_func)(GLdouble left, GLdouble right,
		GLdouble bottom, GLdouble top,
		GLdouble zNear, GLdouble zFar);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glOrtho");
	printf("glOrtho: l/r/b/t/N/F: %f/%f/%f/%f/%f/%f\n", left, right, bottom, top, zNear, zFar);
	/*real_func(left, right, bottom, top, zNear, zFar);*/
	real_func(-960.0, 960.0, 540.0, -540.0, 0.0, 1500.0);
	return;
}
#endif
#if 0
void glMatrixMode(GLenum mode) {
	static void (*real_func)(GLenum mode);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glMatrixMode");
	printf("glMatrixMode called.\n"
		"GL_MODELVIEW: %i\n"
		"GL_PROJECTION: %i\n"
		"GL_TEXTURE: %i\n"
		"GL_COLOR: %i\n"
		"this mode: %i\n",
		GL_MODELVIEW, GL_PROJECTION, GL_TEXTURE, GL_COLOR, mode);
	real_func(mode);
}
#endif
#if 0
char top_matrix = 0;
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
	static void (*real_func)(GLfloat x, GLfloat y, GLfloat z);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glTranslatef");
	printf("glTranslatef: x/y/z: %f/%f/%f\n", x, y, z);
	/*real_func(x, y, z);*/
	/*real_func(x*1.5, y*1.5, z*1.5);*/
	/*real_func(x*1.225, y*1.225, z*1.225);*/
	if (top_matrix) {
		real_func(x, y, z);
		glScalef(1.5, 1.5, 1.5);
	} else	real_func(x*1.5, y*1.5, z*1.5);
	/*glScalef(1.225, 1.225, 1.225);*/
}
#endif
#if 1
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
	/*printf("glViewport: x/y/w/h: %i/%i/%i/%i\n", x, y, width, height);*/
	static void (*real_func)(GLint x, GLint y, GLsizei width, GLsizei height);
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glViewport");
	/*real_func(x, y, width, height);*/
	real_func(0, 0, 1920, 1080);
}
#endif
#if 1
/* this seems to break things...maybe. */
void (*fr_mousefunc)(int button, int state, int x, int y);
void faked_mousefunc(int button, int state, int x, int y) {
	fr_mousefunc(button, state, x*2/3, y*2/3);
}
void (*fr_motionfunc)(int x, int y);
void faked_motionfunc(int x, int y) {
	fr_motionfunc(x*2/3, y*2/3);
}

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
#if 0
void glPushMatrix() { 
	static void (*real_func)();
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glPushMatrix");
	printf("pushmatrix!\n");
	real_func();
	top_matrix = 0;
}
void glPopMatrix() { 
	static void (*real_func)();
	if (!real_func) real_func = dlsym(RTLD_NEXT, "glPopMatrix");
	printf("popmatrix!\n");
	real_func();
	top_matrix = 1;
}
#endif
#endif

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
