#!/bin/bash
if ! thisdir="$(dirname "$(readlink -f "$0")")"
then	echo "Couldn't find Fieldrunners directory."
	exit 1
fi
if ! [[ -f "$thisdir/frfix.so" ]]
then	echo "frfix.so not found, attempting to build."
	if ! (cd "$thisdir"; bash build.sh)
	then	echo "Couldn't build frfix.so.  You may have to run build.sh as root (eg. sudo ./build.sh)"
		exit 2
	fi
fi
if [[ "$(uname -m)" == "x86_64" ]] && which pulseaudio >/dev/null 2>&1
then	# avoid trying to load pulse on 64-bit machines.
	export ALSA_CONFIG_PATH=$thisdir/alsa.conf
	if  pulseaudio --check
	then	do_pulse=true
		echo "PulseAudio is running.  Stopping it."
		if ! pulseaudio --kill
		then	echo "Couldn't stop PulseAudio."
			exit 3
		fi
	fi
fi
LD_PRELOAD=$thisdir/frfix.so $thisdir/Fieldrunners
if [[ "$do_pulse" == "true" ]]
then	echo "Restarting PulseAudio."
	unset ALSA_CONFIG_PATH
	pulseaudio --start || echo "Unable to restart PulseAudio."
fi
