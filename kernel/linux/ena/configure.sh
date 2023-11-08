#!/bin/sh

# This file is used to configure the ENA driver build

# Global arguments
builddir=
njobs=
scriptdir=
kerneldir=
config_file=
compiler=
cc=
cc_flags=""
verbose=""
log_results=""

# The files needed for running feature detection are created first.
# This variable holds the directories in which test snippets should be compiled.
try_compile_dirs_queue=""

# List with the same content as try_compile_dirs_queue but for configuration
# which depend on others
try_compile_dirs_dependant=""

# "Queue" of currently running tasks
compile_queue_pids=""
compile_queue_dirs=""

prog_name=`basename $0`
def_result_file_relative="config/ecc_def_results.txt"

usage() {
	echo -e "$prog_name - configure ENA driver build."
	echo -e "Usage:"
	echo -e "$prog_name [options]"
	echo
	echo -e "Options:"
	echo -e "-h, --help              Print help message and exit."
	echo -e "-j, --jobs              Number of parallel jobs to execute."
	echo -e "-k, --kerneldir         Path to kernel headers against which the driver should compile."
	echo -e "-v, --verbose           Print more information on what the script does"
	echo -e "-l, --log-results       Log the compilation check results in ${def_result_file_relative}"
}

# Print all passed arguments via stdin if VERBOSE variable is set.
# Returns immediately otherwise
log_if_verbose() {
    if [ -z ${verbose} ]; then
        return
    fi

    while read line; do
        echo ${line}
    done
}

# Function parses the shell arguments
#
# This function should be called at the beginning of the script. All of the
# arguments passed to the script should be passed to this function.
parse_args() {
	while [ "$#" -gt 0 ]; do
		case "$1" in
		-j | --jobs)
			shift
			njobs=$1
			;;
		-k | --kernel-dir)
			shift
			kerneldir=$1
			;;
		-h | --help)
			usage
			exit 0
			;;
		-v | --verbose)
			verbose="true"
			;;
		-l | --log-results)
			log_results="true"
			;;
		*)
			echo "Unknown argument: $1" 1>&2
			usage
			exit 1
		esac
		shift
	done
}

# Prepare global variables
prepare_variables() {
	if [ -z "$njobs" ]; then
		njobs=`get_ncpu`
	fi

	if [ -z "$kerneldir" ]; then
		kerneldir="/lib/modules/`uname -r`/build"
	fi

	if [ ! -d "$kerneldir" ]; then
		echo "kernel headers directory ${kerneldir} isn't found. Are they installed?"
		exit 1
	fi

	case $njobs in
	''|*[0-9]*)
		;;
	*)
		echo "Number of jobs must be an integer." 1>&2
		exit 1
		;;
	esac
}

# Get random string consist of current epoch time concatented with random generated number (0-32767)
get_random_string() {
	NOW=`date +%s%3N`
	echo ${NOW}_${RANDOM}
}

# Get number of CPUs available
get_ncpu() {
	num_proc=`grep -c processor /proc/cpuinfo`
	echo $num_proc
}

# Prepare environment for compiling test snippet
#
# Arguments:
# `prologue` - test snippet prologue (global scope)
# `body` - body of main function
# `success_def` - what should be defined in config.h when compilation is
#                 successful
#
# Returns:
# Directory with compilation env
prepare_compilation_env() {
	prologue=$1
	body=$2
	success_def=$3

	# create temporary directory
	tmp_string=`get_random_string`
	tmp_dir="tmp_${success_def}_${tmp_string}"
	tmp_path="$builddir/$tmp_dir"
	mkdir -p "$tmp_path"

	# Write the file contents.
	source_file="$tmp_path/main.c"
	touch ${source_file}
	if [ ! -f $tmp_path/main.c ]; then
		echo "Failed to create test file ${tmp_path}" >&2
		echo "Existing" >&2
		exit 1
	fi

	printf -- "#include <linux/module.h>\n#include <linux/kernel.h>\n#include <linux/version.h>\n" >>${source_file}
	printf -- "#define ENA_REQUIRE_MIN_VERSION(A, B, C) LINUX_VERSION_CODE < KERNEL_VERSION(A, B, C)\n" >>${source_file}
	printf -- "MODULE_LICENSE(\"Dual BSD/GPL\");\n" >>${source_file}
	printf -- "$prologue\n" >>${source_file}
	printf -- "int\nmain(void)\n{" >>${source_file}
	printf -- "$body\n" >>${source_file}
	printf -- ";\nreturn 0;\n}\n" >>${source_file}

	printf -- "SUCCESSDEF=$success_def\n" \
		> "$tmp_path/info.cfg"

	echo "$tmp_dir"
}

