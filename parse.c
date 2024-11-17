/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * What is this file?
 *
 * The cfind CLI needs to take a dsl string query, parse, and execute it.
 * This is the file to do just that part, but the search mechanics themselves
 * need to be in sql_db.h
 *
 * There's also the question of whether other database types should be able to
 * support queries. Well, it kind of needs to to for unit tests.
 * - here's a C file/snippet
 * - run indexer on it
 * - run search query on index
 * - assert results
 *
 * Unit tests don't want to have to set up sql databases.
 * XXX only contains parser now
 */
#include "parse.h"

#include "search_types.h"
#include "cf_string.h"
#include "cf_print.h"
#include "cf_assert.h"
#include "cc_support.h"
#include "db_types.h"
#include "token.h"

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static int parse_command_verb(cf_tok_iter_t *iter, search_kind_t *out);
static int parse_type_search(cf_tok_iter_t *iter, type_search_t *out);
static int parse_typename_search(cf_tok_iter_t *iter, typename_search_t *out);
static int parse_member_search(cf_tok_iter_t *iter, member_search_t *out);

static int parse_name_spec1(cf_tok_iter_t *iter, name_spec_t *out);
static int parse_name_spec2(const cf_str_t *tok, cf_tok_iter_t *iter,
		name_spec_t *out);

static bool str2uint64(const cf_str_t *str, uint64_t *out);
static bool char2int(char c, uint8_t *out);
static bool is_digit(char c);

static bool command_string2kind(const cf_str_t *str, search_kind_t *out);
static name_elab_t str2elab(const cf_str_t *str);

static bool litcmp_(const char *lit, size_t len, const cf_str_t *s2);

/*
 * Wrapper to litcmp_().
 *
 * Test `lit` is a string literal, and pass in its strlen(3) -- which has zero
 * cost if the libc string header is good.
 */
#define litcmp(lit, str) ({ \
	_Static_assert(__builtin_constant_p(lit), \
			"first argument must be a string literal"); \
	litcmp_(lit, strlen(lit), str); \
})

/*
 * Turn a command string into a struct representation.
 *
 * Grammar
 * -------
 *
 * COMMAND [OPTION]... ARGS...
 *
 * COMMAND:
 *   td, typedecl    search for type declaration
 *   tn, typename    name of a type
 *   md, memberdecl  member declaration
 *   XXX add type use and member use
 *
 * OPTIONS:
 *   XXX unimplemented
 *
 * Commands explained:
 * - typedecl
 *   Search for the definition location of a user defined type.
 *   ARGS: <ID> | <name>
 *   - <ID>
 *     numeric identifier that uniquely identifies a type
 *     useful for redoing searches if name is ambiguous
 *   - <name>
 *     name of user defined type
 *     many different kinds of names are supported
 *     `struct name {};` can be found with args=["struct", "name"]
 *     struct {} name;
 *     typedef struct {} name;
 *     XXX clean up
 *     The parser is informed enough about the C language to know that
 *     "struct foo" is the whole name of a type.
 * - typename
 *   Search for the definition of the name of a type. This is different from
 *   the `typedecl` command in the case of typedefs. `typedecl` searches for
 *   the location of the underlying type; `typename` searches for the location
 *   of a name for a type.
 *   ARGS: <name>
 *   - <name>
 *     type name to search for
 * - memberdecl
 *   Search for the definition location of a member of a struct or union.
 *   ARGS: <type-name> <member-name>
 *   - <type-name>
 *     sub-query for the owning struct
 *     same as typedecl argument
 *   - <member-name>
 *     name of the member
 *
 * ----------------
 * Steps:
 * - scan for command
 * - scan for next token
 *   starts with a '-', it's an option
 *   otherwise it's an arg
 * - pass tokenized args into sub parser according to what command is
 */
