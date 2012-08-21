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
 * 1024 is a safe default.
 */
#define _GNU_SOURCE
#include <asoundlib.h>
#include <dlfcn.h>

/* Fieldrunners opens 'plughw:0,0' by default, with crazy settings that just
 * don't work well.  Let's open 'default' like users expect.
 * It looks like the developers read one of the major ALSA tutorials while
 * porting this.  The tutorial in question is off-base on a lot of things.
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
	 * hardware, so snd_pcm_delay() returns garbage (4096 always) and audio
	 * gets glitchy.
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
