#!/bin/bash
trap 'print_summary && echo "test-framework exiting on error"' ERR
set -e

debug_print()
{
	if [ "$DEBUG" = "yes" ]; then
		echo -e -n "$@"
	fi
}

error_print()
{
	echo -e -n "$@"
}

print_summary()
{
	[ -n "$ONLY" ] && echo "WARNING: ONLY is set to ${ONLY}."
	local form="%-13s %-17s %s\n"
	printf "$form" "status" "script" "skipped tests E(xcluded) S(low)"
	echo "------------------------------------------------------------------------------------"
	for O in $TESTSUITE_LIST; do
		local skipped=""
		local slow=""
		local o=$(echo $O | tr "[:upper:]" "[:lower:]")
		o=${o//_/-}
        #o=${o//tyn/tyN}
        local log=${TMP}/${o}.log
        [ -f $log ] && skipped=$(grep excluded $log | awk '{ printf " %s", $3 }' | sed 's/test_//g')
        [ -f $log ] && slow=$(grep SLOW $log | awk '{ printf " %s", $3 }' | sed 's/test_//g')
		[ "${!O}" = "done" ] && \
		printf "$form" "Done" "$O" "E=$skipped" && \
		[ -n "$slow" ] && printf "$form" "-" "-" "S=$slow"

    done

    for O in $TESTSUITE_LIST; do
        [ "${!O}" = "no" ] && \
            printf "$form" "Skipped" "$O" ""
    done

    for O in $TESTSUITE_LIST; do
        [ "${!O}" = "done" -o "${!O}" = "no" ] || \
            printf "$form" "UNFINISHED" "$O" ""
    done
	return
}

filesystem_free()
{
	local MOUNT_POINT=$1
	local FREE_BLOCK=$(df $MOUNT_POINT -P | grep $MOUNT_POINT | awk '{print $4}')
	expr $FREE_BLOCK \* 1024
}

filesystem_is_mounted()
{
	local FS_TYPE="$1"
	local MOUNT_POINT="$2"
	/bin/grep -w $MOUNT_POINT /proc/mounts | awk '{ print $3 }' | grep -w -q $FS_TYPE
}

mount_filesystem_noexit()
{
	local MOUNT_CMD=$1
	local FS_TYPE=$2
	local DEV=$3
	local MOUNT_POINT=$4
	local OPTION=$5

	if [ "$1" = "" -o "$2" = "" -o \
	     "$3" = "" -o "$4" = "" -o \
	     "$5" = "" ];then
		error "argument error"
	fi
	debug_print "mounting $FS_TYPE to $MOUNT_POINT..."
	if ! filesystem_is_mounted $FS_TYPE $MOUNT_POINT; then
		$MOUNT_CMD $DEV $MOUNT_POINT $OPTION

		if ! filesystem_is_mounted $FS_TYPE $MOUNT_POINT; then
			debug_print "failed\n"
			error_print "failed to mount $FS_TYPE to $MOUNT_POINT"
			error_print "CMD: $MOUNT_CMD $DEV $MOUNT_POINT $OPTION, fs_type is $FS_TYPE"
			/bin/grep -w $MOUNT_POINT /proc/mounts | awk '{ print $3 }' | grep $FS_TYPE
			return 1
		else
			debug_print "succeed\n"
		fi
	else
		 debug_print "already done\n"
    fi
	return 0
}

mount_filesystem()
{
	mount_filesystem_noexit "$1" "$2" "$3" "$4" "$5"
	local RET=$?
	if [ "$RET" != 0 ]; then
		exit 1
	fi
	return 0
}

mount_lowerfs()
{
	local INDEX;
	local LOWERFS_NAME;
	local LOWERFS_MNT;
	local LOWERFS_MOUNT_CMD;
	local LOWERFS_DEV;
	
	for INDEX in ${!LOWERFS_NAME_ARRAY[@]}; do
		LOWERFS_NAME=${LOWERFS_NAME_ARRAY[$INDEX]}
		LOWERFS_MNT=${LOWERFS_MNT_ARRAY[$INDEX]}
		LOWERFS_MOUNT_CMD=${LOWERFS_MOUNT_CMD_ARRAY[$INDEX]}
		LOWERFS_DEV=${LOWERFS_DEV_ARRAY[$INDEX]}
		LOWERFS_OPTION=${LOWERFS_OPTION_ARRAY[$INDEX]}
		mount_filesystem "$LOWERFS_MOUNT_CMD" $LOWERFS_NAME $LOWERFS_DEV $LOWERFS_MNT "$LOWERFS_OPTION"
	done
}

umount_filesystem_noexit()
{
	local FS_TYPE=$1
	local MOUNT_POINT=$2


	debug_print "umounting $MOUNT_POINT..."

	if [ "$FS_TYPE" = "" ]; then
		error_print "failed to umount $MOUNT_POINT, BAD fstype\n"
		return 1;
	fi

	if [ "$MOUNT_POINT" = "" ]; then
		error_print "failed to umount $MOUNT_POINT, BAD mount point\n"
		return 1;
	fi

	if filesystem_is_mounted $FS_TYPE $MOUNT_POINT; then
		# Sync before umount, nfs may fail if not.
		sync
		/bin/umount $MOUNT_POINT

		if filesystem_is_mounted $FS_TYPE $MOUNT_POINT; then
			error_print "unable to umount $MOUNT_POINT\n"
			error_print "CMD: /bin/umount $MOUNT_POINT\n"
			return 1
		else
			debug_print "succeed\n"
		fi
	else
		 debug_print "already done\n"
	fi
	return 0
}

umount_filesystem()
{
	umount_filesystem_noexit $1 $2
	local RET=$?
	
	if [ "$RET" != 0 ]; then
		exit 1
	fi
	return 0
}

umount_lowerfs()
{
	local INDEX;
	local LOWERFS_NAME;
	local LOWERFS_MNT;
	for INDEX in ${!LOWERFS_NAME_ARRAY[@]}; do
		LOWERFS_NAME=${LOWERFS_NAME_ARRAY[$INDEX]}
		LOWERFS_MNT=${LOWERFS_MNT_ARRAY[$INDEX]}
		umount_filesystem $LOWERFS_NAME $LOWERFS_MNT
	done
}

remount_lowerfs()
{
	umount_lowerfs
	mount_lowerfs
	return
}

module_is_inserted()
{
	local MODULE=$1
	local inserted=$(/sbin/lsmod | awk '{ print $1}')
	if $(echo $inserted | grep -w -q "$MODULE"); then
		return 0
	else
		return 1
	fi
	return
}

remove_module()
{
	local MODULE=$1

	debug_print "removing module $MODULE..."
	if module_is_inserted $MODULE; then
		/sbin/rmmod $MODULE
		
		if module_is_inserted $MODULE; then		
			debug_print "failed\n"
			error_print "failed to remove module $MODULE\n"
			exit 1
		else
			debug_print "succeed\n"
		fi
	else
		debug_print "already done\n"
	fi
	return
}

insert_module()
{
	local MODULE=$1
	local MODULE_PATH=$2
	
	debug_print "inserting module $MODULE..."
	if ! module_is_inserted $MODULE; then
		/sbin/insmod $MODULE_PATH
	
		if ! $(module_is_inserted $MODULE); then		
			debug_print "failed\n"
			error_print "failed to remove module $MODULE_PATH"
			exit 1
		else
			debug_print "succeed\n"
		fi
	else
		debug_print "already done\n"
	fi
	return
}

reinsert_module()
{
	local MODULE=$1
	local MODULE_PLACE=$2
	remove_module $MODULE
	insert_module $MODULE $MODULE_PLACE
	return
}

module_is_installed()
{
	MODULE=$1
	MODINFO=$(modinfo $MODULE 2>&1 | grep -w "could not find")
	if [ "${MODINFO}" = "" ]; then
		return 0
	fi
	return 1
}

mount_mtfs()
{
	mount_filesystem $MOUNT_MTFS mtfs $MTFS_DEV $MTFS_MNT1 "$MTFS_OPTION"
}

mount_mtfs_noexit()
{
	mount_filesystem_noexit $MOUNT_MTFS mtfs $MTFS_DEV $MTFS_MNT1 "$MTFS_OPTION"
	return $?
}

mount_mtfs2()
{
	mount_filesystem $MOUNT_MTFS mtfs $MTFS2_DEV $MTFS_MNT2 "$MTFS_OPTION"
}

umount_mtfs()
{
	umount_filesystem mtfs $MTFS_MNT1
	return $?
}

umount_mtfs2()
{
	umount_filesystem mtfs $MTFS_MNT2
	return $?
}

remount_mtfs()
{
	umount_mtfs
	if [ "$DOING_MUTLTI" = "yes" ]; then
		umount_mtfs2
	fi
	
	mount_mtfs
	if [ "$DOING_MUTLTI" = "yes" ]; then
		mount_mtfs2
	fi
	return
}

init_leak_log()
{
	echo "malloc" > /proc/sys/mtfs/debug
	echo > $MTFS_LOG
	../utils/mtfsctl debug_kernel > /dev/null
}

init_log()
{
	echo "error" > /proc/sys/mtfs/debug
	echo > $MTFS_LOG
	../utils/mtfsctl debug_kernel > /dev/null
}

check_leak_log()
{
	if module_is_inserted $DEBUG_MODULE; then
		../utils/mtfsctl debug_kernel > $MTFS_LOG
		$LEAK_FINDER $MTFS_LOG 2>&1 | egrep '*** Leak:' && error "memory leak detected, see log $MTFS_LOG" || true

		local PROC_FILE="/proc/sys/mtfs/memused"
		if [ -f $PROC_FILE ]; then
			local MEM_USED=$(cat $PROC_FILE)
			if [ "$MEM_USED" != "0" ]; then
				error "memory leaked $MEM_USED, see $PROC_FILE"
			fi
		else
			error "$PROC_FILE not found"
		fi
	fi
}

leak_detect_state_push()
{
	# For those tests with too many operations, in which case log will overflow
	# Save old state, change to new state
	local ENABLE=$1
	export FORMER_DETECT_LEAK="$DETECT_LEAK"
	DETECT_LEAK=$ENABLE
}

leak_detect_state_pop()
{
	# Recover back to old state
	if module_is_inserted mtfs; then
		../utils/mtfsctl debug_kernel > /dev/null
	fi
	DETECT_LEAK="$FORMER_DETECT_LEAK"
}

mtfs_hashname()
{
	local DEV="$1"
	cat /proc/fs/mtfs/device_list | grep -w $DEV | awk '{print $2}'
}

abandon_branch()
{
	local DEV="$1"
	local BRANCH="$2"

	local HASH_NAME=$(mtfs_hashname $DEV)
	if [ "$HASH_NAME" = "" ]; then
		echo "failed to abandon branch[$BRANCH] of $DEV: not found"
		return
	fi

	local PROC_EMASK="/proc/fs/mtfs/devices/"$HASH_NAME"/branch"$BRANCH"/bops_emask"
	echo "write_ops read_ops" > $PROC_EMASK
	echo "$BRANCH abandoned"
}

abandon_branches()
{
	if [ "$ABANDON_BINDEX" != "-1" ]; then
		abandon_branch $MTFS_DEV $ABANDON_BINDEX
		if [ "$DOING_MUTLTI" = "yes" ]; then
			abandon_branch $MTFS2_DEV $ABANDON_BINDEX
		fi
	fi
}

undo_abandon_branch()
{
	local DEV="$1"
	local BRANCH="$2"

	local HASH_NAME=$(mtfs_hashname $DEV)
	if [ "$HASH_NAME" = "" ]; then
		echo "failed to undo abandon branch[$BRANCH] of $DEV: not found"
		return
	fi

	local PROC_EMASK="/proc/fs/mtfs/devices/"$HASH_NAME"/branch"$BRANCH"/bops_emask"
	echo "" > $PROC_EMASK
}

undo_abandon_branches()
{
	if [ "$ABANDON_BINDEX" != "-1" ]; then
		undo_abandon_branch $MTFS_DEV $ABANDON_BINDEX
		if [ "$DOING_MUTLTI" = "yes" ]; then
			undo_abandon_branch $MTFS2_DEV $ABANDON_BINDEX
		fi
	fi
}

lowerfs_insert_module()
{
	local INDEX;
	local LOWERFS_MODULE;
	local LOWERFS_MODULE_PATH;
	for INDEX in ${!LOWERFS_MODULE_ARRAY[@]}; do
		LOWERFS_MODULE=${LOWERFS_MODULE_ARRAY[$INDEX]}
		LOWERFS_MODULE_PATH=${LOWERFS_MODULE_PATH_ARRAY[$INDEX]}
		insert_module $LOWERFS_MODULE $LOWERFS_MODULE_PATH
	done
}

lowerfs_remove_module()
{
	local INDEX;
	local LOWERFS_MODULE;
	for INDEX in ${!LOWERFS_MODULE_ARRAY[@]}; do
		LOWERFS_MODULE=${LOWERFS_MODULE_ARRAY[$INDEX]}
		remove_module $LOWERFS_MODULE
	done
}

setup_all()
{
	if [ "$MOUNT_LOWERFS" = "yes" ]; then
		mount_lowerfs
	fi

	insert_module $DEBUG_MODULE $DEBUG_MODULE_PATH
	if [ "$DETECT_LEAK" = "yes" ]; then
		# Since log is inited here
		# memory leaked when inserting module won't be detected
		init_leak_log
	else
		init_log
	fi

	insert_module $MTFS_MODULE $MTFS_MODULE_PATH
	insert_module $SUBJECT_MODULE $SUBJECT_MODULE_PATH
	lowerfs_insert_module
	insert_module $JUNCTION_MODULE $JUNCTION_MODULE_PATH

	mount_mtfs
	if [ "$DOING_MUTLTI" = "yes" ]; then
		if [ "$LOWERFS_HAVE_DEV" = "yes" ]; then
			echo "lowerfs have backup dev, not able to mount mtfs to mnt2"
		else
			mount_mtfs2
		fi
	fi

	abandon_branches
	return
}

cleanup_all()
{
	CHECK_LEAK="$1"

	umount_mtfs

	remove_module $JUNCTION_MODULE
	lowerfs_remove_module
	remove_module $SUBJECT_MODULE
	remove_module $MTFS_MODULE

	if [ "$DETECT_LEAK" = "yes" -a "$CHECK_LEAK" != "skip_leak_check" ]; then
		check_leak_log
	fi

	remove_module $DEBUG_MODULE

	if [ "$MOUNT_LOWERFS" = "yes" ]; then
		umount_lowerfs
	fi

	return
}

cleanup_and_setup()
{
	cleanup_all
	setup_all
	return
}

log ()
{
	echo $*
}

basetest() {
    if [[ $1 = [a-z]* ]]; then
        echo $1
    else
        echo ${1%%[a-z]*}
    fi
}

pass() {
    $TEST_FAILED && echo -n "FAIL " || echo -n "PASS "
    echo $@
}

run_one() {
    testnum=$1
    message=$2
    tfile=f${testnum}
    export tdir=d0.${TESTSUITE}/d${base}

    local SAVE_UMASK=`umask`
    umask 0022

    local BEFORE=`date +%s`
    echo
    log "== test $testnum: $message == `date +%H:%M:%S`"
    #check_mds
    export TESTNAME=test_$testnum
    TEST_FAILED=false
    test_${testnum} || error "test_$testnum failed with $?"
    #check_mds
    cd $SAVE_PWD
    ##reset_fail_loc
	##check_grant ${testnum} || error "check_grant $testnum failed with $?"
    ##check_catastrophe || error "LBUG/LASSERT detected"
    ##ps auxww | grep -v grep | grep -q multiop && error "multiop still running"
    pass "($((`date +%s` - $BEFORE))s)"
    TEST_FAILED=false
    unset TESTNAME
    unset tdir
    umask $SAVE_UMASK
}

check_command()
{
	if [ ! -f $1 ]; then
		error $1 not exist
	fi
	
	if [ ! -x $1 ]; then
		error $1 can not be executed
	fi	
}

remake_dir()
{
	local NEW_DIR="$1"
	if [ -d $NEW_DIR ]; then
		rm -rf $NEW_DIR -fr
	fi

	if [ -d $NEW_DIR ]; then
		error "rm $NEW_DIR failed"
	fi

	mkdir $NEW_DIR -p
	local RET=$?
	if [ $RET -ne 0 ]; then
		error "mkdir $NEW_DIR failed: $RET"
	fi

	if [ ! -d $NEW_DIR ]; then
		error "mkdir $NEW_DIR failed"
	fi
	echo "mkdir succeed"
}

export_mtfs_dev()
{
	local INDEX;
	local BRANCH_DIR;
	local DEV="";
	local FIRST="yes";

	for INDEX in ${!BRANCH_DIR_ARRAY[@]}; do
		BRANCH_DIR=${BRANCH_DIR_ARRAY[$INDEX]}
		if [ "$FIRST" = "yes" ]; then
			DEV=$BRANCH_DIR
			FIRST="no"
		else
			DEV="$DEV:$BRANCH_DIR"
		fi
	done
	if [ "$DEV" = "" ]; then
		error "failed to get device name"
	fi
	export MTFS_DEV=$DEV
}

init_test_env()
{
	export CONFIGS=${CONFIGS:-local}
	. $TESTS_DIR/cfg/$CONFIGS.sh
	export TEST_FAILED=false
	export SAVE_PWD=${SAVE_PWD:-$PWD}
	export TESTSUITE=`basename $0 .sh`
	export TESTS_DIR=${TESTS_DIR:-$(cd $(dirname $0); echo $PWD)}
	export TMP=${TMP:-/tmp}
	export DIR=${DIR:-$MTFS_MNT1/test}
	export ABANDON_BINDEX=${ABANDON_BINDEX:-"-1"}

	export_mtfs_dev

	if [ "$DOING_MUTLTI" = "yes" ]; then
		export DIR1=${DIR1:-$MTFS_MNT1/$DIR_SUB}
		export DIR2=${DIR2:-$MTFS_MNT2/$DIR_SUB}
	fi
	assert_DIR

	#cmds under tests/src
	export CHECKSTAT=${CHECKSTAT:-"$TESTS_DIR/src/checkstat -v"}
	check_command $CHECKSTAT

	export MCREATE=${MCREATE:-$TESTS_DIR/src/mcreate}
	check_command $MCREATE

	export OPENFILE=${OPENFILE:-$TESTS_DIR/src/openfile}
	check_command $OPENFILE

	export MRENAME=${MRENAME:-$TESTS_DIR/src/mrename}
	check_command $MRENAME

	export MULTIOP=${MULTIOP:-$TESTS_DIR/src/multiop}
	check_command $MULTIOP

	export RENAME_MANY=${RENAME_MANY:-$TESTS_DIR/src/rename_many}
	check_command $RENAME_MANY

	export OPENUNLINK=${OPENUNLINK:-$TESTS_DIR/src/openunlink}
	check_command $OPENUNLINK

	export CREATETEST=${CREATETEST:-$TESTS_DIR/src/createtest}
	check_command $CREATETEST

	export OPENDIRUNLINK=${OPENDIRUNLINK:-$TESTS_DIR/src/opendirunlink}
	check_command $OPENDIRUNLINK	

	export OPENFILLEDDIRUNLINK=${OPENFILLEDDIRUNLINK:-$TESTS_DIR/src/openfilleddirunlink}
	check_command $OPENFILLEDDIRUNLINK	

	export TRUNCATE=${TRUNCATE:-$TESTS_DIR/src/truncate}
	check_command $TRUNCATE

	export SMALL_WRITE=${SMALL_WRITE:-$TESTS_DIR/src/small_write}
	check_command $SMALL_WRITE

	export UTIME=${UTIME:-$TESTS_DIR/src/utime}
	check_command $UTIME

	export CREATEMANY=${CREATEMANY:-$TESTS_DIR/src/createmany}
	check_command $CREATEMANY

	export UNLINKEMANY=${UNLINKEMANY:-$TESTS_DIR/src/unlinkmany}
	check_command $UNLINKEMANY

	export SOCKETSERVER=${SOCKETSERVER:-$TESTS_DIR/src/socketserver.pl}
	check_command $SOCKETSERVER	

	export SOCKETCLIENT=${SOCKETCLIENT:-$TESTS_DIR/src/socketclient.pl}
	check_command $SOCKETCLIENT

	export MUNLINK=${MUNLINK:-$TESTS_DIR/src/munlink}
	check_command $MUNLINK

	export DIRCTIO=${DIRCTIO:-$TESTS_DIR/src/directio}
	check_command $DIRCTIO

	export FLOCKS_TEST=${FLOCKS_TEST:-$TESTS_DIR/src/flocks_test}
	check_command $FLOCKS_TEST

	export MULTIFSTAT=${MULTIFSTAT:-$TESTS_DIR/src/multifstat}
	check_command $MULTIFSTAT

	export OPENDEVUNLINK=${OPENDEVUNLINK:-$TESTS_DIR/src/opendevunlink}
	check_command $OPENDEVUNLINK

	export STATMANY=${STATMANY:-$TESTS_DIR/src/statmany}
	check_command $STATMANY

	export MMAP_SANITY=${MMAP_SANITY:-$TESTS_DIR/src/mmap_sanity}
	check_command $MMAP_SANITY	

	export LEAK_FINDER=${LEAK_FINDER:-$TESTS_DIR/src/leak_finder.pl}
	check_command $LEAK_FIND

	export MLINK=${MLINK:-$TESTS_DIR/src/mlink}
	check_command $MLINK
	
	export RWV=${RWV:-$TESTS_DIR/src/rwv}
	check_command $RWV
	
	export SENDFILE=${SENDFILE:-$TESTS_DIR/src/sendfile}
	check_command $SENDFILE

	export CORRECT=${CORRECT:-$TESTS_DIR/src/correct}
	check_command $CORRECT

	export MULTICORRECT=${MULTICORRECT:-$TESTS_DIR/src/multicorrect}
	check_command $MULTICORRECT

	export APPEND_MANY=${APPEND_MANY:-$TESTS_DIR/src/append_many}
	check_command $APPEND_MANY

	export SKIPWRITE_MANY=${SKIPWRITE_MANY:-$TESTS_DIR/src/skipwrite_many}
	check_command $SKIPWRITE_MANY

	export MOUNT_MTFS=${MOUNT_MTFS:-$UTILS_DIR/mount.mtfs}
	check_command $MOUNT_MTFS
	
	export UTIL_MTFS=${UTIL_MTFS:-$UTILS_DIR/mtfs}
	check_command $UTIL_MTFS
	cleanup_and_setup

	if [ -d $DIR ]; then
		rm -rf $DIR/[Rdfs][0-9]*
		if [ "$DOING_MUTLTI" = "yes" ]; then
			rm -rf $DIR1/[Rdfs][0-9]*
			rm -rf $DIR2/[Rdfs][0-9]*
		fi
	else
		mkdir $DIR
	fi

	if [ $UID -ne 0 ]; then
		log "running as non-root uid $UID"
		export RUNAS_ID="$UID"
		export RUNAS=""
	else
		export RUNAS_ID=${RUNAS_ID:-505}
		export RUNAS=${RUNAS:-"$TESTS_DIR/src/runas -u $RUNAS_ID"}
	fi
	check_command $RUNAS

	check_runas_id $RUNAS_ID $RUNAS_ID $RUNAS
	build_test_filter
}

run_one_cleanup_setup()
{
	if [ "$CLEANUP_PER_TEST" = "yes" ]; then 
		setup_all
	fi

	run_one $1 "$2"
	local ret=$?

	if [ "$CLEANUP_PER_TEST" = "yes" ]; then
		cleanup_all
	fi

	return $ret
}

# print a newline if the last test was skipped
export LAST_SKIPPED=
run_test()
{
	export base=`basetest $1`
	if [ ! -z "$ONLY" ]; then
		testname=ONLY_$1
		if [ ${!testname}x != x ]; then
			[ "$LAST_SKIPPED" ] && echo "" && LAST_SKIPPED=
			run_one_cleanup_setup $1 "$2"
			return $?
        fi
		
		testname=ONLY_$base
		if [ ${!testname}x != x ]; then
			[ "$LAST_SKIPPED" ] && echo "" && LAST_SKIPPED=
			run_one_cleanup_setup $1 "$2"
			return $?
		fi
		LAST_SKIPPED="y"
		echo -n "."
		return 0
	fi
	
	testname=EXCEPT_$1
	if [ ${!testname}x != x ]; then
		LAST_SKIPPED="y"
		TESTNAME=test_$1 skip "skipping excluded test $1"
		return 0
	fi
	
	testname=EXCEPT_$base
	if [ ${!testname}x != x ]; then
		LAST_SKIPPED="y"
		TESTNAME=test_$1 skip "skipping excluded test $1 (base $base)"
		return 0
	fi
	
	testname=EXCEPT_SLOW_$1
	if [ ${!testname}x != x ]; then
		LAST_SKIPPED="y"
		TESTNAME=test_$1 skip "skipping SLOW test $1"
		return 0
	fi
	testname=EXCEPT_SLOW_$base
	if [ ${!testname}x != x ]; then
		LAST_SKIPPED="y"
		TESTNAME=test_$1 skip "skipping SLOW test $1 (base $base)"
		return 0
	fi

	LAST_SKIPPED=
	run_one_cleanup_setup $1 "$2"

	return $?
}

error_noexit()
{
	log "== ${TESTSUITE} ${TESTNAME} failed: $@ == `date +%H:%M:%S`"
	TEST_FAILED=true
}

error()
{
	error_noexit "$@"
	[ "$FAIL_ON_ERROR" = "yes" ] && exit 1 || true
}

error_exit() {
    error_noexit "$@"
    exit 1
}

testslist_filter ()
{
    local script=$TESTS_DIR/${TESTSUITE}.sh

    [ -f $script ] || return 0

    local start_at=$START_AT
    local stop_at=$STOP_AT

    local var=${TESTSUITE//-/_}_START_AT
    [ x"${!var}" != x ] && start_at=${!var}
    var=${TESTSUITE//-/_}_STOP_AT
    [ x"${!var}" != x ] && stop_at=${!var}

    sed -n 's/^test_\([^ (]*\).*/\1/p' $script | \
        awk ' BEGIN { if ("'${start_at:-0}'" != 0) flag = 1 }
            /^'${start_at}'$/ {flag = 0}
            {if (flag == 1) print $0}
            /^'${stop_at}'$/ { flag = 1 }'
}

build_test_filter()
{
	
    EXCEPT="$EXCEPT $(testslist_filter)"

    [ "$ONLY" ] && log "only running test `echo $ONLY`"
    for O in $ONLY; do
        eval ONLY_${O}=true
    done
	
    [ "$EXCEPT$ALWAYS_EXCEPT" ] && \
        log "excepting tests: `echo $EXCEPT $ALWAYS_EXCEPT`"
    for E in $EXCEPT $ALWAYS_EXCEPT; do
        eval EXCEPT_${E}=true
    done
	
	if [ ! "$DO_SLOW_TESTS" = "yes" ]; then
	    [ "$EXCEPT_SLOW" ] && \
			log "skipping tests DO_SLOW_TESTS=no: `echo $EXCEPT_SLOW`"
		for E in $EXCEPT_SLOW; do
			eval EXCEPT_SLOW_${E}=true
		done
	fi
	
    for G in $GRANT_CHECK_LIST; do
        eval GCHECK_ONLY_${G}=true
   	done
}

skip()
{
	log " skip: ${TESTSUITE} ${TESTNAME} $@"
}

assert_DIR()
{
	local failed=""
	[[ $DIR/ = $MTFS_MNT1/* ]] || \
		{ failed=1 && echo "DIR=$DIR not in $MTFS_MNT1. Aborting."; }
	if [ "$DOING_MUTLTI" = "yes" ]; then
		[[ $DIR1/ = $MTFS_MNT1/* ]] || \
			{ failed=1 && echo "DIR1=$DIR1 not in $MTFS_MNT1. Aborting."; }
		[[ $DIR2/ = $MTFS_MNT2/* ]] || \
			{ failed=1 && echo "DIR2=$DIR2 not in $MTFS_MNT2. Aborting"; }
		[ $DIR1 = $DIR2 ] && \
			{ failed=1 && echo "DIR1=$DIR1 is equal to DIR2=$DIR2 Aborting"; }
	fi

	[ -n "$failed" ] && exit 99 || true
}

check_runas_id_ret()
{
	local myRC=0
	local myRUNAS_UID=$1
	local myRUNAS_GID=$2
	shift 2
	local myRUNAS=$@
	if [ -z "$myRUNAS" ]; then
		error_exit "myRUNAS command must be specified for check_runas_id"
	fi
	mkdir $DIR/d0_runas_test
	chmod 0755 $DIR
	chown $myRUNAS_UID:$myRUNAS_GID $DIR/d0_runas_test
	$myRUNAS touch $DIR/d0_runas_test/f$$ || myRC=1
	rm -rf $DIR/d0_runas_test
	return $myRC
}

check_runas_id()
{
	local myRUNAS_UID=$1
	local myRUNAS_GID=$2
	shift 2
	local myRUNAS=$@
	check_runas_id_ret $myRUNAS_UID $myRUNAS_GID $myRUNAS || \
		error_exit "unable to write to $DIR/d0_runas_test as UID $myRUNAS_UID.
			Please set RUNAS_ID to some UID which exists or
			add user $myRUNAS_UID:$myRUNAS_GID."
}

# Run multiop in the background, but wait for it to print
# "PAUSING" to its stdout before returning from this function.
multiop_bg_pause()
{
	#MULTIOP_PROG=${MULTIOP_PROG:-multiop}
	MULTIOP_PROG=${MULTIOP_PROG:-$MULTIOP}
	FILE=$1
	ARGS=$2

	TMPPIPE=/tmp/multiop_open_wait_pipe.$$
	mkfifo $TMPPIPE

	echo "$MULTIOP_PROG $FILE v$ARGS"
	$MULTIOP_PROG $FILE v$ARGS > $TMPPIPE &

	echo "TMPPIPE=${TMPPIPE}"
	local multiop_output

	read -t 60 multiop_output < $TMPPIPE
	if [ $? -ne 0 ]; then
		rm -f $TMPPIPE
		return 1
	fi
	rm -f $TMPPIPE
	if [ "$multiop_output" != "PAUSING" ]; then
		echo "Incorrect multiop output: $multiop_output"
		kill -9 $PID
		return 1
	fi

	return 0
}

no_dsh()
{
	shift
	eval $@
}

do_node()
{
	HOST=$1
	shift
	local myPDSH=$PDSH
	if [ "$HOST" = "$HOSTNAME" ]; then
		myPDSH="no_dsh"
	elif [ -z "$myPDSH" -o "$myPDSH" = "no_dsh" ]; then
		echo "cannot run remote command on $HOST with $myPDSH"
		return 128
	fi
	if $VERBOSE; then
		echo "CMD: $HOST $@" >&2
		$myPDSH $HOST $LCTL mark "$@" > /dev/null 2>&1 || :
	fi

	if [ "$myPDSH" = "rsh" ]; then
		# we need this because rsh does not return exit code of an executed command
		local command_status="$TMP/cs"
		rsh $HOST ":> $command_status"
		rsh $HOST "(PATH=\$PATH:$RMTFS/utils:$RMTFS/tests:/sbin:/usr/sbin;
		cd $RPWD; sh -c \"$@\") ||
		echo command failed >$command_status"
		[ -n "$($myPDSH $HOST cat $command_status)" ] && return 1 || true
		return 0
	fi
	$myPDSH $HOST "(PATH=\$PATH:$RMTFS/utils:$RMTFS/tests:/sbin:/usr/sbin; cd $RPWD; sh -c \"$@\")" | sed "s/^${HOST}: //"
	return ${PIPESTATUS[0]}
}

single_local_node()
{
	[ "$1" = "$HOSTNAME" ]
}

do_nodes()
{
	local rnodes=$1
	shift

	if $(single_local_node $rnodes); then
		do_node $rnodes $@
		return $?
	fi

	# This is part from do_node
	local myPDSH=$PDSH

	[ -z "$myPDSH" -o "$myPDSH" = "no_dsh" -o "$myPDSH" = "rsh" ] && \
		echo "cannot run remote command on $rnodes with $myPDSH" && return 128

	if $VERBOSE; then
		echo "CMD: $rnodes $@" >&2
		$myPDSH $rnodes $LCTL mark "$@" > /dev/null 2>&1 || :
	fi
	local i
	local j
	for((i=1;i<100;i++)){
		j="\$$i"
		host=`echo $rnodes | awk -F, "{print $j}"`
		if [[ $host = "" ]];then
			break
		fi
		$myPDSH $host "(PATH=\$PATH:$RMTFS/utils:$RMTFS/tests:/sbin:/usr/sbin; cd $RPWD; sh -c \"$@\")" | sed -re "s/\w+:\s//g"
	}
#    $myPDSH $rnodes "(PATH=\$PATH:$RMTFS/utils:$RMTFS/tests:/sbin:/usr/sbin; cd $RPWD; sh -c \"$@\")" | sed -re "s/\w+:\s//g"
	return ${PIPESTATUS[0]}
}

assert_env() {
	local failed=""
	for name in $@; do
		if [ -z "${!name}" ]; then
			echo "$0: $name must be set"
			failed=1
		fi
	done
	[ $failed ] && exit 1 || true
}

wait_remote_prog () {
	local prog=$1
	local WAIT=0
	local INTERVAL=5
	local rc=0

	[ "$PDSH" = "no_dsh" ] && return 0

	while [ $WAIT -lt $2 ]; do
		running=$(ps uax | grep "$PDSH.*$prog.*$MOUNT" | grep -v grep) || true
		[ -z "${running}" ] && return 0 || true
		echo "waited $WAIT for: "
		echo "$running"
		[ $INTERVAL -lt 60 ] && INTERVAL=$((INTERVAL + INTERVAL))
		sleep $INTERVAL
		WAIT=$((WAIT + INTERVAL))
	done
	local pids=$(ps  uax | grep "$PDSH.*$prog.*$MOUNT" | grep -v grep | awk '{print $2}')
	[ -z "$pids" ] && return 0
	echo "$PDSH processes still exists after $WAIT seconds.  Still running: $pids"
	# FIXME: not portable
	for pid in $pids; do
		cat /proc/${pid}/status || true
		cat /proc/${pid}/wchan || true
		echo "Killing $pid"
		kill -9 $pid || true
		sleep 1
		ps -P $pid && rc=1
	done
	
	return $rc
}

check_nonexist()
{
	local LOCAL_FILE=$1
	local LOCAL_DIR=$(dirname $1)
	local LOCAL_BASE=$(basename $1)
	local EXIST=0

	$CHECKSTAT -t file $LOCAL_FILE > /dev/null 2>&1
	local RETVAL=$?
	if [ $RETVAL -eq 0 ]; then
		EXIST=`expr $EXIST + 1`
	fi

	ls $LOCAL_FILE > /dev/null 2>&1
	RETVAL=$?
	if [ $RETVAL -eq 0 ]; then
		EXIST=`expr $EXIST + 2`
	fi

	ls $LOCAL_DIR | grep -q $LOCAL_BASE > /dev/null 2>&1
	RETVAL=$?
	if [ $RETVAL -eq 0 ]; then
		EXIST=`expr $EXIST + 4`
	fi

	return $EXIST
}

check_exist()
{
	check_nonexist $1
	local RETVAL=$?
	if [ $RETVAL -eq 7 ]; then
		return 0
	fi
	return $RETVAL
}

check_common_nonexist()
{
	local LOCAL_TYPE=$1
	local LOCAL_FILE=$2
	local LOCAL_DIR=$(dirname $LOCAL_FILE)
	local LOCAL_BASE=$(basename $LOCAL_FILE)
	local EXIST=0

	$CHECKSTAT -t $LOCAL_TYPE $LOCAL_FILE > /dev/null 2>&1
	local RETVAL=$?
	if [ $RETVAL -eq 0 ]; then
		EXIST=`expr $EXIST + 1`
	fi

	ls $LOCAL_FILE > /dev/null 2>&1
	RETVAL=$?
	if [ $RETVAL -eq 0 ]; then
		EXIST=`expr $EXIST + 2`
	fi

	ls $LOCAL_DIR | grep -q $LOCAL_BASE > /dev/null 2>&1
	RETVAL=$?
	if [ $RETVAL -eq 0 ]; then
		EXIST=`expr $EXIST + 4`
	fi

	return $EXIST
}

check_common_exist()
{
	check_common_nonexist $1 $2
	local RETVAL=$?
	if [ $RETVAL -eq 7 ]; then
		return 0
	fi
	return $RETVAL
}

check_dir_nonexist()
{
	check_common_nonexist dir $1
	return $?
}

check_dir_exist()
{
	check_common_exist dir $1
	return $?
}


