# PCN Test Infrastructure

# This file provides the generic infrastructure to automate
# heterogenous migrations. The first line of a specialized wrapper
# shall be:
#
# #!/usr/bin/env bash
# . pcn_test.sh
#
# After that, the wrapper script may populate the following variables.
#
# At RASEC, server={test-arm,test-x86}, debug="RIO_DEBUG={0,1,2,3}".

# Placeholders for variables defined in individualized shell scripts.
#target=
#bin=
#debug=
#verbose=

server=
target=
target_bin=
dir=
task=
args=

error()
{
    echo Error: $1
    exit -1
}

show_help()
{
    echo "Usage $0: -b <binary> [...]"
    echo "  -b: binary executable to execute"
    echo "  -s: server target to run code on; defaults to localhost"
    echo "  -t: target to run code on {x86,arm}; defaults to localhost machine type"
    echo "    -a: alias for -t arm"
    echo "    -x: alias for -t x86"
    echo "  -l: launch initial command"
    echo "    --la: launch executable on arm node; defaults to ssh:test-arm, or SERVER if specified"
    echo "    --lx: launch executable on x86 node; defaults to ssh:test-x86, or SERVER if specified"
    echo "  -d: dump checkpoint"
    echo "    --da: dumps checkpoint for arm node; defaults to ssh:test-arm, or SERVER if specified"
    echo "    --dx: dumps checkpoint for x86 node; defaults to ssh:test-x86, or SERVER if specified"
    echo "  -r: restore checkpoint"
    echo "    --ra: restore checkpoint for arm node; defaults to ssh:test-arm, or SERVER if specified"
    echo "    --rx: restore checkpoint for x86 node; defaults to ssh:test-x86, or SERVER if specified"
    echo "  -k: terminate all instances of binary"
    echo "  -K: terminate any RIO server"
    echo "  -v: show verbose diagnostics"
    echo "  -z: list of arguments to be passed to node. Place arguments in a string"
    echo "  -h: show arguments"
    echo "  --debug [int]: enable RIO_DEBUG, defaults to RIO_DEBUG=1, supports {1,2,3}"
    echo -e "\nThe following flags are incompatible with one another"
    echo "  -x / -a"
    echo "  -l / -d / -r"
}

set_target()
{
    if [[ ! -z "${target}" ]]; then
	error "Target already set to ${target}"
    fi

    target="$1"
}

set_task()
{
    if [[ ! -z "${task}" ]]; then
	error "Task already set to ${task}"
    fi

    task="$1"
}

set_debug()
{
    if [[ -n "${debug}" ]]; then
	error "Debug already set to ${debug}"
    fi

    if [[ -z "$1" ]]; then
	debug="RIO_DEBUG=1"
    else
	debug="RIO_DEBUG=$1"
    fi
}

set_args()
{
    if [[ ! -z "${args}" ]]; then
	error "Runtime arguments set to \"${args}\""
    fi

    args="$1"
}

process_vars()
{
    #pid=$(ssh ${target} -C "ps -e | grep \" $exe\" | grep -v RIO" | awk '{ print $1 }')

    # Set the defaults
    target="${target=localhost}"
    server="${server=localhost}"

    bin="$(realpath $bin)"
    exe="$(basename $bin)"
    dir="$(dirname $bin)"

    if [[ -z "${dir}" ]]; then
	dir="$(/bin/pwd)"
    fi

    case "${target}" in
	arm)
	    target_bin="${bin}_aarch64"
	    target="aarch64"
	    ;;
	x86)
	    target_bin="${bin}_x86-64"
	    target="x86-64"
	    ;;
	localhost)
	    host="$(uname -m)"
	    host="${host/x86_64/x86-64}"
	    target_bin="${bin}_${host}"
	    ;;
	*)
	    error "Invalid target"
    esac

    if [[ -n "${verbose}" ]]; then
	echo target = $target
	echo server = $server
	echo bin = $bin
	echo target_bin = $target_bin
	echo dir = $dir
	echo debug = $debug
    fi
}

run_cmd()
{
    cmd=$1

    if [[ "${server}" == "localhost" ]]; then
	bash -c "${cmd}"
    else
	ssh root@${server} -t -C "bash -c \"${cmd}\""
    fi
}

do_launch()
{
    rm -f ${bin}
    cp ${target_bin} ${bin}; \

    cmd="cd ${dir}; \
	 export DISPLAY=${DISPLAY}; \
	 ${debug} ${bin} ${args}"

    run_cmd "$cmd"
}

