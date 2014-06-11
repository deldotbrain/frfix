#!/bin/bash
# launch.sh: launch Fieldrunners with frfix loaded
# Copyright (C) 2012-2014, Ryan Pennucci <decimalman@gmail.com>

thisdir="$(dirname "$(readlink -f "$0")")"
[[ -f "$thisdir/frfix.so" ]] || \
	echo "Running without frfix.so!  Expect problems.  Run ./build.sh to create frfix.so."
LD_PRELOAD=$thisdir/frfix.so $thisdir/[Ff]ieldrunners
