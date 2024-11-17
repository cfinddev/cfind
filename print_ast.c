/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 cfind developer
 *
 * An unused file for printing debugging information about AST nodes.
 */
#include "cf_print.h"
#include "index_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "clang-c/CXString.h"
#include "clang-c/Index.h"

static void print_cursor(const index_ctx_t *ctx, CXCursor cursor);
static void inspect_struct(CXCursor cursor);
static void inspect_typedef(CXCursor cursor);
static void inspect_member(CXCursor cursor);
static void inspect_member_ref(CXCursor cursor);
static void type_traverse(CXCursor cursor);
static enum CXVisitorResult print_struct_members(
		CXCursor cursor, CXClientData ctx);

CF_VEC_FUNC_DECL(cursor_stack_t, CXCursor, cursor_stack);

typedef struct {
	unsigned count;
} member_ctx_t;

// 32 spaces
static const char space_buf[] =
	"                                ";

/*
 * Print debug information about a cursor used in AST traversal.
 */
static void
print_cursor(const index_ctx_t *ctx, CXCursor cursor)
{
	const uint32_t indent = MIN(2 * cursor_stack_len(&ctx->path.parent_stack),
			sizeof(space_buf));
	char name_buf[24];
	const char *name;
	const enum CXCursorKind kind = clang_getCursorKind(cursor);
	switch (kind) {
		case CXCursor_UnexposedDecl:
			name = "unexposed";
			break;
		case CXCursor_StructDecl:
			inspect_struct(cursor);
			name = "struct";
			break;
		case CXCursor_UnionDecl:
			name = "union";
			break;
		case CXCursor_EnumDecl:
			name = "enum";
			break;
		case CXCursor_FieldDecl:
			name = "member";
			inspect_member(cursor);
			break;
		case CXCursor_FunctionDecl:
			name = "function";
			break;
		case CXCursor_TypedefDecl:
			inspect_typedef(cursor);
			name = "typedef";
			break;
		case CXCursor_MemberRefExpr:
			// XXX shouldn't see this at global scope
			inspect_member_ref(cursor);
			name = "member-ref-expr";
			break;
		case CXCursor_VarDecl:
			name = "global-var";
			break;
		default:
			snprintf(name_buf, sizeof(name_buf), "%u", kind);
			name = name_buf;
			break;
	}

	CXString cursor_name = clang_getCursorSpelling(cursor);
	CXType ct = clang_getCursorType(cursor);
	CXString ct_name = clang_getTypeSpelling(ct);

	cf_print_debug("%.*siter: found kind=%s, "
			"cursor=(%p, '%s'), type=(%p, '%s')\n",
			indent, space_buf,
			name,
			cursor.data[0], clang_getCString(cursor_name),
			ct.data[0], clang_getCString(ct_name));

	clang_disposeString(ct_name);
	clang_disposeString(cursor_name);
}

/*
 */
static void
type_traverse(CXCursor cursor)
{
	CXType ct = clang_getCursorType(cursor);
	CXString name = clang_getCursorSpelling(cursor);

	CXPrintingPolicy policy = clang_getCursorPrintingPolicy(cursor);

	clang_PrintingPolicy_setProperty(policy,
			CXPrintingPolicy_SuppressTagKeyword, 0);
	clang_PrintingPolicy_setProperty(policy,
			CXPrintingPolicy_IncludeTagDefinition, 0);
	clang_PrintingPolicy_setProperty(policy,
			CXPrintingPolicy_TerseOutput, 1);

	CXString alt_name = clang_getCursorPrettyPrinted(cursor, policy);

	printf("cursor: ct.kind %d, clang_getCursorSpelling(cursor) -> '%s'\n"
			"clang_getCursorPrettyPrinted(cursor,) -> '%s'\n",
			ct.kind, clang_getCString(name), clang_getCString(alt_name));

	clang_PrintingPolicy_dispose(policy);
	clang_disposeString(alt_name);
	clang_disposeString(name);

	for (unsigned i = 0; ; ++i) {
		CXString n = clang_getTypeSpelling(ct);
		printf("  %u: ct={%p %p}, kind=%u, clang_getTypeSpelling(ct) -> '%s'\n",
				i,
				ct.data[0], ct.data[1],
				ct.kind,
				clang_getCString(n)
				);
		clang_disposeString(n);

		CXType canon = clang_getCanonicalType(ct);
		if (clang_equalTypes(ct, canon)) {
			printf("  END\n");
			break;
		}
		memcpy(&ct, &canon, sizeof(ct));
	}
}

/*
 * Print interesting information about a cursor pointing at a struct/union/enum
 * decl.
 */