getpid()
{
    pid=$(ssh root@${server} -C "ps -e | grep \" $exe\" | grep -v RIO" | awk '{ print $1 }')

    echo $pid
}

do_dump()
{
    pid=$(getpid)

    cmd="cd ${dir}; \
	 rm -rf t; mkdir t; \
	 . /scratch/pjr/setpath; \
	 criu-het dump --arch ${target} -j --tcp-established -D t -t ${pid} --ghost-limit 50M; \
	 echo $?"

    run_cmd "$cmd"
}

do_restore()
{
    cmd="cd ${dir}; \
	 . /scratch/pjr/setpath; \
	 rm -f ${bin}; \
	 cp ${target_bin} ${bin}; \
	 criu restore -j --tcp-established -D t/${target}; \
	 echo $?"

    run_cmd "$cmd"
}

do_kill_strong()
{
    for SERVER in $(echo "${server}")
    do
	# extract PID of xcalc app on X86
	pid=$(ssh root@${SERVER} -C "ps -e | grep $exe" | awk '{ print $1 }')

	# copy binary to remote server
	if [[ ! -z "${pid}" ]]; then
	    ssh root@${SERVER} -C "kill -9 ${pid}"
	fi

	# extract PID of xcalc RIO server
	pid=$(ssh root@${SERVER} -C "ps -e | grep RIO" | awk '{ print $1 }')

	# copy binary to remote server
	if [[ ! -z "${pid}" ]]; then
	    ssh root@${SERVER} -C "kill -9 ${pid}"
	fi
    done
}

do_kill()
{
    for SERVER in $(echo "${server}")
    do
	# extract PID of xcalc app on X86
	pid=$(ssh root@${SERVER} -C "ps -e | grep $exe" | awk '{ print $1 }')

	if [[ -z "${pid}" ]]; then
	    exit
	fi

	ssh root@${SERVER} -C "kill -9 ${pid}"

	# extract PID of xcalc RIO server
	pid=$(ssh root@${SERVER} -C "ps -e | grep ${pid}" | awk '{ print $1 }')

	# copy binary to remote server
	if [[ ! -z "${pid}" ]]; then
	    ssh root@${SERVER} -C "kill -9 ${pid}"
	fi
    done
}

while getopts ":b:s:t:axldrkKvhz:-:" OPT; do
    if [ "$OPT" = "-" ]; then
	OPT="${OPTARG%%=*}"
	OPTARG="${OPTARG#$OPT}"
	OPTARG="${OPTARG#=}"
    fi

    case "${OPT}" in
	b)
	    bin=${OPTARG}
	    ;;
	s)
	    server=${OPTARG}
	    ;;

	t)
	    set_target "${OPTARG}"
	    ;;
	a)
	    set_target "arm"
	    ;;
	x)
	    set_target "x86"
	    ;;

	l)
	    set_task "launch"
	    ;;
	la)
	    set_target "arm"
	    server="${server:=test-arm}"
	    set_task "launch"
	    ;;
	lx)
	    set_target "x86"
	    server="${server:=test-x86}"
	    set_task "launch"
	    ;;

	d)
	    set_task "dump"
	    ;;
	da)
	    set_target "arm"
	    server="${server:=test-x86}"
	    set_task "dump"
	    ;;
	dx)
	    set_target "x86"
	    server="${server:=test-arm}"
	    set_task "dump"
	    ;;

	debug)
	    set_debug "${OPTARG=1}"
	    ;;

	r)
	    set_task "restore"
	    ;;
	ra)
	    set_target "arm"
	    server="${server:=test-arm}"
	    set_task "restore"
	    echo "server = ${server}"
	    ;;
	rx)
	    set_target "x86"
	    server="${server:=test-x86}"
	    set_task "restore"
	    ;;

	k)
	    set_task "terminate"
	    server="${server:=test-arm test-x86}"
	    target="localhost"
	    ;;

	K)
	    set_task "terminate-strong"
	    server="${server:=test-arm test-x86}"
	    target="localhost"
	    ;;

	z)
	    set_args "${OPTARG}"
	    ;;

	v)
	    verbose="1"
	    ;;
	h)
	    show_help
	    ;;

	*)
	    show_help
	    error "Invalid arguments"
	    ;;
    esac
done

process_vars

# Perform the task
case "${task}" in
    launch)
	do_launch
	;;
    dump)
	do_dump
	;;
    restore)
	do_restore
	;;
    terminate)
	do_kill
	;;
    terminate-strong)
	do_kill_strong
	;;
    *)
	error "Invalid task $task"
	;;
esac
