#!/bin/bash
thisdir="$(dirname "$(readlink -f "$0")")"
[[ -f "$thisdir/frfix.so" ]] || \
	echo "Running without frfix.so!  Expect problems.  Run ./build.sh to create frfix.so."
LD_PRELOAD=$thisdir/frfix.so $thisdir/[Ff]ieldrunners
