/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * main()-containing file for the indexer.
 *
 * The goal is to produce an index (a search database) from a bunch of C source
 * files.
 */
#include "cf_index.h"
#include "cf_print.h"
#include "main_support.h"
#include "version.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sysexits.h>
#include <sys/param.h>

typedef struct {
	bool help;
	bool version;
	index_config_t config;
} cfind_index_args_t;

static void print_usage(void);
static void print_help(void);

static const struct option cfind_index_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{"src", no_argument, NULL, 's'},
	{"dir", no_argument, NULL, 'd'},
	{"out", required_argument, NULL, 'o'},
	{"dry-run", no_argument, NULL, 'n'},
	{NULL, 0, NULL, 0},
};

static void
print_usage(void)
{
	printf("Usage: cfind-index [OPTION]... [-s] source-file\n" \
			"   or: cfind-index [OPTION]... -d build-directory\n");
}

static void
print_help(void)
{
	print_usage();
	printf("cfind indexing tool. " \
			"Create a search database from C source files.\n" \
			"OPTIONS:\n" \
			"   -h, --help      print this\n" \
			"   --version       display version\n" \
			"   -s, --src       input path is a single `.c' file (default)\n" \
			"   -d, --dir       input path is the parent directory of a \n" \
			"                   compilation database\n" \
			"   -o, --out       path to sqlite database to create\n" \
			"   -n, --dry-run   input file is a single `.c' file\n"
			);
}

static void
print_version(void)
{
	printf("cfind-index %s\n", CF_VERSION_STR);
}

/*
 * Three return values:
 * - 0
 *   keep going
 * - 1
 *   stop; successfully parsed all arguments
 * - anything else
 *   error
 */
static int
parse_one_arg(int argc, char **argv, cfind_index_args_t *out)
{
	int option_index;
	int c = getopt_long(argc, argv, "hVsdo:n", cfind_index_options,
			&option_index);
	if (c == -1) {
		return 1;
	}

	switch (c) {
		case 'h':
			out->help = true;
			break;
		case 'V':
			out->version = true;
			break;
		case 's':
			out->config.input_kind = input_source_file;
			break;
		case 'd':
			out->config.input_kind = input_comp_db;
			break;
		case 'o':
			out->config.db_args.sql_path = optarg;
			out->config.db_kind = index_db_sql;
			break;
		case 'n':
			out->config.db_args.sql_path = NULL;
			out->config.db_kind = index_db_nop;
			break;
		default:
		case '?':
			return EX_USAGE;
	}
	return 0;
}

/*
 * Default CLI arguments.
 *
 * Notable defaults:
 * - single '.c' file is indexed
 * - default output database is sqlite file "cf.db"
 */
static void
make_default_args(cfind_index_args_t *out)
{
	*out = (cfind_index_args_t) {
		.config = {
			.db_kind = index_db_sql,
			.input_kind = input_source_file,
			.db_args = {
				.sql_path = "cf.db"
			},
		},
	};
}

static int
parse_args(int argc, char **argv, cfind_index_args_t *out)
{
	int error;
	make_default_args(out);

	while (!(error = parse_one_arg(argc, argv, out))) {
	}
	if (error != 1) {
		return error;
	}
	if (out->help || out->version) {
		// stop parsing if '-h' or '--version' is given
		return 0;
	}

	// last argument is always input path
	if (optind >= argc) {
		printf("missing input file\n");
		return EX_USAGE;
	}

	out->config.input_path = argv[optind++];
	return 0;
}

int
main(int argc, char **argv)
{
	int error;
	cfind_index_args_t args;

	if ((error = cf_setup_stdio())) {
		// note: this returns an errno rather than a sysexits value
		return error;
	}

	// parse `argv` into a struct
	if ((error = parse_args(argc, argv, &args))) {
		print_usage();
		return error;
	}

	// execute based on `args`

	if (args.help) {
		print_help();
		return 0;
	}
	if (args.version) {
		print_version();
		return 0;
	}

	cf_print_info("index %s('%s')\n",
			((args.config.input_kind == input_comp_db) ?
					"index_project" : "index_source"),
			args.config.input_path);
	// call into indexer
	if ((error = cf_index_project(&args.config))) {
		return EX_DATAERR;
	}

	return 0;
}