int
parse_command(const cf_str_t *cmd_str, search_cmd_t *out)
{
	int error;

	// make a token iterator over `cmd_str`
	cf_tok_iter_t iter;
	tok_iter_make(cmd_str, &iter);

	memset(out, 0, sizeof(*out));
	search_kind_t cmd;

	// parse token 0 as command verb
	if ((error = parse_command_verb(&iter, &cmd))) {
		cf_print_err("no command given, error %d\n", error);
		goto fail;
	}

	out->kind = cmd;

	// parse tokens 1... as args specific to `cmd`
	switch (cmd) {
		case search_type_decl:
			error = parse_type_search(&iter, &out->arg.type);
			break;
		case search_typename:
			error = parse_typename_search(&iter, &out->arg.typename);
			break;
		case search_member_decl:
			error = parse_member_search(&iter, &out->arg.member);
			break;
		default:
			__builtin_unreachable();
	}

	if (error) {
		cf_print_err("can't parse args, error %d\n", error);
		goto fail;
	}

	// warn about unparsed tokens
	if (tok_iter_next(&iter)) {
		cf_str_t tok;
		tok_iter_peek(&iter, &tok);
		cf_print_debug("trailing token(s) '%*.s'\n",
				(int)cf_str_len(&tok), tok.str);
		cf_str_free(&tok);
	}

fail:
	tok_iter_free(&iter);
	return error;
}

/*
 * XXX decide where `iter` should point on call and on return
 * on the current token, or before?
 *
 * it should point to the token before and be left pointing to the last parsed
 * token
 *
 * instead if
 * - points to unparse token on entrance
 * - points to token after on exit
 * if the iterator becomes exhausted, that fact is swallowed if the token
 * parsing is otherwise succesful
 *
 * the caller also has to make sure to throw away `iter` in case of any error
 * because it probably doesn't want to test `error` to determine iterator
 * exhaustion (but it could)
 */
static int
parse_command_verb(cf_tok_iter_t *iter, search_kind_t *out)
{
	cf_str_t tok;
	// parse first token as the command verb
	if (!tok_iter_next(iter)) {
		return ENOENT;
	}

	tok_iter_peek(iter, &tok);

	if (!command_string2kind(&tok, out)) {
		return EINVAL;
	}

	cf_str_free(&tok);
	return 0;
}

/*
 * first token is one of three things
 * 1. numeric type ID
 *   Test tok[0] is a digit (C names cannot start with a number).
 * 2. an elaborated type keyword
 *   Test token is one of "struct", "union", "enum".
 * 3. the name itself
 *   default case
 *
 * Case (2) requires another token to be extracted.
 *
 * On return, `iter` points to the last token extracted.
 */
static int
parse_type_search(cf_tok_iter_t *iter, type_search_t *out)
{
	int error = 0;
	cf_str_t tok;

	// extract token
	if (!tok_iter_next(iter)) {
		return ENOENT;
	}

	tok_iter_peek(iter, &tok);

	memset(out, 0, sizeof(*out));
	const bool is_id = is_digit(tok.str[0]);
	out->is_id = is_id;

	if (is_id) {
		// atoi on token
		uint64_t id;
		if (!str2uint64(&tok, &id)) {
			cf_print_err("cannot parse '%*.s' as a type id\n",
					(int)cf_str_len(&tok), tok.str);
			error = EINVAL;
			goto fail;
		}
		if (id > INT64_MAX) {
			cf_print_err("type id out of range %llu > INT64_MAX\n", p_(id));
			error = ERANGE;
			goto fail;
		}
		out->rowid = (int64_t)id;
	} else {
		// parse token as a name_spec_t
		parse_name_spec2(&tok, iter, &out->name);
	}

fail:
	cf_str_free(&tok);
	return error;
}

/*
 * Parse a `typename_search_t` from the tokens in `iter`.
 *
 * Tokens are either
 * 1. "struct" "foo"
 * 2. "foo_t"
 */
static int
parse_typename_search(cf_tok_iter_t *iter, typename_search_t *out)
{
	return parse_name_spec1(iter, &out->name);
}

/*
 * Parse a `member_search_t` from the tokens in `iter`.
 *
 * First 1 or 2 tokens are a `type_search_t`. The following token is the member
 * name.
 */
