#include "types.h"
#include "tables.h"

#include "os.h"

#include "gfx.h"
#include "gfx/gfx_types.h"

void load_or_reload_shader(GFX_Device *device, ShaderID id, GFX_Shader *shaders[SHADER_MAX], OS_Timestamp ts[SHADER_MAX]) {
	ShaderMetadata *metadata = &shader_to_metadata[id];
	if (metadata->filepaths[SHADER_STAGE_VERTEX].length == 0 &&
		metadata->filepaths[SHADER_STAGE_FRAGMENT].length == 0 &&
		metadata->filepaths[SHADER_STAGE_COMPUTE].length == 0)
		return;

	ArenaTemp scratch = arena_scratch_begin(NULL);
	bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length > 0;
	if (is_compute) {
		ts[id] = os_file_last_modified(metadata->filepaths[SHADER_STAGE_COMPUTE]);
		String8 bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_COMPUTE]);
		shaders[id] = gfx_compute_make(device, bytecode, (char *)shader_to_string[id].text);
	} else {
		String8 vs_bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_VERTEX]);
		String8 fs_bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_FRAGMENT]);

		OS_Timestamp fs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_FRAGMENT]);
		OS_Timestamp vs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_VERTEX]);

		ts[id] = MAX(fs_ts, vs_ts);
		shaders[id] = gfx_shader_make(device, vs_bytecode, fs_bytecode, (char *)shader_to_string[id].text);
		for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation)
			gfx_pipeline_ensure(device, shaders[id], metadata->pipelines[permutation]);
	}

	arena_scratch_end(scratch);
}
