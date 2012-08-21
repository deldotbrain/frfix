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
void (*fr_callback)(snd_async_handler_t *ahandler);

void fake_callback(snd_async_handler_t *ahandler) {
	snd_pcm_sframes_t avail, delay;
	snd_pcm_t *pcm = snd_async_handler_get_pcm(ahandler);
	snd_pcm_avail_delay(pcm, &avail, &delay);
#ifdef LOGDELAY
	printf("delay: %li\n", delay);
#endif
	if (delay < (FIXDELAY)) fr_callback(ahandler);
}

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
