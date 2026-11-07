#include "core/arena.h"
#include "core/logger.h"
#include "core/strings.h"
#include "core/os.h"
#include <dlfcn.h>
#include <stdlib.h>

OS_Library os_dlopen(string8 path) {
	OS_Library result = NULL;

	ArenaTemp scratch = arena_scratch_begin(NULL);

	bool rel = path.bytes[0] != '/';
	string8 fullpath = rel ? str8_pathjoin(scratch.arena, os_cwd(scratch.arena), path) : path;

	result = dlopen((char *)fullpath.bytes, RTLD_NOW);
	if (result == NULL) {
		log_warn("os_library_load - %s", dlerror());
	}

	arena_scratch_end(scratch);
	return result;
}

void os_dlclose(OS_Library lib) {
	if (os_library_valid(lib) == false) return;
	if (dlclose(lib) != 0) {
		log_warn("os_library_unload - %s", dlerror());
	}
}

bool os_dlsym(OS_Library lib, string8 symbol, void *out_symbol) {
	void *result = 0;

	bool ok = out_symbol;
	if (ok == false)
		log_warn("%s - invalid out parameter passed.", __func__);

	ok = ok && os_library_valid(lib);
	if (ok == false)
		log_warn("%s - invalid library handle passed.", __func__);

	if (ok) {
		result = dlsym(lib, (char *)symbol.bytes);

		ok = result;
		if (ok == false)
			log_warn("os_library_symbol - %s", dlerror());
	}

	if (ok)
		*(void **)out_symbol = result;

	return ok;
}

int32_t os_execute_command(string8 cmd) {
	return system((char *)cmd.bytes);
}
