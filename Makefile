# SPDX-License-Identifier: GPL-2.0-or-later
SHELL=/bin/sh
CC=clang
DEBUG=-O0 -g
CFLAGS=-std=c17 -Wall -Wextra \
	-Werror=incompatible-pointer-types \
	-Werror=unreachable-code \
	-Werror=sizeof-pointer-memaccess \
	-ftrivial-auto-var-init=pattern \
	-Os \
	$(DEBUG)
IFLAGS=-I. $(CLANG_INCLUDE)
LD=clang

CLANG_INCLUDE=-I/usr/lib/llvm-16/include/
CLANG_LIB=-lclang-16
SQLITE_LIB=-lsqlite3

BUILD_DIR=build

SRCS=cf_alloc.c \
	cfind.c \
	cf_index.c \
	cfind-index.c \
	cf_map.c \
	cf_string.c \
	cf_vector.c \
	cf_db.c \
	db_types.c \
	main_support.c \
	mem_db.c \
	nop_db.c \
	parse.c \
	print_ast.c \
	search.c \
	search_types.c \
	sql_db.c \
	sql_query.c \
	token.c

OBJS=$(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS=$(addprefix $(BUILD_DIR)/,$(SRCS:.c=.d))
BINS=cfind-index cfind

CFIND_INDEX_OBJS= $(addprefix $(BUILD_DIR)/,\
	cfind-index.o \
	cf_index.o \
	cf_alloc.o \
	cf_map.o \
	cf_string.o \
	cf_vector.o \
	cf_db.o \
	db_types.o \
	main_support.o \
	mem_db.o \
	nop_db.o \
	sql_db.o \
	sql_query.o \
	)

CFIND_OBJS=$(addprefix $(BUILD_DIR)/, \
	cfind.o \
	cf_alloc.o \
	cf_map.o \
	cf_string.o \
	cf_vector.o \
	cf_db.o \
	db_types.o \
	main_support.o \
	mem_db.o \
	nop_db.o \
	parse.o \
	search.o \
	search_types.o \
	sql_db.o \
	sql_query.o \
	token.o \
	)

# must be first
all: $(BINS)

# How does this dependency nonsense work?:
#
# Let's say the target is 'cf_alloc.o'. On the first invocation of make, it
# tries to `include cf_alloc.d`. This file doesn't exist but there's a rule
# further down in the makefile to create it. (This is the `%.d: %.c` rule).
# make executes this rule and the result is a file "cf_alloc.d" with contents:
#    cf_alloc.d cf_alloc.o: cf_alloc.c cf_alloc.h
# This file is then included by make as interpreted as two new rules.
# Each rule is implicit, and although it doesn't fully specify how to create
# 'cf_alloc.o', it serves to edit its dependencies. Further down in the
# makefile, the rule '%.o: %.c' uses these new dependencies to create
# 'cf_alloc.o'. The 'cf_alloc.d' rule is similar. It specifies that the '.d'
# file needs to be regenerated if any of the current dependencies of
# 'cf_alloc.o' change.

ifeq (,$(filter clean,$(MAKECMDGOALS)))
# include .d files except during 'make clean'
-include $(DEPS)
endif

$(BUILD_DIR)/%.d: %.c
	set -e; \
	if ! test -d $(BUILD_DIR) ; then \
		mkdir $(BUILD_DIR); \
	fi;
	printf '%s %s/' $@ $(BUILD_DIR) > $@; \
	$(CC) -MM $(IFLAGS) $< >> $@;

$(BUILD_DIR)/%.o: %.c
	$(CC) -c $(CFLAGS) $(IFLAGS) $< -o $@

obj: $(OBJS)

cfind-index: $(BUILD_DIR)/cfind-index
$(BUILD_DIR)/cfind-index: $(CFIND_INDEX_OBJS)
	$(LD) $(CLANG_LIB) $(SQLITE_LIB) -o $@ $^

cfind: $(BUILD_DIR)/cfind
$(BUILD_DIR)/cfind: $(CFIND_OBJS)
	$(LD) $(SQLITE_LIB) -o $@ $^

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/*

.PHONY: all clean cfind-index cfind
