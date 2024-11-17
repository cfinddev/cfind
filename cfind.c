/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * cfind database query tool.
 */
#include "main_support.h"
#include "cf_print.h"
#include "cf_string.h"
#include "search.h"
#include "sql_db.h"
#include "version.h"

#include <unistd.h>
#include <getopt.h>
#include <sysexits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sqlite3.h>

/*
 */
typedef struct {
	char *db_path;
	cf_str_t cmd_str;
	bool help;
	bool version;
	bool cmd;
} cfind_args_t;

static const struct option cfind_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{"interactive", no_argument, NULL, 'i'},
	{"command", required_argument, NULL, 'c'},
	{NULL, 0, NULL, 0},
};

static void
print_usage(void)
{
	printf("Usage: cfind [OPTION]... [-i] database-file\n" \
			"   or: cfind [OPTION]... -c cmd database-file\n");
}

static void
print_help(void)
{
	print_usage();
	printf("cfind query tool. " \
			"Search a database created by cfind-index.\n" \
			"OPTIONS:\n" \
			"   -h, --help            print this\n" \
			"   --version             display version\n" \
			"   -i, --interactive     interactive mode (default)\n" \
			"   -c, -cmd <command>    execute a single command\n"
			);
}

static void
print_version(void)
{
	printf("cfind %s\n", CF_VERSION_STR);
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
parse_one_arg(int argc, char **argv, cfind_args_t *out)
{
	int option_index;
	int c = getopt_long(argc, argv, "hVic:", cfind_options, &option_index);
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
		case 'c':
			out->cmd = true;
			cf_str_borrow(optarg, strlen(optarg), &out->cmd_str);
			break;
		case 'i':
			out->cmd = false;
			break;
		default:
		case '?':
			return EX_USAGE;
	}
	return 0;
}

static int
parse_args(int argc, char **argv, cfind_args_t *out)
{
	int error;
	memset(out, 0, sizeof(*out));

	while (!(error = parse_one_arg(argc, argv, out))) {
	}
	if (error != 1) {
		return error;
	}
	if (out->help || out->version) {
		// stop parsing if '-h' or '--version' is given
		return 0;
	}

	if (optind >= argc) {
		printf("missing database-file\n");
		return EX_USAGE;
	}

	out->db_path = argv[optind++];
	return 0;
}

/*
 * sqlite3 database backend demo
 *
 * Simulate cfind index query tool.
 */
int
main(int argc, char **argv)
{
	cf_setup_stdio();

	int error;
	cfind_args_t args;

	if ((error = parse_args(argc, argv, &args))) {
		print_usage();
		return error;
	}

	if (args.help) {
		print_help();
		return 0;
	}
	if (args.version) {
		print_version();
		return 0;
	}

	if (!args.cmd) {
		cf_print_err("interactive mode unimplemented\n");
		return EX_UNAVAILABLE;
	}

	return run_one_command(args.db_path, &args.cmd_str);
}