# Push directory and PID to compile queue
#
# Arguments:
# `cur_dir` - directory where the compilation task is executing
# `cur_pid` - pid of the compile task
compile_queue_push() {
	cur_dir=$1
	cur_pid=$2

	compile_queue_pids="$compile_queue_pids $cur_pid"
	compile_queue_dirs="$compile_queue_dirs $cur_dir"
}

# Pop next directory and PID from compile queue
#
# WARNING:
# This function doesn't work when used from subshell.
compile_queue_pop() {
	num_elements=`echo "$compile_queue_pids" | wc -w`

	if [ "$num_elements" -le "1" ]; then
		compile_queue_pids=""
		compile_queue_dirs=""
	else
		# Remove element, unfortunately doesn't work when there is only one element
		compile_queue_pids=`echo "$compile_queue_pids" | awk '{for (i=2; i < NF; i++) printf $i " "; print $NF}'`
		compile_queue_dirs=`echo "$compile_queue_dirs" | awk '{for (i=2; i < NF; i++) printf $i " "; print $NF}'`
	fi
}

# Pop next directory from try_compile_dirs_queue
#
# WARNING:
# This function doesn't work when used from subshell.
try_compile_pop() {
	num_elements=`echo "$try_compile_dirs_queue" | wc -w`

	ret=`echo "$try_compile_dirs_queue" | awk '{ print $1 }'`

	# we dequeued the last element
	if [ "$num_elements" -eq "1" ]; then
		try_compile_dirs_queue=""
	else
		try_compile_dirs_queue=`echo "$try_compile_dirs_queue" | awk '{for (i=2; i < NF; i++) printf $i " "; print $NF}'`
	fi
}

# Wait for the next process to end
#
# Returns:
# The directory of the next process to wait for
compile_queue_wait_next_pid() {
	next_pid=`echo "$compile_queue_pids" | awk '{print $1}'`
	next_dir=`echo "$compile_queue_dirs" | awk '{print $1}'`

	wait "$next_pid"

	ret="${next_dir}"
}

# Log the result @def with the result @res into @def_result_file_relative.
# If the definition is an empty string or if result logging wasn't requested
# then do nothing
log_test_result() {
	def=$1
	res=$2

	if [ -z ${log_results} ]; then
		return
	fi

	if [ -z $def ]; then
		return
	fi

	echo ${def} ${res} >> ${scriptdir}/${def_result_file_relative}
}

