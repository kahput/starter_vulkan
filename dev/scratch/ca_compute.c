#include <core.h>
#include "gpu/gpu_types.h"
#include <gpu.h>

int main(void) {
	log_level(LOG_DEBUG);
	Arena arena[] = { arena_make(MiB(128)) };

	GPU_Device device[] = { 0 };
	gpu_device_make(device);

	string8 bytecode = os_file_read(arena, s("resources/shaders/generated/c_BlurCompute.spv"));
	GPU_Shader *blur = gpu_compute_make(device, bytecode, "Blur");

	GPU_Image *image = gpu_image_make(device, 1280, 720,
		(ImageOptions){
		  .debug_name = "target",
		  .format = PIXEL_FORMAT_RGBA8_UNORM,
		  .usage =  IMAGE_USAGE_STORAGE | IMAGE_USAGE_SAMPLE,
		});

	gpu_device_destroy(device);
}
