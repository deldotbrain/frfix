#!/bin/bash
thisdir="$(dirname "$(readlink -f "$0")")"
[[ -f "$thisdir/frfix.so" ]] || \
	echo "Running without frfix.so!  Expect problems.  Run ./build.sh to create frfix.so."
# on 64-bit machines, kill PulseAudio and load a Pulse-less alsa.conf
#if [[ "$(uname -m)" == "x86_64" ]] && which pulseaudio >/dev/null 2>&1
if false
then	export ALSA_CONFIG_PATH=$thisdir/alsa.conf
	if  pulseaudio --check
	then	do_pulse=true
		pulseaudio --kill || \
			echo "Couldn't kill PulseAudio.  Fieldrunners will probably crash."
	fi
fi
LD_PRELOAD=$thisdir/frfix.so $thisdir/[Ff]ieldrunners
if [[ "$do_pulse" == "true" ]]
then	unset ALSA_CONFIG_PATH
	pulseaudio --start || \
		echo "Unable to restart PulseAudio."
fi
