#!/bin/sh

# This script is used to intercept the arguments passed to the compiler.

# If we're trying to compile main.c then we know that Kconfig isn't trying to
# discover which features are supported by the compiler.
echo "$@" | grep "main\.c"
if [ "$?" -ne 0 ]; then
	# If there is no main.c, we only act as a wrapper around compiler.
	# Kconfig should detect supported compiler flags by itself.
	$INTERCEPTED_CC $@
	exit $?
fi

rm -f $CC_ARGS_FILE

# Remove unnecessary arguments.
# We don't want to use all of the warning flags enabled by the Kconfig, as
# we're doing some shortcuts in feature detection. We pass our own set of
# warning flags instead.
while [ "$#" -gt 0 ]; do
	firstchars=`printf -- "%.2s" "$1"`
	if [ "$firstchars" = "-W" ]; then
		:
	elif [ "$firstchars" = "-c" ]; then
		:
	elif [ "$firstchars" = "-o" ]; then
		# make sure the binary name is not parsed as well
		shift
	elif [ $(printf $1 | tail -c 6) = "main.c" ]; then
		:
	else
		printf -- "$1 " >>$CC_ARGS_FILE
	fi
	shift
done
