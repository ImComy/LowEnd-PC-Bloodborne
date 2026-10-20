/* CUSA00207 1.09 original region predicate: native Cross-confirm layout.
 * Verified read-only against the current mapped executable and native menu.
 * Check beyond the function prologue (32 bytes, by length and hash: code_check.h);
 * other predicate patches fail closed. */
static const BbCode bb_map_cross_region_guard={32,0xfaab51ea2797df47ull};
