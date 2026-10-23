# Minimal host-compatibility fixes to the SHA256-pinned upstream source.
# No VM opcodes, number representation, bytecode checks or coroutine semantics
# change. Null union-member address expressions were benign on the old compiler
# but are undefined C and fail UBSan. Union/member pointers share the same address;
# direct casts also preserve null. Pointer hashing explicitly narrows uintptr_t.
function(bw_lua_replace path before after)
  file(READ "${path}" body)
  # Some exact replacement blocks retain their original declaration as a
  # prefix. Check the full result first so repeated configuration is idempotent.
  string(FIND "${body}" "${after}" patched)
  if(NOT patched EQUAL -1)
    return()
  endif()
  string(FIND "${body}" "${before}" found)
  if(found EQUAL -1)
    string(FIND "${body}" "${after}" patched)
    if(patched EQUAL -1)
      message(FATAL_ERROR "Unexpected pinned Lua source: ${path}")
    endif()
  else()
    string(REPLACE "${before}" "${after}" body "${body}")
    file(WRITE "${path}" "${body}")
  endif()
endfunction()
bw_lua_replace("${lua502_SOURCE_DIR}/src/lobject.h"
  "check_exp(ttistable(o), &(o)->value.gc->h)"
  "check_exp(ttistable(o), cast(Table *, (o)->value.gc))")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.h"
  "check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, &((o)->uv))"
  "check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, cast(UpVal *, (o)))")
bw_lua_replace("${lua502_SOURCE_DIR}/src/llimits.h"
  "#define IntPoint(p)  ((lu_hash)(p))"
  "#include <stdint.h>\n#define IntPoint(p)  ((lu_hash)(uintptr_t)(p))")

# Context-owned allocator extension for this exact 5.0.2 source. The callback is
# carried on each state; bootstrap/global teardown route explicitly before/after
# global_State exists. No process-global or TLS current allocator. Protected
# host calls reuse 5.0.2's own error recovery, not a later-version Lua API.
bw_lua_replace("${lua502_SOURCE_DIR}/include/lua.h"
  "typedef struct lua_State lua_State;"
  "typedef struct lua_State lua_State;\ntypedef void *(*lua_BWAlloc)(void *ud, void *pointer, size_t oldsize, size_t size);")
bw_lua_replace("${lua502_SOURCE_DIR}/include/lua.h"
  "LUA_API lua_State *lua_open (void);"
  "LUA_API lua_State *lua_open (void);\nLUA_API lua_State *lua_open_with_allocator(lua_BWAlloc alloc, void *ud);\nLUA_API int lua_bw_protected(lua_State *L, void (*function)(lua_State *, void *), void *ud);")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.h"
  "  CommonHeader;\n  StkId top;"
  "  CommonHeader;\n  lua_BWAlloc bw_alloc;\n  void *bw_alloc_ud;\n  StkId top;")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lmem.h"
  "void *luaM_realloc (lua_State *L, void *oldblock, lu_mem oldsize, lu_mem size);"
  "void *luaM_realloc (lua_State *L, void *oldblock, lu_mem oldsize, lu_mem size);\nvoid *luaM_rawalloc(lua_State *L, void *block, lu_mem oldsize, lu_mem size);")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lmem.c"
  "#define MINSIZEARRAY\t4"
  [=[/* BW: raw route also serves bootstrap/global state without GC accounting. */
void *luaM_rawalloc(lua_State *L, void *block, lu_mem oldsize, lu_mem size) {
  if (L && L->bw_alloc)
    return L->bw_alloc(L->bw_alloc_ud, block, oldsize, size);
  if (size == 0) {
    if (block) l_free(block, oldsize);
    return NULL;
  }
  return l_realloc(block, oldsize, size);
}
#define MINSIZEARRAY	4]=])
bw_lua_replace("${lua502_SOURCE_DIR}/src/lmem.c"
  "      l_free(block, oldsize);"
  "      luaM_rawalloc(L, block, oldsize, 0);")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lmem.c"
  "    block = l_realloc(block, oldsize, size);"
  "    block = luaM_rawalloc(L, block, oldsize, size);")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.c"
  [=[static lua_State *mallocstate (lua_State *L) {
  lu_byte *block = (lu_byte *)luaM_malloc(L, sizeof(lua_State) + EXTRASPACE);]=]
  [=[static lua_State *mallocstate (lua_State *L, lua_BWAlloc alloc, void *ud) {
  lu_byte *block = (lu_byte *)((L == NULL && alloc != NULL)
    ? alloc(ud, NULL, 0, sizeof(lua_State) + EXTRASPACE)
    : luaM_malloc(L, sizeof(lua_State) + EXTRASPACE));]=])
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.c"
  "  global_State *g = luaM_new(NULL, global_State);"
  "  global_State *g = cast(global_State *, luaM_rawalloc(L, NULL, 0, sizeof(global_State)));")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.c"
  "    luaM_freelem(NULL, G(L));"
  "    luaM_rawalloc(L, G(L), sizeof(global_State), 0);")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.c"
  "  freestate(NULL, L);"
  "  luaM_rawalloc(L, cast(lu_byte *, L) - EXTRASPACE, sizeof(lua_State) + EXTRASPACE, 0);")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.c"
  "  lua_State *L1 = mallocstate(L);"
  "  lua_State *L1 = mallocstate(L, NULL, NULL);")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.c"
  "  preinit_state(L1);\n  L1->l_G = L->l_G;"
  "  preinit_state(L1);\n  L1->bw_alloc = L->bw_alloc;\n  L1->bw_alloc_ud = L->bw_alloc_ud;\n  L1->l_G = L->l_G;")
bw_lua_replace("${lua502_SOURCE_DIR}/src/lstate.c"
  [=[LUA_API lua_State *lua_open (void) {
  lua_State *L = mallocstate(NULL);
  if (L) {  /* allocation OK? */]=]
  [=[LUA_API lua_State *lua_open (void) {
  return lua_open_with_allocator(NULL, NULL);
}

LUA_API lua_State *lua_open_with_allocator(lua_BWAlloc alloc, void *ud) {
  lua_State *L = mallocstate(NULL, alloc, ud);
  if (L) {  /* allocation OK? */
    L->bw_alloc = alloc;
    L->bw_alloc_ud = ud;]=])
bw_lua_replace("${lua502_SOURCE_DIR}/src/lapi.c"
  "const char lua_ident[] ="
  [=[/* BW: callback must not own C++ destructors across Lua's longjmp. */
LUA_API int lua_bw_protected(lua_State *L,
                            void (*function)(lua_State *, void *), void *ud) {
  int status;
  lua_lock(L);
  status = luaD_pcall(L, function, ud, savestack(L, L->top), 0);
  lua_unlock(L);
  return status;
}

const char lua_ident[] =]=])
