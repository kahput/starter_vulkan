#include "core/arena.h"
#include "core/logger.h"
#include "core/strings.h"
#include "os.h"
#include <dlfcn.h>

OS_Library os_library_load(String path) {
	OS_Library result = NULL;
	ArenaTemp scratch = arena_scratch_begin(NULL);
	String cwd = os_current_directory(scratch.arena); // TODO: Internal header to get os__concat_cwd();
	result = dlopen(stringpath_join(scratch.arena, cwd, path).text, RTLD_NOW);

	if (result == NULL)
		LOG_WARN("os_library_load - %s", dlerror());

	arena_scratch_end(scratch);
	return result;
}

void os_library_unload(OS_Library lib) {
	if (os_library_valid(lib) == false)
		return;

	if (dlclose(lib) != 0)
		LOG_WARN("os_library_unload - %s", dlerror());
}

void os_library_symbol(OS_Library lib, String symbol, void *out_symbol) {
	bool error = false;
	if (out_symbol == NULL) {
		LOG_WARN("os_library_symbol - invalid out parameter passed.");
		error = true;
	}
	if (os_library_valid(lib) == false) {
		LOG_WARN("os_library_symbol - invalid library handle passed.");
		return;
	}

	if (error == false) {
		void *result = dlsym(lib, symbol.text);

		if (result == NULL)
			LOG_WARN("os_library_symbol - %s", dlerror());

		*(void **)out_symbol = result;
	}
}
