// Stateless shim — all definitions live in nt_rack_shim.hpp via inline
// function-local statics. This .cpp is kept so the existing Makefile
// rules (and skel build script) still find a translation unit, but it
// emits no symbols.
//
// See nt_rack_shim.hpp for why we cannot use file-scope pointer-typed
// globals: the Disting NT plugin loader does not apply runtime
// relocations to a plugin's `.data.rel` section, nor run its
// `.init_array`, so any plugin-local pointer-to-global stays at 0.
#include "nt_rack_shim.hpp"