static void
inspect_struct(CXCursor cursor)
{
	type_traverse(cursor);

	CXType ct = clang_getCursorType(cursor);
	CXType canon = clang_getCanonicalType(ct);

	// XXX can't yet tell if this is an anonymous struct
	const unsigned anon = clang_Cursor_isAnonymousRecordDecl(cursor);
	const unsigned anon2 = clang_Cursor_isAnonymous(cursor);

	// a hack
	const bool incomplete = clang_Type_getAlignOf(ct) ==
			CXTypeLayoutError_Incomplete;

	// note: these strings are owned, not borrowed
	CXString ussr = clang_getCursorUSR(cursor);
	CXString name = clang_getCursorSpelling(cursor);
	CXString type_spell = clang_getTypeSpelling(ct);
	CXString canon_name = clang_getTypeSpelling(canon);

	// XXX can't get a unique identifier for the type
	// maybe use clang_equalTypes()
	// XXX right now it's hard to tell when a type gets used
	// aha, it looks like CXType::data[0] stores a `clang::Type*`
	// this is what clang_equalTypes() does under the hood

	cf_print_debug(
			"struct decl; anon?:%d, anon2?:%d, incomplete?:%d, USR '%s', "
			"name '%s', type-spell '%s', canon-name '%s', "
			"clang::type kind %d\n",
			anon, anon2, incomplete, clang_getCString(ussr),
			clang_getCString(name), clang_getCString(type_spell),
			clang_getCString(type_spell), ct.kind);

	member_ctx_t ctx = {
		.count = 0,
	};
	(void)clang_Type_visitFields(ct, print_struct_members, &ctx);
	cf_print_debug("%u members\n", ctx.count);

	clang_disposeString(canon_name);
	clang_disposeString(type_spell);
	clang_disposeString(name);
}

static void
inspect_typedef(CXCursor cursor)
{
	CXType ct = clang_getCursorType(cursor);
	CXString new_name = clang_getTypedefName(ct);
	CXType old_type = clang_getTypedefDeclUnderlyingType(cursor);
	CXString old_name = clang_getTypeSpelling(old_type);
	CXType canon_type = clang_getCanonicalType(old_type);
	CXString canon_name = clang_getTypeSpelling(canon_type);

	const unsigned ttt = clang_Type_isTransparentTagTypedef(ct);

	cf_print_info(
			"typedef decl kind=%d, new_name='%s', cursor-type=%p, "
			"old-kind=%d, old_name='%s', old-type=%p,"
			"canon-kind=%d, canon_name='%s', canon-type=%p, "
			"ttt?: %u\n",
			ct.kind,
			clang_getCString(new_name),
			ct.data[0],
			old_type.kind,
			clang_getCString(old_name),
			old_type.data[0],
			canon_type.kind,
			clang_getCString(canon_name),
			canon_type.data[0],
			ttt
			);

	/*
	 * XXX two challenges:
	 * - traversing the type chain until `CXType` is the base-most type
	 * - doing a lookup with `CXType`; resolving it to a `rowid`
	 *
	 * XXX and
	 * - using this typedef as the primary name for an anonymous type
	 *   typedef struct { } foo_t;
	 */

	clang_disposeString(canon_name);
	clang_disposeString(old_name);
	clang_disposeString(new_name);
}

/*
 * Print about a `CXCursor_FieldDecl`
 */
static void
inspect_member(CXCursor cursor)
{
	if (clang_getCursorKind(cursor) != CXCursor_FieldDecl) {
		// XXX might be `CXCursor_EnumConstantDecl`
		cf_print_debug("  non-field in print_members() %d\n",
				clang_getCursorKind(cursor));
		return;
	}

	CXType ct = clang_getCursorType(cursor);
	CXString type_spell = clang_getTypeSpelling(ct);
	CXString name = clang_getCursorSpelling(cursor);

	cf_print_debug("  member: data[0]=%p (type '%s') '%s'\n",
			cursor.data[0],
			clang_getCString(type_spell),
			clang_getCString(name));

	clang_disposeString(type_spell);
	clang_disposeString(name);
}

/*
 * Print out information about a `CXCursor_MemberRefExpr`.
 */
static void
inspect_member_ref(CXCursor cursor)
{
	CXType ct = clang_getCursorType(cursor);
	CXString cursor_name = clang_getCursorSpelling(cursor);
	CXString name = clang_getTypeSpelling(ct);
	const void *data = cursor.data[0]; // should be a `FieldDecl *`
	CXPrintingPolicy policy = clang_getCursorPrintingPolicy(cursor);

	// clang_PrintingPolicy_setProperty(policy,
	// 		CXPrintingPolicy_SuppressTagKeyword, 0);
	// clang_PrintingPolicy_setProperty(policy,
	// 		CXPrintingPolicy_IncludeTagDefinition, 0);
	clang_PrintingPolicy_setProperty(policy,
			CXPrintingPolicy_TerseOutput, 0);

	CXString alt_name = clang_getCursorPrettyPrinted(cursor, policy);

	cf_print_info("CXCursor_MemberRefExpr "
			"clang_getCursorSpelling()='%s' "
			"clang_getCursorType()=%p, "
			"clang_getTypeSpelling()='%s' "
			"clang_getCursorPrettyPrinted()='%s' "
			"cursor.data[0]=%p"
			"\n",
			clang_getCString(cursor_name),
			ct.data[0],
			clang_getCString(name),
			clang_getCString(alt_name),
			data
			);

	clang_disposeString(alt_name);
	clang_PrintingPolicy_dispose(policy);
	clang_disposeString(name);
	clang_disposeString(cursor_name);
}

/*
 * Callback-based iterator similar to index_ast_node() below.
 */
static enum CXVisitorResult
print_struct_members(CXCursor cursor, CXClientData ctx_)
{
	member_ctx_t *ctx = ctx_;
	ctx->count++;

	inspect_member(cursor);

	// return CXVisit_Break;
	return CXVisit_Continue;
}
