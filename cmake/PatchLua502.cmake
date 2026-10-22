# Minimal host-compatibility fixes to the SHA256-pinned upstream source.
# No VM opcodes, number representation, bytecode checks or coroutine semantics
# change. Null union-member address expressions were benign on the old compiler
# but are undefined C and fail UBSan. Union/member pointers share the same address;
# direct casts also preserve null. Pointer hashing explicitly narrows uintptr_t.
function(bw_lua_replace path before after)
  file(READ "${path}" body)
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