static int
parse_member_search(cf_tok_iter_t *iter, member_search_t *out)
{
	int error;

	// parse base type
	if ((error = parse_type_search(iter, &out->base))) {
		goto fail;
	}

	// parse member name
	if (!tok_iter_next(iter)) {
		error = ENOENT;
		goto fail_member;
	}

	tok_iter_peek(iter, &out->name);

	return 0;
fail_member:
	// free `out->base`
	if (!out->base.is_id) {
		cf_str_free(&out->base.name.name);
	}
fail:
	return error;
}

static int
parse_name_spec1(cf_tok_iter_t *iter, name_spec_t *out)
{
	int error;
	cf_str_t tok;

	if (!tok_iter_next(iter)) {
		return ENOENT;
	}

	tok_iter_peek(iter, &tok);
	if ((error = parse_name_spec2(&tok, iter, out))) {
		goto fail;
	}

fail:
	cf_str_free(&tok);
	return error;
}

static int
parse_name_spec2(const cf_str_t *tok, cf_tok_iter_t *iter, name_spec_t *out)
{
	memset(out, 0, sizeof(*out));
	out->kind = str2elab(tok);

	if (out->kind == name_none) {
		// parse `tok` as the name itself
		cf_str_borrow_str(tok, &out->name);
		return 0;
	}

	// `tok` is a C tag type keyword
	// extract the next token and parse it as the name
	if (!tok_iter_next(iter)) {
		cf_print_err("expected tag after keyword '%.*s'\n",
				(int)cf_str_len(tok), tok->str);
		return EINVAL;
	}
	tok_iter_peek(iter, &out->name);
	return 0;
}

/*
 * strtoll(3) with a more sensible return type.
 *
 * Also:
 * - unsigned value only (no '+' or '-' permitted)
 * - decimal
 * - UINT64_MAX is a valid value
 * - empty string is an error
 *
 * Return true if `str` was successfuly parsed; false if `str` is not an
 * integer or not representable as a `uint64_t`.
 */
static bool
str2uint64(const cf_str_t *str, uint64_t *out)
{
	uint64_t val = 0;
	const size_t len = cf_str_len(str);
	if (!len) {
		goto fail;
	}
	for (size_t i = 0; i < len; ++i) {
		// val = val * 10
		if (__builtin_mul_overflow(val, 10, &val)) {
			goto fail;
		}
		// val += str[i]
		uint8_t ones;
		if (!char2int(str->str[i], &ones)) {
			goto fail;
		}
		val += ones;
	}
	*out = val;
	return true;
fail:
	return false;
}

static bool
char2int(char c, uint8_t *out)
{
	if (!is_digit(c)) {
		return false;
	}
	*out = (uint8_t)(c - '0');
	return true;
}

/*
 * isdigit(3)
 */
static bool
is_digit(char c)
{
	return ('0' <= c) && (c <= '9');
}

static bool
command_string2kind(const cf_str_t *str, search_kind_t *out)
{
	if (litcmp("td", str) || litcmp("typedecl", str)) {
		*out = search_type_decl;
		return true;
	}

	if (litcmp("tn", str) || litcmp("typename", str)) {
		*out = search_typename;
		return true;
	}

	if (litcmp("md", str) || litcmp("memberdecl", str)) {
		*out = search_member_decl;
		return true;
	}

	return false;
}

static name_elab_t
str2elab(const cf_str_t *str)
{
	if (litcmp("struct", str)) {
		return name_struct;
	}
	if (litcmp("union", str)) {
		return name_union;
	}
	if (litcmp("enum", str)) {
		return name_enum;
	}
	return name_none;
}

/*
 * Compare two strings for exact equality.
 *
 * This is useful for comparing a NUL-terminated string literal `lit` with an
 * unterminated `cf_str_t` `s2`. strncmp(3) alone is insufficient because that
 * does a prefix comparison is `s2` is shorter than `lit`.
 */
static bool
litcmp_(const char *lit, size_t len, const cf_str_t *s2)
{
	if (len != cf_str_len(s2)) {
		return false;
	}
	return memcmp(lit, s2->str, len) == 0;
}
