#!/bin/bash
if ! thisdir="$(dirname "$(readlink -f "$0")")"
then	echo "Couldn't find Fieldrunners directory."
	echo "Either readlink doesn't work, or launch.sh isn't in the Fieldrunners directory."
	exit 1
fi
[[ -f "$thisdir/frfix.so" ]] || \
	echo "Running without frfix.so!  Expect problems.  Run ./build.sh to create frfix.so."
# If PulseAudio is installed, kill PulseAudio and load a Pulse-less alsa.conf
if which pulseaudio >/dev/null 2>&1
then	export ALSA_CONFIG_PATH=$thisdir/alsa.conf
	if  pulseaudio --check
	then	do_pulse=true
		pulseaudio --kill || \
			echo "Couldn't kill PulseAudio.  Fieldrunners will probably crash."
	fi
fi
if [[ -f "$thisdir/Fieldrunners" ]]
then	LD_PRELOAD=$thisdir/frfix.so $thisdir/Fieldrunners
elif [[ -f "$thisdir/fieldrunners" ]]
then	LD_PRELOAD=$thisdir/frfix.so $thisdir/fieldrunners
else	echo "Unable to find Fieldrunners executable!"
	exit 4
fi
if [[ "$do_pulse" == "true" ]]
then	unset ALSA_CONFIG_PATH
	pulseaudio --start || \
		echo "Unable to restart PulseAudio."
fi
