/* goal: test deferred anonymous types when an anonymous type (with an instance
 * variable name) is nested within an anonymous type definition (with a typedef
 * name). This requires `defer_anon_types` to be a list and not a singular
 * storage unit.
 *
 * Note: typedefs cannot be nested within struct definitions.
 */
typedef struct {
	struct {
		int a;
	} bar;
	int c;
} foo_t;
