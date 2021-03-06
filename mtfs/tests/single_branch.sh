#!/bin/bash
set -e

TESTS_DIR=${TESTS_DIR:-$(cd $(dirname $0); echo $PWD)}

. $TESTS_DIR/test-framework.sh
init_test_env

ORIGIN_DIR=$DIR
ORIGIN_DIR1=$DIR1
ORIGIN_DIR2=$DIR2

export SINGLE_DIR=${SINGLE_DIR:-$ORIGIN_DIR/single_tests}
rm $SINGLE_DIR -fr

make_single()
{
	local REMOVED_DIR=$1

	$UTIL_MTFS setbranch -b 0 -d 0 $MTFS_MNT1/test/
	$UTIL_MTFS setbranch -b 1 -d 0 $MTFS_MNT1/test/
	check_dir_nonexist $DIR || rm $DIR -fr
	check_dir_nonexist $DIR || error "failed to rmdir $DIR"
	mkdir $DIR
	check_dir_exist $DIR || error "failed to mkdir $DIR"
	check_dir_exist $REMOVED_DIR || error "mtfs failed to mkdir a branch $REMOVED_DIR"
	rm $REMOVED_DIR -fr
	check_dir_nonexist $REMOVED_DIR || error "failed to remove $REMOVED_DIR"
	[ -d $DIR ] || error "$DIR is gone"
}

export DIR=$SINGLE_DIR
export DIR1=$MTFS_MNT1/$DIR_SUB/single_tests
export DIR2=$MTFS_MNT2/$DIR_SUB/single_tests

for INDEX in ${!BRANCH_DIR_ARRAY[@]}; do
	for SKIP_INDEX in ${SKIP_ABANDON_BRANCHES[@]}; do
		if [ "$SKIP_INDEX" = "$INDEX" ]; then
			continue 2
		fi
	done

	BRANCH_DIR=${BRANCH_DIR_ARRAY[$INDEX]}
	make_single $BRANCH_DIR/$DIR_SUB/single_tests
	bash posix.sh
	bash multi_mnt.sh
done;

rm $DIR -fr

export DIR=$ORIGIN_DIR
export DIR1=$ORIGIN_DIR1
export DIR2=$ORIGIN_DIR2

echo "cleanup_all start"
cleanup_all
echo "=== $0: completed ==="