# Finish the next task in queue
finish_next_task() {
	compile_queue_wait_next_pid
	directory="${ret}"
	compile_queue_pop

	# Gather all the interesting results from the info.cfg file
	success_def=`awk -F "=" '/SUCCESSDEF/ {print $2}' $builddir/$directory/info.cfg`
	retcode=`awk -F "=" '/RETCODE/ {print $2}' $builddir/$directory/info.cfg`

	if [ "$retcode" -eq "0" ]; then
		log_test_result "${success_def}" 1

		echo "$success_def: YES" | log_if_verbose
		echo "#define $success_def 1" >> $config_file
	else
		log_test_result "${success_def}" 0

		echo "$success_def: NO" | log_if_verbose
	fi

	rm -rf $builddir/$directory

	# Check if there are any configurations that depend on this one
	# if found, run it one by one
	tmp=""
	dependant_config_dirs=""
	for dir_config_pair in $try_compile_dirs_dependant ; do
		found_config=$(echo ${dir_config_pair} | \
			( IFS=":" read dir config ; echo -n ${config}))

		if [ $found_config = $success_def ]; then
			dependant_config_dir=$(echo ${dir_config_pair} | \
				( IFS=":" read dir config ; echo ${dir}))
			dependant_config_dirs="${dependant_config_dirs} ${dependant_config_dir}"
		else
			tmp="${tmp} ${dir_config_pair}"
		fi
	done

	# put the rest of the unhandled dependencies back
	try_compile_dirs_dependant="$tmp"
	if [ -n "${dependant_config_dirs}" ]; then
		# If the compilation of the previous check succeeded then make sure that
		# the compilation that depends on it would have this value defined
		if [ "$retcode" -eq "0" ]; then
			cc_flags="${cc_flags} -D${success_def}"
		fi

		try_compile_dirs_queue="${try_compile_dirs_queue} ${dependant_config_dirs}"
	fi
}

# Try to compile the feature
#
# Arguments
# `$1` - directory where main.c is located
compile_test_feature() {
	cd $kerneldir
	$cc $cc_flags -c -o "$1/main.o" "$1/main.c" 2> "$1/stderr.log"
	echo "RETCODE=$?" >> "$1/info.cfg"
	cd -
}

# Fork the test process
#
# Fork the test process and print PID.
#
# Arguments:
# `$1` - directory where the test should be executed
fork_cc_process() {
	compile_test_feature $1 2> /dev/null 1> /dev/null < /dev/null &
	ret=$!
}

# Try to compile the file in directory
#
# This function adds the try_compile to queue, which then can be run
# using try_compile_run function
#
# Arguments:
# `prologue` - test snippet prologue (global scope)
# `body` - body of main function
# `success_def` - what will be defined in config.h when compilation succeeds
# `dependant_config` - another configuration that the current one depends on
try_compile_async() {
	dir=`prepare_compilation_env "$1" "$2" "$3"`

	# this configuration depends on another one
	if [ -n "${4}" ]; then
		try_compile_dirs_dependant="$try_compile_dirs_dependant $dir:${4}"
	else
		try_compile_dirs_queue="$try_compile_dirs_queue $dir"
	fi
}

# Rate limit number of parallel processes
rate_limit_processes() {
	num_proc=`echo "$compile_queue_pids" | wc -w`
	if [ "$num_proc" -ge "$njobs" ]; then
		finish_next_task
	fi
}

# Run the try_compile tasks
try_compile_run() {
	while true; do
		while [ -n "${try_compile_dirs_queue}" ]; do
			rate_limit_processes

			try_compile_pop
			test_dir=$ret

			fork_cc_process "$builddir/$test_dir"
			newpid=$ret
			compile_queue_push $test_dir $newpid
		done

		# Finalize the configuration, waits for the unfinished tasks and finish
		# the config.h file
		pid_count=`echo "$compile_queue_pids" | wc -w`
		if [ $pid_count -eq 0 ]; then
			break
		fi

		finish_next_task
		pid_count=`echo "$compile_queue_pids" | wc -w`
	done

	printf -- "#endif /* _ENA_CONFIG_H_ */\n" >> $config_file
}

# Get directory where the script is located
#
# Function returns the absolute path of the directory where the script
# is located.
# Arguments:
# `script` - file name of the script
get_script_dir() {
	script="$1"

	script_path=`readlink -f "$script"`
	script_dir=`dirname "$script_path"`
	echo $script_dir
}

# Prepare build directory
prepare_build_dir() {
	builddir="$scriptdir"
	# We need to know the absolute path to build directory, otherwise
	# the Kbuild will not work
	builddir=`readlink -f "$builddir"`
	config_file="$builddir/config.h"

	# Start preparing config file
	printf "#ifndef _ENA_CONFIG_H_\n#define _ENA_CONFIG_H_\n" > $config_file
}


