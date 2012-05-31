#
# Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
#

# MTFS libmtfs test Makefile
AM_CFLAGS=$(LLCFLAGS)
AM_CPPFLAGS=$(LLCPPFLAGS)
AM_LDFLAGS :=

LIBMTFS_LIBS = $(top_builddir)/mtfs/libuser/libuser.a
if LIBMTFS_TESTS
noinst_PROGRAMS = err2str

noinst_PROGRAMS += test_rule_tree test_branch_bitmap test_parse_option test_cfg

test_rule_tree_SOURCES = test_rule_tree.c
test_rule_tree_CFLAGS = $(LL_CFLAGS)
test_rule_tree_LDADD := $(LIBMTFS_LIBS)
test_rule_tree_DEPENDENCIES := $(LIBMTFS_LIBS)

test_branch_bitmap_SOURCES = test_branch_bitmap.c
test_branch_bitmap_CFLAGS = $(LL_CFLAGS)
test_branch_bitmap_LDADD := $(LIBMTFS_LIBS)
test_branch_bitmap_DEPENDENCIES := $(LIBMTFS_LIBS)

test_parse_option_SOURCES = test_parse_option.c
test_parse_option_CFLAGS = $(LL_CFLAGS)
test_parse_option_LDADD := $(LIBMTFS_LIBS)
test_parse_option_DEPENDENCIES := $(LIBMTFS_LIBS)

test_cfg_SOURCES = test_cfg.c
test_cfg_CFLAGS = $(LL_CFLAGS)
test_cfg_LDADD := $(LIBMTFS_LIBS)
test_cfg_DEPENDENCIES := $(LIBMTFS_LIBS)
endif #LIBMTFS_TESTS

noinst_DATA = 
noinst_SCRIPTS =
EXTRA_DIST = run.sh misc.sh
EXTRA_DIST += rule_tree branch_bitmap parse_option cfg