# Intercept CC arguments
#
# Function tries to find out what compiler is being used by the kernel and
# what arguments are passed to it. The information about the compiler is
# obtained by parsing the HOSTCC variable in Kbuild. The arguments are
# obtained by passing special intercept_cc script as CC variable to Kbuild.
intercept_cc_args() {
	echo "Trying to intercept CC arguments"

	mkdir -p "$builddir/cc_test"

	# Prepare "module" file
	printf "#include <linux/module.h>\n#include <linux/kernel.h>\n" > $builddir/cc_test/main.c
	printf "MODULE_LICENSE(\"Dual BSD/GPL\");\n" >> $builddir/cc_test/main.c
	printf "int main(void){return 0;}\n" >> $builddir/cc_test/main.c

	# Prepare Kbuild file
	printf "obj-m=main.o\n" > $builddir/cc_test/Kbuild

	# Let's detect the compiler first
	cd $builddir/cc_test
	curdir=`pwd`
	cc=`make -C $kerneldir M=$curdir -pn 2> /dev/null | grep "^HOSTCC" | grep -v '\\$' | tail -1 | awk -F "=" '{print $2}'`

	echo "Detected compiler: $cc"

	# Run the test.
	# We need to export CC_ARGS_FILE, so that intercept_cc.sh knows where
	# to write the arguments. INTERCEPTED_CC informs intercept_cc.sh
	# about the actual compiler that should be used, as it has no way of
	# knowing it.
	export CC_ARGS_FILE="$builddir/cc_test/cc_args.txt"
	export INTERCEPTED_CC="$cc"
	make -C $kerneldir M=`pwd` CC="$scriptdir/config/intercept_cc.sh" > /dev/null 2>&1

	# Just to be sure
	unset CC_ARGS_FILE
	unset INTERCEPTED_CC
	cd $OLDPWD

	# If there is no cc_args.txt file, then we can be fairly certain, that
	# the Makefile wasn't executed
	if [ ! -f "$builddir/cc_test/cc_args.txt" ]; then
		echo "Failed to intercept CC arguments. Kernel headers missing?"
		echo "Exiting"
		exit 1
	fi

	cc_version=`${cc} --version | grep -m 1 -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -n1 | awk -F'.' '{print $1}'`
	# Get intercepted CC flags and append the ones needed for feature detection
	cc_flags=`cat "$builddir/cc_test/cc_args.txt"`
	# -fno-pie -Werror=incompatible-pointer-types -Wno-nonnull -Werror=implicit seems to be the best combination for
	# feature detection, older compilers don't have some of these flags, use the ones supported and add -Werror to
	# compensate for the rest
	if [ $cc_version -gt 4 ]; then
		cc_flags="$cc_flags -fno-pie -Werror=incompatible-pointer-types -Wno-nonnull -Werror=implicit"
	else
		cc_flags="$cc_flags -Werror -Wno-nonnull -Werror=implicit"
	fi

	# On newer Linux versions, stdarg and other compiler-dependent headers are
	# provided in include/linux. On older kernels we should include compiler
	# specific header ourselves
	if [ ! -f "$kerneldir/include/linux/stdarg.h" ]; then
		system_dir=`$cc -print-file-name=include`
		cc_flags="$cc_flags -isystem $system_dir"
	fi

	echo "Managed to intercept CC arguments"
	echo "Command: ${cc} ${cc_flags}" | log_if_verbose
}

# Main function of script.
#
# All of the script arguments should be passed here.
main() {
	parse_args "$@"
	scriptdir=`get_script_dir "$0"`
	rm -f ${scriptdir}/${def_result_file_relative}

	prepare_variables
	prepare_build_dir
	intercept_cc_args
	# create all compile tests
	. $scriptdir/config/test_defs.sh
	echo '### Detection of the available kernel API ###'
	try_compile_run
	echo '### Detection completed ###'
}

main "$@"
