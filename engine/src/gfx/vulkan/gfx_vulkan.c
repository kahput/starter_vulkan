#include "gfx.h"
#include "gfx/gfx_types.h"
#include "gfx/vulkan/tables.h"

#include "core/debug.h"
#include "core/logger.h"

#ifndef VK_USE_PLATFORM_XCB_KHR
	#define VK_USE_PLATFORM_XCB_KHR
#endif
#include <vulkan/vulkan.h>

#include <spirv_reflect/spirv_reflect.h>

static inline GFX_Pipeline *gfx__pipeline_alloc(GFX_Device *device) {
	GFX_Pipeline *result = 0;

	bool ok = gfx_device_valid(device) && (device->pipeline_count < MAX_PIPELINES || device->first_free_pipeline);
	ASSERT(ok);

	if (ok) { // acquire new shader
		if (device->first_free_pipeline) {
			result = device->first_free_pipeline;
			device->first_free_pipeline = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &device->pipeline_pool[device->pipeline_count];

		device->pipeline_count++;
	}

	return result;
}

// :helper
static bool gfx__validate_extensions(const char *required[], uint32_t required_count, VkExtensionProperties *available, uint32_t available_count);

static bool gfx__instance_make(GFX_Device *device);
static bool gfx__device_make(GFX_Device *device);
static bool gfx__frame_resources_make(GFX_Device *device);
static void gfx__load_debug_extensions(GFX_Device *device);

static inline uint32_t gfx__find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);
static inline VKAPI_ATTR VkBool32 VKAPI_CALL gfx__debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData);

PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger = STUB_vkCreateDebugUtilsMessenger;
PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessenger = STUB_vkDestroyDebugUtilsMessenger;
PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = STUB_vkSetDebugUtilsObjectName;
PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabel = STUB_vkCmdBeginDebugUtilsLabel;
PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabel = STUB_vkCmdEndDebugUtilsLabel;

VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {
	.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
	.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
	.pfnUserCallback = gfx__debug_callback,
	.pUserData = 0
};

static const char *required_device_extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// :res
bool gfx_reflect_shader_uniforms(String8 bytecode, UniformSet sets[GFX_LIMIT_UNIFORM_SETS]) {
	uint32_t set_count = 0;
	SpvReflectShaderModule module = { 0 };
	SpvReflectDescriptorSet **reflect_sets = 0;
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = sets;
	if (ok) { // reflect shader module
		ok = spvReflectCreateShaderModule(bytecode.length, bytecode.text, &module) == SPV_REFLECT_RESULT_SUCCESS;

		if (ok == false)
			LOG_ERROR("%s - failed to reflect shader bytecode.", __func__);
	}

	if (ok) { // enumerate descriptor sets
		ok = spvReflectEnumerateDescriptorSets(&module, &set_count, NULL) == SPV_REFLECT_RESULT_SUCCESS;

		if (ok) {
			reflect_sets = arena_push_count(scratch.arena, SpvReflectDescriptorSet *, set_count);
			ok = spvReflectEnumerateDescriptorSets(&module, &set_count, reflect_sets) == SPV_REFLECT_RESULT_SUCCESS;
		}
	}

	if (ok) {
	}

	if (ok) { // populate uniform metadata
		ASSERT(set_count <= GFX_LIMIT_UNIFORM_SETS);

		for (uint32_t set_index = 0; set_index < MIN(set_count, GFX_LIMIT_UNIFORM_SETS); ++set_index) {
			SpvReflectDescriptorSet *spv_set = reflect_sets[set_index];
			UniformSet *set = &sets[spv_set->set];

			for (uint32_t binding_index = 0; binding_index < spv_set->binding_count; ++binding_index) {
				SpvReflectDescriptorBinding *spv_binding = spv_set->bindings[binding_index];

				int32_t existing_index = -1;
				for (uint32_t search_index = 0; search_index < set->uniform_count; ++search_index) {
					if (set->uniforms[search_index].binding == spv_binding->binding) {
						existing_index = search_index;
						break;
					}
				}

				Uniform *uniform = &set->uniforms[existing_index != -1 ? (uint32_t)existing_index : set->uniform_count++];

				memory_copy(uniform->name, spv_binding->name, MIN(sizeof(uniform->name), str8_wrap(spv_binding->name).length));
				if (spv_binding->descriptor_type >= SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
					spv_binding->descriptor_type <= SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC &&
					spv_binding->name[0] == 0) {
					String8 name = str8_wrap(spv_binding->block.member_count == 1 ? spv_binding->block.members[0].name : spv_binding->block.name);
					memory_copy(uniform->name, name.text, MIN(sizeof(uniform->name) - 1, name.length));
				}
				ASSERT(uniform->name[0] != 0);

				uniform->binding = spv_binding->binding;
				uniform->count = spv_binding->count;

				ASSERT(spv_binding->descriptor_type < countof(uniform_type_from_vulkan_descriptor_type));
				uniform->type = uniform_type_from_vulkan_descriptor_type[(VkDescriptorType)spv_binding->descriptor_type];
			}
		}
	}

	if (ok == false)
		memory_zero(sets, sizeof(UniformSet[GFX_LIMIT_UNIFORM_SETS]));

	arena_scratch_end(scratch);
	spvReflectDestroyShaderModule(&module);

	return ok;
}

GFX_Buffer *gfx_buffer_make(GFX_Device *device, uint64_t size, BufferOptions options) {
	GFX_Buffer *result = 0;

	bool ok = gfx_device_valid(device) && (device->buffer_count < MAX_BUFFERS || device->first_free_buffer);
	const char *name = options.debug_name ? options.debug_name : "<unnamed_buffer>";

	LOG_DEBUG("creating '%s' buffer.", name);
	if (ok) { // acquire new buffer
		if (device->first_free_buffer) {
			result = device->first_free_buffer;
			device->first_free_buffer = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &device->buffer_pool[device->buffer_count];

		device->buffer_count++;
	}

	VkBufferUsageFlags vk_usage = 0;
	VkMemoryPropertyFlags memory_flags = 0;
	if (ok) {
		result->options = options;

		vk_usage = buffer_options_to_vulkan_buffer_usage_flags(options);
		memory_flags = memory_type_to_vulkan_memory_flags[options.memory];

		ok = vk_usage && memory_flags;
		if (ok == false) {
			LOG_WARN("invalid properties passed, aborting creation of %s", name);
		}
	}

	if (ok) { // create buffer handle
		result->size = size;
		uint32_t family_indices[] = { device->graphics_index, device->transfer_index };
		result->info = (VkBufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = result->size,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		ok = vkCreateBuffer(device->handle, &result->info, 0, &result->handle) == VK_SUCCESS;
	}

	if (ok) { // allocate buffer memory
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(device->handle, result->handle, &memory_requirements);

		uint32_t memory_type_index = gfx__find_memory_type(device->info.gpu, memory_requirements.memoryTypeBits, memory_flags);
		size_t allocation_size = memory_requirements.size;

		VkMemoryAllocateFlagsInfo allocate_flag_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
			.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
		};

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = &allocate_flag_info,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = memory_type_index,
		};

		ok = vkAllocateMemory(device->handle, &allocate_info, 0, &result->memory) == VK_SUCCESS;
	}

	if (ok) { // bind memory & get buffer device address
		ok = vkBindBufferMemory(device->handle, result->handle, result->memory, 0) == VK_SUCCESS;

		if (vk_usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
			VkBufferDeviceAddressInfo bda_info = {
				.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
				.buffer = result->handle,
			};
			result->address = vkGetBufferDeviceAddress(device->handle, &bda_info);
		}
	}

	if (ok && options.data) { // upload
		GFX_Command *cmd = gfx_transfer_cmd(device);
		gfx_cmd_buffer_upload(cmd, result, 0, size, options.data);
	}

#if DEV_BUILD
	if (ok) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_BUFFER,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(device->handle, &name_info);
	}
#endif

	if (ok == false) // remove half-made resources on error
		gfx_buffer_destroy(device, result);

	return result;
}

GFX_Image *gfx_image_make(GFX_Device *device, uint32_t width, uint32_t height, ImageOptions options) {
	GFX_Image *result = 0;

	bool ok = gfx_device_valid(device) && device->image_count < MAX_IMAGES;
	const char *name = options.debug_name ? options.debug_name : "<unnamed_image>";

	LOG_DEBUG("creating '%s' image.", name);
	if (ok) { // acquire new image
		if (device->first_free_image) {
			result = device->first_free_image;
			device->first_free_image = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &device->image_pool[device->image_count];

		result->imageid = indexof(device->image_pool, result);
		device->image_count++;
	}

	uint32_t layer_count = image_options_to_vulkan_layer_count(options);
	VkFormat vk_format = pixel_format_to_vulkan_format[options.format];

	if (ok) { // make vulkan image handle
		VkImageUsageFlags vk_usage = image_options_to_vulkan_image_usage_flags(options);
		VkSampleCountFlags vk_sample = image_options_to_vulkan_sample_count(device->info.limits, options);
		VkImageCreateFlags vk_flags = image_options_to_vulkan_image_flags(options);

		result->width = width, result->height = height;
		result->options = options;
		result->res_usage = RESOURCE_USAGE_UNDEFINED;
		options.max_mip_level = options.max_mip_level ? options.max_mip_level : 1;
		result->miplevels = MIN(options.max_mip_level, (uint32_t)log2(maxf(width, height)) + 1);

		VkImageCreateInfo image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.flags = vk_flags,
			.format = vk_format,
			.extent = {
			  .width = width,
			  .height = height,
			  .depth = 1,
			},
			.mipLevels = result->miplevels,
			.arrayLayers = layer_count,
			.samples = vk_sample,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = resource_usage_to_vulkan_image_layout[result->res_usage],
		};

		ok = vkCreateImage(device->handle, &image_info, 0, &result->handle) == VK_SUCCESS;
	}

	if (ok) { // allocate memory
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(device->handle, result->handle, &memory_requirements);

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = gfx__find_memory_type(device->info.gpu, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
		};

		ok = vkAllocateMemory(device->handle, &allocate_info, 0, &result->memory) == VK_SUCCESS;
	}

	if (ok) // bind memory to handle
		vkBindImageMemory(device->handle, result->handle, result->memory, 0);

	if (ok) { // create image view
		VkImageViewType vk_type = image_type_to_vulkan_image_view_type[options.type];
		VkImageAspectFlags vk_aspect = image_options_to_vulkan_aspect_flags(options);

		VkImageViewCreateInfo view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = result->handle,
			.viewType = vk_type,
			.format = vk_format,
			.components = {
			  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
			  .aspectMask = vk_aspect,
			  .baseMipLevel = 0,
			  .levelCount = result->miplevels,
			  .baseArrayLayer = 0,
			  .layerCount = layer_count,
			}
		};

		ok = vkCreateImageView(device->handle, &view_info, 0, &result->view) == VK_SUCCESS;
	}

	if (ok && options.pixels) { // upload
		GFX_Command *cmd = gfx_transfer_cmd(device);

		gfx_cmd_image_upload(cmd, result, result->width, result->height, options.pixels);

		VkFormatProperties format_properties;
		vkGetPhysicalDeviceFormatProperties(device->info.gpu, vk_format, &format_properties);
		bool blit_support = format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
		if (blit_support == false) {
			LOG_WARN("pixel format (%s) does not support linear blitting.", pixel_format_to_string[options.format]);
		}

		if (result->miplevels > 1 && blit_support) {
			uint32_t mip_width = width, mip_height = height;
			VkImageAspectFlags aspect = image_options_to_vulkan_aspect_flags(options);

			for (uint32_t level = 1; level < result->miplevels; ++level) {
				gfx_cmd_image_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_TRANSFER_SRC, level - 1, 1, result);

				VkImageBlit blit = { 0 };

				VkImageBlit blit_info = {
					.srcOffsets[1] = {
					  .x = mip_width,
					  .y = mip_height,
					  .z = 1,
					},
					.srcSubresource = {
					  .aspectMask = aspect,
					  .baseArrayLayer = 0,
					  .layerCount = layer_count,
					  .mipLevel = level - 1,
					},
					.dstOffsets[1] = {
					  .x = mip_width > 1 ? mip_width / 2 : 1,
					  .y = mip_height > 1 ? mip_height / 2 : 1,
					  .z = 1,
					},
					.dstSubresource = {
					  .aspectMask = aspect,
					  .baseArrayLayer = 0,
					  .layerCount = layer_count,
					  .mipLevel = level,
					},
				};

				vkCmdBlitImage(cmd->handle,
					result->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					result->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &blit_info, VK_FILTER_LINEAR);

				if (mip_width > 1)
					mip_width /= 2;
				if (mip_height > 1)
					mip_height /= 2;
			}
		}
		gfx_cmd_image_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_TRANSFER_SRC, result->miplevels - 1, 1, result);
		gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, result);
	}

#if DEV_BUILD
	if (ok) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_IMAGE,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(device->handle, &name_info);
	}
#endif

	if (ok == false) // remove half-made resources on error
		gfx_image_destroy(device, result);

	/* LOG_INFO("image loaded successfuly (%ux%u | %s)", indexof(device->image_pool, image), width, height, image_format_to_string[format]); */
	return result;
}

GFX_Sampler *gfx_sampler_make(GFX_Device *device, SamplerOptions options) {
	GFX_Sampler *result = 0;

	bool ok = gfx_device_valid(device) && (device->sampler_count < MAX_SAMPLERS || device->first_free_sampler);
	const char *name = options.debug_name ? options.debug_name : "<unnamed_sampler>";

	LOG_DEBUG("creating '%s' sampler.", name);
	if (ok) { // acquire new sampler
		if (device->first_free_sampler) {
			result = device->first_free_sampler;
			device->first_free_sampler = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &device->sampler_pool[device->sampler_count];

		device->sampler_count++;
	}

	if (ok) {
		result->info = (VkSamplerCreateInfo){
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = (VkFilter)options.mag_filter,
			.minFilter = (VkFilter)options.min_filter,
			.mipmapMode = (VkSamplerMipmapMode)options.mipmap_filter,
			.addressModeU = (VkSamplerAddressMode)options.address_mode_u,
			.addressModeV = (VkSamplerAddressMode)options.address_mode_v,
			.addressModeW = (VkSamplerAddressMode)options.address_mode_w,
			.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
			.anisotropyEnable = VK_TRUE,
			.maxAnisotropy = device->info.limits.maxSamplerAnisotropy,
			.compareEnable = options.compare_enable,
			.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.maxLod = VK_LOD_CLAMP_NONE
		};

		ok = vkCreateSampler(device->handle, &result->info, NULL, &result->handle) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create vulkan sampler.");
		}
	}

#if DEV_BUILD
	if (ok) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_SAMPLER,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(device->handle, &name_info);
	}
#endif

	if (ok == false) // remove half-made resources on error
		gfx_sampler_destroy(device, result);

	return result;
}

GFX_Shader *gfx_compute_make(GFX_Device *device, String8 bytecode, const char *debug_name) {
	GFX_Shader *result = 0;
	GFX_Pipeline *pipeline = 0;
	uint32_t set_count = 0;

	bool ok = gfx_device_valid(device) && (device->shader_count < MAX_SHADERS || device->first_free_shader);
	const char *name = debug_name ? debug_name : "<unnamed_compute>";

	LOG_DEBUG("creating '%s' compute shader.", name);
	if (ok) { // acquire new shader
		if (device->first_free_shader) {
			result = device->first_free_shader;
			device->first_free_shader = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &device->shader_pool[device->shader_count];

		result->debug_name = name;
		device->shader_count++;

		ok = bytecode.text && bytecode.length > 0;
		if (ok == false)
			LOG_WARN("%s - invalid shader bytecode passed.", __func__);
	}

	if (ok) { // create shader module
		VkShaderModuleCreateInfo csm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)bytecode.text,
			.codeSize = bytecode.length,
		};

		ok = vkCreateShaderModule(device->handle, &csm_create_info, NULL, &result->modules[SHADER_STAGE_COMPUTE]) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create '%s' module.", name);
	}

	if (ok) { // create descriptor set layouts
		ok = gfx_reflect_shader_uniforms(bytecode, result->reflection.sets);

		VkDescriptorSetLayoutBinding bindings[32] = { 0 };
		for (uint32_t set_index = 0; set_index < GFX_LIMIT_UNIFORM_SETS && ok; ++set_index) {
			UniformSet *set = &result->reflection.sets[set_index];
			if (set->uniform_count == 0)
				continue;
			uniforms_to_vulkan_descriptor_bindings(set->uniforms, set->uniform_count, bindings, VK_SHADER_STAGE_COMPUTE_BIT);

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = set->uniform_count,
				.pBindings = bindings,
			};
			ok &= vkCreateDescriptorSetLayout(device->handle, &dsl_create_info, 0, &result->layouts[set_count]) == VK_SUCCESS;

			set_count = ok ? set_count + 1 : set_count;
		}
	}

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = set_count,
			.pSetLayouts = result->layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &device->global_range,
		};

		ok = vkCreatePipelineLayout(device->handle, &pl_create_info, NULL, &result->layout) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create compute pipeline layout.");
		}
	}

	if (ok) { // create compute pipeline
		pipeline = gfx__pipeline_alloc(device);
		ok = pipeline != 0;
	}

	if (ok) { // create default pipeline for compute shader
		pipeline->next = result->first_pipeline;
		result->first_pipeline = pipeline;

		pipeline->shader = result;

		VkPipelineShaderStageCreateInfo compute_stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = result->modules[SHADER_STAGE_COMPUTE],
			.pName = "main",
		};

		VkComputePipelineCreateInfo cp_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = compute_stage,
			.layout = result->layout,
		};

		ok = vkCreateComputePipelines(device->handle, 0, 1, &cp_create_info, NULL, &pipeline->handle) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create compute pipeline.");
		}
	}

	if (ok == false) // remove half-made resources on error
		gfx_shader_destroy(device, result);

	return result;
}

GFX_Shader *gfx_shader_make(GFX_Device *device, String8 vs_bytecode, String8 fs_bytecode, const char *debug_name) {
	GFX_Shader *result = 0;
	const char *name = debug_name ? debug_name : "<unnamed_raster>";
	uint32_t set_count = 0;

	bool ok = gfx_device_valid(device);
	if (ok) { // check validitiy of shader code
		ok &= vs_bytecode.text && vs_bytecode.length > 0;
		ok &= fs_bytecode.text && fs_bytecode.length > 0;

		if (ok == false) {
			LOG_WARN("invalid shader bytecode passed.");
		}
	}

	if (ok) { // acquire new shader
		LOG_DEBUG("creating '%s' grahpics shader.", name);

		if (device->first_free_shader) {
			result = device->first_free_shader;
			device->first_free_shader = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &device->shader_pool[device->shader_count];

		result->debug_name = name;
		device->shader_count++;
	}

	if (ok) { // create shader module
		VkShaderModuleCreateInfo vsm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)vs_bytecode.text,
			.codeSize = vs_bytecode.length,
		};

		VkShaderModuleCreateInfo fsm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)fs_bytecode.text,
			.codeSize = fs_bytecode.length,
		};

		ok &= vkCreateShaderModule(device->handle, &vsm_create_info, NULL, &result->modules[SHADER_STAGE_VERTEX]) == VK_SUCCESS;
		ok &= vkCreateShaderModule(device->handle, &fsm_create_info, NULL, &result->modules[SHADER_STAGE_FRAGMENT]) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create vertex/fragment shader module.");
		}
	}

	if (ok) { // create descriptor set layouts
		gfx_reflect_shader_uniforms(fs_bytecode, result->reflection.sets);
		gfx_reflect_shader_uniforms(vs_bytecode, result->reflection.sets);

		VkDescriptorSetLayoutBinding bindings[32] = { 0 };
		for (uint32_t set_index = 0; set_index < GFX_LIMIT_UNIFORM_SETS; ++set_index) {
			UniformSet *set = &result->reflection.sets[set_index];
			if (set->uniform_count == 0)
				continue;
			uniforms_to_vulkan_descriptor_bindings(set->uniforms, set->uniform_count, bindings, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT);

			VkDescriptorSetLayoutCreateInfo dsl_create_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = set->uniform_count,
				.pBindings = bindings,
			};
			ok &= vkCreateDescriptorSetLayout(device->handle, &dsl_create_info, 0, &result->layouts[set_count]) == VK_SUCCESS;

			set_count = ok ? set_count + 1 : set_count;
		}
	}

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = set_count,
			.pSetLayouts = result->layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &device->global_range,
		};

		ok = vkCreatePipelineLayout(device->handle, &pl_create_info, NULL, &result->layout) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create shader '%s' pipeline layout.", name);
		}
	}

	if (ok == false) // remove half-made resources on error
		gfx_shader_destroy(device, result);

	return result;
}

GFX_Pipeline *gfx_pipeline_ensure(GFX_Device *device, GFX_Shader *shader, PipelineOptions options) {
	GFX_Pipeline *result = 0;
	const char *name = 0;
	uint32_t set_count = 0;
	bool match_found = false;

	bool ok = gfx_shader_valid(device, shader);
	if (ok) {
		name = shader->debug_name ? shader->debug_name : "<unnamed_raster>";

		for (GFX_Pipeline *pipeline = shader->first_pipeline; pipeline && pipeline != device->pipeline_pool; pipeline = pipeline->next) {
			if (memory_equals(&options, &pipeline->options, sizeof(PipelineOptions))) {
				result = pipeline;
				match_found = true;
				break;
			}
		}
	}

	if (ok && match_found == false) { // acquire new shader
		result = gfx__pipeline_alloc(device);
	}

	if (ok && match_found == false) { // create graphics pipeline
		VkPipelineShaderStageCreateInfo shader_stages[] = {
			{
			  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			  .stage = VK_SHADER_STAGE_VERTEX_BIT,
			  .module = shader->modules[SHADER_STAGE_VERTEX],
			  .pName = "main",
			},
			{
			  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			  .module = shader->modules[SHADER_STAGE_FRAGMENT],
			  .pName = "main",
			}
		};

		VkDynamicState dynamic_states[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo ds_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = countof(dynamic_states),
			.pDynamicStates = dynamic_states
		};

		VkPipelineVertexInputStateCreateInfo vis_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		};

		VkPipelineInputAssemblyStateCreateInfo ias_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.primitiveRestartEnable = VK_FALSE
		};

		VkPipelineViewportStateCreateInfo vps_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		VkPipelineRasterizationStateCreateInfo rs_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.lineWidth = 1.0f,
			.cullMode = options.cull_mode,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
		};

		VkPipelineMultisampleStateCreateInfo mss_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.sampleShadingEnable = VK_FALSE,
			.rasterizationSamples = options.sample_count ? (VkSampleCountFlags)options.sample_count : VK_SAMPLE_COUNT_1_BIT,
			.alphaToCoverageEnable = options.color_attachment_count && options.sample_count > 1 ? VK_TRUE : VK_FALSE,
			.minSampleShading = 1.0f,
		};

		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = options.disable_depth_test == false,
			.depthWriteEnable = options.disable_depth_write == false,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.depthBoundsTestEnable = VK_FALSE,
			.minDepthBounds = 0.0f,
			.maxDepthBounds = 1.0f
		};

		VkPipelineColorBlendAttachmentState color_attachment_blends[GFX_LIMIT_COLOR_ATTACHMENTS] = { 0 };
		VkPipelineColorBlendStateCreateInfo cbs_create_info = { 0 };

		VkFormat color_attachment_formats[GFX_LIMIT_COLOR_ATTACHMENTS] = { 0 };
		VkPipelineRenderingCreateInfo r_create_info = { 0 };

		{ // attachment state
			VkColorComponentFlags rgba_write_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendAttachmentState color_attachment_blend_default = {
				.colorWriteMask = rgba_write_mask,
				.blendEnable = options.enable_blend,

				// Color: result = src.rgb + (0.0 * dst.rgb)
				.srcColorBlendFactor = (VkBlendFactor)options.src_color_factor,
				.dstColorBlendFactor = (VkBlendFactor)options.dst_color_factor,
				.colorBlendOp = VK_BLEND_OP_ADD,

				.srcAlphaBlendFactor = (VkBlendFactor)options.src_alpha_factor,
				.dstAlphaBlendFactor = (VkBlendFactor)options.dst_alpha_factor,
				.alphaBlendOp = VK_BLEND_OP_ADD,
			};
			for (uint32_t index = 0; index < options.color_attachment_count; ++index)
				color_attachment_blends[index] = color_attachment_blend_default;

			cbs_create_info = (VkPipelineColorBlendStateCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
				.logicOpEnable = VK_FALSE,
				.attachmentCount = options.color_attachment_count,
				.pAttachments = color_attachment_blends,
			};

			for (uint32_t attachment_index = 0; attachment_index < options.color_attachment_count; ++attachment_index)
				color_attachment_formats[attachment_index] = pixel_format_to_vulkan_format[options.color_attachments[attachment_index]];

			r_create_info = (VkPipelineRenderingCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
				.colorAttachmentCount = options.color_attachment_count,
				.pColorAttachmentFormats = color_attachment_formats,
				.depthAttachmentFormat =
					pixel_format_is_depth(options.depth_attachment)
					? pixel_format_to_vulkan_format[options.depth_attachment]
					: pixel_format_to_vulkan_format[PIXEL_FORMAT_DEPTH],
			};
		}

		VkGraphicsPipelineCreateInfo gp_create_info = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = &r_create_info,
			.stageCount = countof(shader_stages),
			.pStages = shader_stages,
			.pVertexInputState = &vis_create_info,
			.pInputAssemblyState = &ias_create_info,
			.pViewportState = &vps_create_info,
			.pRasterizationState = &rs_create_info,
			.pMultisampleState = &mss_create_info,
			.pDepthStencilState = &depth_stencil_create_info,
			.pColorBlendState = &cbs_create_info,
			.pDynamicState = &ds_create_info,
			.layout = shader->layout,
		};

		ok = vkCreateGraphicsPipelines(device->handle, 0, 1, &gp_create_info, NULL, &result->handle) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create compute pipeline.");
	}

	if (ok && match_found == false) { // attach to shader
		result->next = shader->first_pipeline;
		shader->first_pipeline = result;

		result->shader = shader;
	}

#if DEV_BUILD
	if (ok && match_found == false) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_PIPELINE,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(device->handle, &name_info);
	}
#endif

	if (ok == false) { // remove half-made resources on error
		gfx_pipeline_destroy(device, result);
	}

	return result;
}

GFX_Swapchain *gfx_swapchain_make(GFX_Device *device, OS_Surface *surface, const char *debug_name) {
	GFX_Swapchain *result = 0;
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = gfx_device_valid(device) && (device->swapchain_count < MAX_SWAPCHAINS || device->first_free_swapchain);
	const char *name = debug_name ? debug_name : "<unnamed_swapchain>";

	LOG_DEBUG("creating '%s' swapchain.", name);
	if (ok) { // acquire new swapchain
		if (device->first_free_swapchain) {
			result = device->first_free_swapchain;
			device->first_free_swapchain = result->next;
			result->next = 0;

			memory_zero(result, sizeof(*result));
		} else
			result = &device->swapchain_pool[device->swapchain_count];

		result->native = surface;
		device->swapchain_count++;
	}

	if (ok) { // create surface
		VkXcbSurfaceCreateInfoKHR surface_create_info = {
			.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
			.connection = os_native_display_handle(),
			.window = (uint32_t)(uint64_t)os_native_surface_handle(surface),
		}; // TODO: Have os decide this

		ok = vkCreateXcbSurfaceKHR(device->instance, &surface_create_info, 0, &result->surface) == VK_SUCCESS;
		if (ok == false) {
			LOG_WARN("failed to create vulkan surface.");
		}
	}

	VkSurfaceCapabilitiesKHR capabilities;

	uint32_t surface_format_count = 0;
	VkSurfaceFormatKHR *surface_formats;

	uint32_t present_mode_count = 0;
	VkPresentModeKHR *present_modes;

	if (ok) { // query surface suitability
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->info.gpu, result->surface, &capabilities);

		vkGetPhysicalDeviceSurfaceFormatsKHR(device->info.gpu, result->surface, &surface_format_count, 0);
		surface_formats = arena_push_count(scratch.arena, VkSurfaceFormatKHR, surface_format_count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device->info.gpu, result->surface, &surface_format_count, surface_formats);

		vkGetPhysicalDeviceSurfacePresentModesKHR(device->info.gpu, result->surface, &present_mode_count, 0);
		present_modes = arena_push_count(scratch.arena, VkPresentModeKHR, present_mode_count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device->info.gpu, result->surface, &present_mode_count, present_modes);

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device->info.gpu, &queue_family_count, 0);
		VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(device->info.gpu, &queue_family_count, queue_family_properties);

		if (device->present_index == -1)
			for (uint32_t index = 0; index < queue_family_count; ++index) {
				VkQueueFlags flags = queue_family_properties[index].queueFlags;

				VkBool32 present_support = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(device->info.gpu, index, result->surface, &present_support);
				if (present_support && device->present_index == -1) {
					device->present_index = index;
					break;
				}
			}

		ok &= surface_format_count > 0; // valid surface formats available
		ok &= present_mode_count > 0; // valid present mode available
		ok &= device->present_index != -1; // supports present queue
	}

	if (ok) { // create swapchain
		vkGetDeviceQueue(device->handle, device->present_index, 0, &device->present_queue);

		VkSurfaceFormatKHR selected_format = surface_formats[0];
		for (uint32_t format_index = 0; format_index < surface_format_count; ++format_index) {
			if (surface_formats[format_index].format == VK_FORMAT_B8G8R8A8_UNORM &&
				surface_formats[format_index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { // ideal format
				selected_format = surface_formats[format_index];
				break;
			}
		}

		VkPresentModeKHR selected_present_mode = present_modes[0];
		for (uint32_t mode_index = 0; mode_index < present_mode_count; mode_index++) {
			// NOTE: Caps framerate to monitor framerate
			/* selected_present_mode = VK_PRESENT_MODE_FIFO_KHR; */
			/* break; */

			// NOTE: Uncap framerate on XWayland
			/* if (present_modes[mode_index] == VK_PRESENT_MODE_IMMEDIATE_KHR) { */
			/* 	selected_present_mode = present_modes[mode_index]; */
			/* 	break; */
			/* } */
			if (present_modes[mode_index] == VK_PRESENT_MODE_MAILBOX_KHR) // ideal presentation mode
				selected_present_mode = present_modes[mode_index];
		}

		uint32x2 surface_size = os_surface_size(surface);
		float dpi = os_surface_dpi(surface);

		VkExtent2D selected_extents =
			capabilities.currentExtent.width != UINT32_MAX
			? capabilities.currentExtent
			: (VkExtent2D){ .width = (uint32_t)(surface_size.x * dpi), .height = (uint32_t)((float)surface_size.y * dpi) };

		uint32_t queue_family_indices[] = { (uint32_t)device->graphics_index, (uint32_t)device->present_index };

		uint32_t image_count = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
			image_count = capabilities.maxImageCount;

		image_count = MIN(image_count, SWAPCHAIN_IMAGE_COUNT);

		result->info = (VkSwapchainCreateInfoKHR){
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = result->surface,
			.minImageCount = image_count,
			.imageFormat = selected_format.format,
			.imageColorSpace = selected_format.colorSpace,
			.imageExtent = selected_extents,
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.preTransform = capabilities.currentTransform,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = selected_present_mode,
			.clipped = VK_TRUE,
		};

		if (queue_family_indices[0] != queue_family_indices[1]) {
			result->info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			result->info.queueFamilyIndexCount = 2;
			result->info.pQueueFamilyIndices = queue_family_indices;
		} else
			result->info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

		ok = vkCreateSwapchainKHR(device->handle, &result->info, NULL, &result->handle) == VK_SUCCESS;
	}

	if (ok) { // get the swapchain images & create image views
		vkGetSwapchainImagesKHR(device->handle, result->handle, &result->image_count, NULL);
		vkGetSwapchainImagesKHR(device->handle, result->handle, &result->image_count, result->images);

		for (uint32_t image_index = 0; image_index < result->image_count; ++image_index) {
			result->view_infos[image_index] = (VkImageViewCreateInfo){
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = result->images[image_index],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = result->info.imageFormat,
				.components = {
				  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
				  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
				  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
				  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
				},
				.subresourceRange = {
				  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				  .baseMipLevel = 0,
				  .levelCount = 1,
				  .baseArrayLayer = 0,
				  .layerCount = 1,
				}
			};

			ok &= vkCreateImageView(device->handle, result->view_infos + image_index, NULL, &result->views[image_index]) == VK_SUCCESS;
		}
	}

	if (ok) { // fill in defaults for wrapper
		GFX_Image *wrapper = &result->wrapper;

		wrapper->options = (ImageOptions){
			.debug_name = name,
			.format = PIXEL_FORMAT_BGRA8_UNORM,
			.sample = SAMPLE_COUNT_1,
			.type = IMAGE_TYPE_2D,
			.usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER,
		};
		wrapper->miplevels = 1;
		wrapper->width = result->info.imageExtent.width;
		wrapper->height = result->info.imageExtent.height;
	}

	if (ok) { // create semaphores
		VkSemaphoreCreateInfo s_create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		for (uint32_t index = 0; index < countof(result->image_available_semaphores); ++index)
			ok &= vkCreateSemaphore(device->handle, &s_create_info, 0, result->image_available_semaphores + index) == VK_SUCCESS;
		for (uint32_t index = 0; index < countof(result->render_done_semaphores); ++index)
			ok &= vkCreateSemaphore(device->handle, &s_create_info, 0, result->render_done_semaphores + index) == VK_SUCCESS;
	}

#if DEV_BUILD
	if (ok) { // assign debug name
		VkDebugUtilsObjectNameInfoEXT name_info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.pObjectName = name,
			.objectType = VK_OBJECT_TYPE_SWAPCHAIN_KHR,
			.objectHandle = (uint64_t)result->handle,

		};
		vkSetDebugUtilsObjectName(device->handle, &name_info);
	}
#endif

	if (ok == false) { // remove half-made resources on error
		gfx_swapchain_destroy(device, result);
		LOG_ERROR("failed to create graphics surface.");
	}

	arena_scratch_end(scratch);
	return result;
}

bool gfx_buffer_destroy(GFX_Device *device, GFX_Buffer *buffer) {
	bool ok = gfx_buffer_valid(device, buffer) && buffer->handle;
	if (ok) {
		if (buffer->memory) {
			if (buffer->mapped)
				vkUnmapMemory(device->handle, buffer->memory);
			vkFreeMemory(device->handle, buffer->memory, 0);
		}
		vkDestroyBuffer(device->handle, buffer->handle, 0);

		device->buffer_count--;
		memory_zero(buffer, sizeof(*buffer));

		buffer->next = device->first_free_buffer;
		device->first_free_buffer = buffer;
	}

	return ok;
}

bool gfx_image_destroy(GFX_Device *device, GFX_Image *image) {
	bool ok = gfx_image_valid(device, image) && image->handle;
	if (ok) {
		if (image->view)
			vkDestroyImageView(device->handle, image->view, 0);
		if (image->memory)
			vkFreeMemory(device->handle, image->memory, 0);
		if (image->handle)
			vkDestroyImage(device->handle, image->handle, 0);

		device->image_count--;
		memory_zero(image, sizeof(*image));

		image->next = device->first_free_image;
		device->first_free_image = image;
	}

	return ok;
}

bool gfx_sampler_destroy(GFX_Device *device, GFX_Sampler *sampler) {
	bool ok = gfx_sampler_valid(device, sampler) && sampler->handle;
	if (ok) {
		if (sampler->handle)
			vkDestroySampler(device->handle, sampler->handle, NULL);
		device->sampler_count--;
		memory_zero(sampler, sizeof(*sampler));

		sampler->next = device->first_free_sampler;
		device->first_free_sampler = sampler;
	}

	return ok;
}

bool gfx_shader_destroy(GFX_Device *device, GFX_Shader *shader) {
	bool ok = gfx_shader_valid(device, shader) && (shader->modules[0] || shader->modules[1] || shader->modules[2]);
	if (ok) {
		for (uint32_t index = 0; index < countof(shader->modules); ++index)
			if (shader->modules[index])
				vkDestroyShaderModule(device->handle, shader->modules[index], NULL);
		for (uint32_t set_index = 0; set_index < countof(shader->layouts); ++set_index)
			if (shader->layouts[set_index])
				vkDestroyDescriptorSetLayout(device->handle, shader->layouts[set_index], NULL);

		if (shader->layout)
			vkDestroyPipelineLayout(device->handle, shader->layout, NULL);

		for (GFX_Pipeline *pipeline = shader->first_pipeline; pipeline && pipeline != device->pipeline_pool;) {
			GFX_Pipeline *curr = pipeline;
			pipeline = curr->next;

			gfx_pipeline_destroy(device, curr);
		}

		device->shader_count--;
		memory_zero(shader, sizeof(*shader));

		shader->next = device->first_free_shader;
		device->first_free_shader = shader;
	}

	return ok;
}

bool gfx_pipeline_destroy(GFX_Device *device, GFX_Pipeline *pipeline) {
	bool ok = gfx_pipeline_valid(device, pipeline) && pipeline->handle;
	if (ok) {
		if (pipeline->handle)
			vkDestroyPipeline(device->handle, pipeline->handle, NULL);

		device->pipeline_count--;
		memory_zero(pipeline, sizeof(*pipeline));

		pipeline->next = device->first_free_pipeline;
		device->first_free_pipeline = pipeline;
	}

	return ok;
}

bool gfx_swapchain_destroy(GFX_Device *device, GFX_Swapchain *swapchain) {
	bool ok = gfx_swapchain_valid(device, swapchain) && swapchain->handle;
	if (ok) {
		for (uint32_t semaphore_index = 0; semaphore_index < countof(swapchain->image_available_semaphores); ++semaphore_index)
			if (swapchain->image_available_semaphores[semaphore_index])
				vkDestroySemaphore(device->handle, swapchain->image_available_semaphores[semaphore_index], NULL);
		for (uint32_t semaphore_index = 0; semaphore_index < countof(swapchain->render_done_semaphores); ++semaphore_index)
			if (swapchain->render_done_semaphores[semaphore_index])
				vkDestroySemaphore(device->handle, swapchain->render_done_semaphores[semaphore_index], NULL);

		for (uint32_t image_index = 0; image_index < swapchain->image_count; ++image_index)
			if (swapchain->views[image_index])
				vkDestroyImageView(device->handle, swapchain->views[image_index], NULL);

		if (swapchain->handle)
			vkDestroySwapchainKHR(device->handle, swapchain->handle, NULL);
		if (swapchain->surface)
			vkDestroySurfaceKHR(device->instance, swapchain->surface, NULL);

		device->swapchain_count--;
		memory_zero(swapchain, sizeof(*swapchain));

		swapchain->next = device->first_free_swapchain;
		device->first_free_swapchain = swapchain;
	}

	return ok;
}

GFX_Image *gfx_backbuffer(GFX_Device *device, GFX_Command *cmd, GFX_Swapchain *swapchain) {
	GFX_Image *result = 0;
	uint32_t swapchain_index = cmd->swapchain_count;
	uint32_t image_index = -1;

	bool ok = cmd && cmd->handle && gfx_swapchain_valid(device, swapchain);
	if (ok) { // acquire swapchain image
		VkResult result = vkAcquireNextImageKHR(
			device->handle,
			swapchain->handle,
			UINT64_MAX,
			swapchain->image_available_semaphores[device->current_frame_index],
			VK_NULL_HANDLE, &cmd->swapchain_image_indices[swapchain_index]);
		image_index = cmd->swapchain_image_indices[swapchain_index];

		ok = (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && image_index != (uint32_t)-1;
	}

	if (ok) { // wrap swapchain
		cmd->swapchain_count++;
		cmd->swapchains[swapchain_index] = swapchain;

		result = &swapchain->wrapper;

		result->handle = swapchain->images[image_index];
		result->view = swapchain->views[image_index];
		result->width = swapchain->info.imageExtent.width;
		result->res_usage = RESOURCE_USAGE_UNDEFINED;
		result->height = swapchain->info.imageExtent.height;
	}

	if (ok == false)
		result = 0;

	return result;
}

bool gfx_swapchain_resize(GFX_Device *device, GFX_Swapchain *swapchain, uint32_t new_width, uint32_t new_height) {
	bool ok = gfx_swapchain_valid(device, swapchain);
	if (ok) {
		vkDeviceWaitIdle(device->handle); // TODO: Queue free instead
		OS_Surface *native = swapchain->native;
		const char *name = swapchain->wrapper.options.debug_name;

		gfx_swapchain_destroy(device, swapchain);
		gfx_swapchain_make(device, native, name);
	}

	return ok;
}

bool gfx_image_resize(GFX_Device *device, GFX_Image *image, uint32_t new_width, uint32_t new_height) {
	ImageOptions options = { 0 };

	bool ok = gfx_image_valid(device, image);
	if (ok) {
		options = image->options;
		ok = gfx_image_destroy(device, image);
		if (ok == false)
			LOG_ERROR("failed to destroy image for resizing.");
	}

	if (ok) {
		ok = gfx_image_make(device, new_width, new_height, options);
		if (ok == false)
			LOG_ERROR("failed to recreate image for resizing.");
	}

	return ok;
}

// :device
bool gfx_device_make(GFX_Device *device) {
	bool ok = device;
	if (ok)
		ok = gfx__instance_make(device);

	if (ok)
		ok = gfx__device_make(device);

	if (ok)
		ok = gfx__frame_resources_make(device);

	if (ok)
		device->global_range = (VkPushConstantRange){
			.stageFlags = VK_SHADER_STAGE_ALL,
			.offset = 0,
			.size = 128
		};

	if (ok) {
		device->arena[0] = arena_make(MiB(32));
		device->buffer_pool = arena_push_count(device->arena, GFX_Buffer, MAX_BUFFERS);
		device->image_pool = arena_push_count(device->arena, GFX_Image, MAX_IMAGES);
		device->sampler_pool = arena_push_count(device->arena, GFX_Sampler, MAX_SAMPLERS);
		device->shader_pool = arena_push_count(device->arena, GFX_Shader, MAX_SHADERS);
		device->pipeline_pool = arena_push_count(device->arena, GFX_Pipeline, MAX_PIPELINES);
		device->swapchain_pool = arena_push_count(device->arena, GFX_Swapchain, MAX_SWAPCHAINS);

		// 0 == invalid
		device->buffer_count += 1;
		device->image_count += 1;
		device->sampler_count += 1;
		device->shader_count += 1;
		device->pipeline_count += 1;
		device->swapchain_count += 1;
	}

	if (ok) {
		device->frame_staging_buffer_slice_size = MiB(64);
		device->frame_staging_buffer =
			gfx_buffer_make(
				device,
				device->frame_staging_buffer_slice_size * MAX_FRAMES_IN_FLIGHT,
				(BufferOptions){
				  .debug_name = "engine:frame_staging_buffer",
				  .memory = MEMORY_TYPE_CPU,
				  .usage = BUFFER_USAGE_TRANSFER | BUFFER_USAGE_UNIFORM | BUFFER_USAGE_STORAGE,
				});

		ok = device->frame_staging_buffer;

		device->transfer_staging_buffer_slice_size = MiB(256);
		device->transfer_staging_buffer =
			gfx_buffer_make(
				device,
				device->transfer_staging_buffer_slice_size * MAX_TRANSFERS_IN_FLIGHT,
				(BufferOptions){
				  .debug_name = "engine:transfer_staging_buffer",
				  .memory = MEMORY_TYPE_CPU,
				  .usage = BUFFER_USAGE_TRANSFER | BUFFER_USAGE_UNIFORM | BUFFER_USAGE_STORAGE,
				});
		ok = device->transfer_staging_buffer;

		if (ok == false) {
			LOG_ERROR("failed to create device staging buffers.");
		}
	}
	if (ok) {
		vkMapMemory(device->handle, device->frame_staging_buffer->memory, 0, device->frame_staging_buffer->size, 0, (void **)&device->frame_staging_buffer->mapped);
		vkMapMemory(device->handle, device->transfer_staging_buffer->memory, 0, device->transfer_staging_buffer->size, 0, (void **)&device->transfer_staging_buffer->mapped);
	}

	if (ok) {
		device->initialized = true;
	} else {
		LOG_WARN("failed to initialize vulkan device.");
	}

	return ok;
}

void gfx_device_destroy(GFX_Device *device) {
	bool ok = gfx_device_valid(device);
	if (ok) {
		vkDeviceWaitIdle(device->handle);
		for (uint32_t index = 0; index < MAX_BUFFERS; ++index) {
			gfx_buffer_destroy(device, &device->buffer_pool[index]);
			if (device->buffer_count == 0)
				break;
		}

		for (uint32_t index = 0; index < MAX_IMAGES; ++index) {
			gfx_image_destroy(device, &device->image_pool[index]);
			if (device->image_count == 0)
				break;
		}

		for (uint32_t index = 0; index < MAX_SAMPLERS; ++index) {
			gfx_sampler_destroy(device, &device->sampler_pool[index]);
			if (device->sampler_count == 0)
				break;
		}

		for (uint32_t index = 0; index < MAX_PIPELINES; ++index) {
			gfx_pipeline_destroy(device, &device->pipeline_pool[index]);
			if (device->pipeline_count == 0)
				break;
		}

		for (uint32_t index = 0; index < MAX_SHADERS; ++index) {
			gfx_shader_destroy(device, &device->shader_pool[index]);
			if (device->shader_count == 0)
				break;
		}

		for (uint32_t index = 0; index < MAX_SWAPCHAINS; ++index) {
			gfx_swapchain_destroy(device, &device->swapchain_pool[index]);
			if (device->swapchain_count == 0)
				break;
		}

		for (uint32_t index = 0; index < countof(device->frame_commands); ++index) {
			GFX_Command *cmd_buffer = &device->frame_commands[index];

			if (cmd_buffer->descriptor_pool)
				vkDestroyDescriptorPool(device->handle, cmd_buffer->descriptor_pool, 0);
			if (cmd_buffer->in_flight_fence)
				vkDestroyFence(device->handle, cmd_buffer->in_flight_fence, 0);
		}

		for (uint32_t index = 0; index < countof(device->transfer_commands); ++index) {
			GFX_Command *cmd_buffer = &device->transfer_commands[index];

			if (cmd_buffer->in_flight_fence)
				vkDestroyFence(device->handle, cmd_buffer->in_flight_fence, 0);
		}

		if (device->graphics_command_pool)
			vkDestroyCommandPool(device->handle, device->graphics_command_pool, 0);

		/* if (device->transfer_command_pool) */
		/* 	vkDestroyCommandPool(device->handle, device->transfer_command_pool, 0); */

#ifdef DEV_BUILD
		if (device->debug_messenger)
			vkDestroyDebugUtilsMessenger(device->instance, device->debug_messenger, 0);
#endif
		if (device->handle)
			vkDestroyDevice(device->handle, 0);
		if (device->instance)
			vkDestroyInstance(device->instance, 0);

		memory_zero(device, sizeof(GFX_Device));
	}
}

// :cmd
GFX_Command *gfx_frame_begin(GFX_Device *device) {
	GFX_Command *result = 0;

	bool ok = gfx_device_valid(device);
	if (ok) {
		result = &device->frame_commands[device->current_frame_index];
		result->frame_index = device->current_frame_index;
		memory_zero_array(result->swapchains);
		memory_zero_array(result->swapchain_image_indices);
		result->active_shader = 0;
		result->swapchain_count = 0;

		// Wait for frame resource availability
		ok = vkWaitForFences(device->handle, 1, &result->in_flight_fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
	}

	if (ok) {
		// reset frame resources
		vkResetCommandBuffer(result->handle, 0);
		vkResetFences(device->handle, 1, &result->in_flight_fence);
		vkResetDescriptorPool(device->handle, result->descriptor_pool, 0);

		result->transient_buffer = device->frame_staging_buffer;
		result->transient_arena[0] = arena_wrap(device->frame_staging_buffer->mapped, device->frame_staging_buffer->size);
		result->transient_arena->offset = device->current_frame_index * device->frame_staging_buffer_slice_size;

		// Begin command recording
		VkCommandBufferBeginInfo cb_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};

		ok = vkBeginCommandBuffer(result->handle, &cb_begin_info) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to begin command buffer recording.");
	}

	if (ok) // submit transfer batch
		gfx_transfer_flush(device);

	if (ok == false)
		result = 0;

	return result;
}

bool gfx_frame_end(GFX_Device *device, GFX_Command *cmd) {
	bool ok = gfx_device_valid(device) && cmd && cmd->handle;
	if (ok) {
		for (uint32_t swapchain_index = 0; swapchain_index < cmd->swapchain_count; ++swapchain_index) {
			// transition swapchain images if it hasn't already been done
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_PRESENT, &cmd->swapchains[swapchain_index]->wrapper);
		}

		ok = vkEndCommandBuffer(cmd->handle) == VK_SUCCESS;
	}

	if (ok) {
		VkSwapchainKHR handles[MAX_SWAPCHAINS] = { 0 };
		VkSemaphore wait_semaphores[MAX_SWAPCHAINS] = { 0 };
		VkSemaphore signal_semaphores[MAX_SWAPCHAINS] = { 0 };
		VkPipelineStageFlags wait_stages[MAX_SWAPCHAINS] = {
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		};

		for (uint32_t swapchain_index = 0; swapchain_index < cmd->swapchain_count; ++swapchain_index) {
			handles[swapchain_index] = cmd->swapchains[swapchain_index]->handle;
			wait_semaphores[swapchain_index] = cmd->swapchains[swapchain_index]->image_available_semaphores[cmd->frame_index];
			signal_semaphores[swapchain_index] = cmd->swapchains[swapchain_index]->render_done_semaphores[cmd->swapchain_image_indices[swapchain_index]];
		}

		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = cmd->swapchain_count,
			.pWaitSemaphores = wait_semaphores,
			.pWaitDstStageMask = wait_stages,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd->handle,
			.signalSemaphoreCount = cmd->swapchain_count,
			.pSignalSemaphores = signal_semaphores,
		};

		ok = vkQueueSubmit(device->graphics_queue, 1, &submit_info, device->frame_commands[device->current_frame_index].in_flight_fence) == VK_SUCCESS;
		if (ok == false)
			LOG_ERROR("failed to submit command buffer to queue.");

		if (cmd->swapchain_count) {
			VkPresentInfoKHR present_info = {
				.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.waitSemaphoreCount = cmd->swapchain_count,
				.pWaitSemaphores = signal_semaphores,
				.swapchainCount = cmd->swapchain_count,
				.pSwapchains = handles,
				.pImageIndices = cmd->swapchain_image_indices,
			};
			vkQueuePresentKHR(device->present_queue, &present_info);
		}
	}

	return ok;
}

GFX_Command *gfx_transfer_cmd(GFX_Device *device) {
	GFX_Command *result = 0;

	bool ok = gfx_device_valid(device);
	if (ok) {
		result = &device->transfer_commands[device->current_transfer_index];

		if (result->recording == 0)
			ok = vkWaitForFences(device->handle, 1, &result->in_flight_fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
	}

	if (ok && result->recording == 0) {
		// reset resources
		vkResetCommandBuffer(result->handle, 0);
		vkResetFences(device->handle, 1, &result->in_flight_fence);

		result->transient_buffer = device->transfer_staging_buffer;
		result->transient_arena[0] = arena_wrap(device->transfer_staging_buffer->mapped, device->transfer_staging_buffer->size);
		result->transient_arena->offset = device->current_transfer_index * device->transfer_staging_buffer_slice_size;

		// Begin command recording
		VkCommandBufferBeginInfo cb_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};
		ok = vkBeginCommandBuffer(result->handle, &cb_begin_info) == VK_SUCCESS;

		if (ok == false) {
			LOG_WARN("failed to begin command buffer recording.");
		}
	}

	if (ok)
		result->recording = 1;

	if (ok == false)
		result = 0;

	return result;
}

bool gfx_transfer_flush(GFX_Device *device) {
	bool ok = gfx_device_valid(device);

	GFX_Command *cmd = 0;
	if (ok) { // check if recording
		cmd = &device->transfer_commands[device->current_transfer_index];

		ok = cmd->recording;
	}

	if (ok) { // end recording
		ok = vkEndCommandBuffer(cmd->handle) == VK_SUCCESS;
		if (ok == false) {
			LOG_INFO("failed to record transfer command buffer.");
		}

		cmd->recording = 0;
	}

	if (ok) { // submit to queue
		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd->handle,
		};

		ok = vkQueueSubmit(device->graphics_queue, 1, &submit_info, cmd->in_flight_fence) == VK_SUCCESS;
		if (ok == false)
			LOG_ERROR("failed to submit transfer command buffer to queue.");

		device->current_transfer_index = (device->current_transfer_index + 1) % MAX_TRANSFERS_IN_FLIGHT;
	}

	return ok;
}

bool gfx_bind(GFX_Device *device, uint32_t set_index, Uniform *uniforms, uint32_t uniform_count) {
	GFX_Shader *shader = 0;
	GFX_Command *cmd = 0;
	VkDescriptorSet set = 0;

	bool ok = gfx_device_valid(device) && uniforms && uniform_count > 0;
	if (ok == false)
		LOG_ERROR("%s - invalid parameters.", __func__);

	if (ok) {
		cmd = &device->frame_commands[device->current_frame_index];
		shader = cmd->active_shader;

		ok = gfx_shader_valid(device, shader);
	}

	if (ok) { // TODO: cache descriptor sets instead?
		VkDescriptorSetAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = cmd->descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &shader->layouts[set_index],
		};
		ok = vkAllocateDescriptorSets(device->handle, &alloc_info, &set) == VK_SUCCESS;
	}

	if (ok) { // write uniforms
		ArenaTemp scratch = arena_scratch_begin(0);
		for (uint32_t uniform_index = 0; uniform_index < uniform_count; ++uniform_index) {
			Uniform *uniform = &uniforms[uniform_index];
			UniformSet *reflected_sets = &shader->reflection.sets[set_index];

			int32_t found_index = -1;
			for (uint32_t search_index = 0; search_index < reflected_sets->uniform_count; ++search_index) {
				if (reflected_sets->uniforms[search_index].binding == uniform->binding) {
					found_index = search_index;
					break;
				}
			}

			if (found_index == -1) {
				LOG_ERROR("shader '%s' has no uniform at set = %u, binding = %u.", shader->debug_name, set_index, uniform->binding);
				ASSERT(false);
			}
			Uniform *reflected_uniform = &reflected_sets->uniforms[found_index];

			ASSERT_FORMAT(uniform->type == reflected_uniform->type,
				"uniform binding '%s' (set = %u, binding = %u) of shader '%s' supplied as: %s, expected: %s.",
				reflected_uniform->name, set_index, reflected_uniform->binding, shader->debug_name, uniform_type_to_string[uniform->type], uniform_type_to_string[reflected_sets->uniforms[found_index].type]);

			switch (uniform->type) {
				case UNIFORM_TYPE_STORAGE_IMAGE:
				case UNIFORM_TYPE_SAMPLER:
				case UNIFORM_TYPE_IMAGE:
				case UNIFORM_TYPE_SAMPLER_WITH_IMAGE: {
					VkDescriptorImageInfo *image_infos = arena_push_count(scratch.arena, VkDescriptorImageInfo, uniform->count);

					VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					if (uniform->type == UNIFORM_TYPE_STORAGE_IMAGE)
						layout = VK_IMAGE_LAYOUT_GENERAL;

					GFX_Sampler *sampler = uniform->resource.sampler_with_textures.sampler;
					for (uint32_t image_index = 0; image_index < uniform->count; ++image_index) {
						GFX_Image *image = uniform->resource.sampler_with_textures.images[image_index];

						image_infos[image_index] = (VkDescriptorImageInfo){
							.imageLayout = image ? layout : 0,
							.imageView = image ? image->view : 0,
							.sampler = sampler ? sampler->handle : 0,
						};
					}
					VkWriteDescriptorSet write = {
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.dstSet = set,
						.dstBinding = uniform->binding,
						.dstArrayElement = 0,
						.descriptorCount = uniform->count,
						.descriptorType = uniform_type_to_vulkan_descriptor_type[uniform->type],
						.pImageInfo = image_infos,
					};
					vkUpdateDescriptorSets(device->handle, 1, &write, 0, 0);
				} break;

				case UNIFORM_TYPE_STORAGE_BUFFER:
				case UNIFORM_TYPE_UNIFORM_BUFFER: {
					GFX_Buffer *buffer = 0;
					uint64_t offset = 0;
					uint64_t size = 0;
					if (uniform->resource.buffer.handle) {
						buffer = uniform->resource.buffer.handle;
						offset = uniform->resource.buffer.offset;
						size = uniform->resource.buffer.size;
					} else if (uniform->resource.buffer.data) {
						buffer = cmd->transient_buffer;
						offset = alignup(cmd->transient_arena->offset, 256);
						size = uniform->resource.buffer.size;
						memory_copy(
							arena_push(cmd->transient_arena, alignup(size, 256), 256, 0),
							uniform->resource.buffer.data,
							size);
					} else
						ASSERT(false); // TODO: Handle fallback

					VkDescriptorBufferInfo buffer_info = {
						.buffer = buffer->handle,
						.offset = offset,
						.range = size,
					};
					VkWriteDescriptorSet write = {
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.dstSet = set,
						.dstBinding = uniform->binding,
						.dstArrayElement = 0,
						.descriptorCount = uniform->count,
						.descriptorType = uniform_type_to_vulkan_descriptor_type[uniform->type],
						.pBufferInfo = &buffer_info,
					};
					vkUpdateDescriptorSets(device->handle, 1, &write, 0, 0);
				} break;

				default:
					ASSERT(false);
					break;
			}
		}

		arena_scratch_end(scratch);
	}

	if (ok) {
		VkPipelineBindPoint bind_point = shader->modules[SHADER_STAGE_COMPUTE] ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
		vkCmdBindDescriptorSets(cmd->handle, bind_point, shader->layout, set_index, 1, &set, 0, 0);
	}

	return ok;
}

uint64_t gfx_cmd_put(GFX_Command *cmd, uint64_t size, void *src) {
	uint64_t result = 0;

	bool ok = cmd && cmd->handle;
	if (ok) {
		result = alignup(cmd->transient_arena->offset, 256);
		void *dst = arena_push(cmd->transient_arena, alignup(size, 256), 256, 0);
		if (src)
			memory_copy(dst, src, size);
	}

	return result;
}

void gfx_cmd_buffer_to_buffer(GFX_Command *cmd, GFX_Buffer *dst, GFX_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size) {
	bool ok = cmd && cmd->handle;
	if (ok) {
		LOG_TRACE("Copying region of %llu from %p to %p", size, src, dst);
		VkBufferCopy copy_region = { .srcOffset = src_offset, .dstOffset = dst_offset, .size = size };
		vkCmdCopyBuffer(cmd->handle, src->handle, dst->handle, 1, &copy_region);
	}
}

void gfx_cmd_buffer_to_image(GFX_Command *cmd, GFX_Image *dst, GFX_Buffer *src, uint64_t src_offset, uint32_t width, uint32_t height) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	bool ok = cmd && cmd->handle && dst && dst->handle;
	if (ok == false)
		LOG_WARN("%s - invalid parameter '%s' passed", __func__, cmd == 0 || cmd->handle == 0 ? "GFX_CommandContext" : "GFX_Image");

	if (ok) {
		gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_DST, dst);

		uint32_t layer_count = image_options_to_vulkan_layer_count(dst->options);
		VkBufferImageCopy *regions = arena_push_count(scratch.arena, VkBufferImageCopy, layer_count);

		for (uint32_t layer_index = 0; layer_index < layer_count; ++layer_index) {
			regions[layer_index] = (VkBufferImageCopy){
				.bufferOffset = src_offset + (layer_index * width * height * pixel_format_to_stride[dst->options.format]),
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource = {
				  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				  .mipLevel = 0,
				  .baseArrayLayer = layer_index,
				  .layerCount = 1,
				},
				.imageOffset = { 0 },
				.imageExtent = { .width = width, .height = height, .depth = 1 },
			};
		}
		vkCmdCopyBufferToImage(cmd->handle, src->handle, dst->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layer_count, regions);
	}

	arena_scratch_end(scratch);
}

void gfx_cmd_buffer_barrier(GFX_Command *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, GFX_Buffer *target) {
	bool ok = cmd && target;
	if (ok) {
		VkPipelineStageFlags src_stage = resource_usage_to_vulkan_pipeline_stage[src];
		VkPipelineStageFlags dst_stage = resource_usage_to_vulkan_pipeline_stage[dst];
		VkAccessFlags src_access = resource_usage_to_vulkan_access_flags[src];
		VkAccessFlags dst_access = resource_usage_to_vulkan_access_flags[dst];

		VkBufferMemoryBarrier buffer_barrier = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.buffer = target->handle,
			.srcAccessMask = src_access,
			.dstAccessMask = dst_access,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.offset = offset,
			.size = size,
		};
		vkCmdPipelineBarrier(cmd->handle, src_stage, dst_stage, 0, 0, 0, 1, &buffer_barrier, 0, 0);
	}
}

bool gfx_cmd_image_barrier(GFX_Command *cmd, ResourceUsage src, ResourceUsage dst, uint32_t base_miplevel, uint32_t level_count, GFX_Image *target) {
	bool ok = cmd && target;
	if (ok) {
		target->res_usage = dst;

		ok = dst;
	}

	if (ok) {
		VkPipelineStageFlags src_stage = resource_usage_to_vulkan_pipeline_stage[src];
		VkPipelineStageFlags dst_stage = resource_usage_to_vulkan_pipeline_stage[dst];
		VkAccessFlags src_access = resource_usage_to_vulkan_access_flags[src];
		VkAccessFlags dst_access = resource_usage_to_vulkan_access_flags[dst];
		VkImageLayout src_layout = resource_usage_to_vulkan_image_layout[src];
		VkImageLayout dst_layout = resource_usage_to_vulkan_image_layout[dst];

		VkImageMemoryBarrier image_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = src_access,
			.dstAccessMask = dst_access,
			.oldLayout = src_layout,
			.newLayout = dst_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target->handle,
			.subresourceRange = {
			  .aspectMask = image_options_to_vulkan_aspect_flags(target->options),
			  .baseArrayLayer = 0,
			  .baseMipLevel = base_miplevel,
			  .levelCount = level_count,
			  .layerCount = image_options_to_vulkan_layer_count(target->options),
			},
		};

		vkCmdPipelineBarrier(
			cmd->handle,
			src_stage, dst_stage,
			0,
			0, NULL,
			0, NULL,
			1, &image_barrier);
	}

	return ok;
}

bool gfx_cmd_image_transition(GFX_Command *cmd, ResourceUsage dst, GFX_Image *target) {
	bool ok = cmd && cmd->handle && target && target->handle;
	if (ok) {
		if (target->res_usage == dst)
			return true;

		return gfx_cmd_image_barrier(cmd, target->res_usage, dst, 0, target->miplevels, target);
	}

	return ok;
}

void gfx_cmd_image_blit(GFX_Command *cmd, Rectangle source_rect, GFX_Image *source, Rectangle target_rect, GFX_Image *target) {
	bool ok = cmd && source && target;
	if (ok) {
		if (source->res_usage != RESOURCE_USAGE_TRANSFER_SRC)
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_SRC, source);
		if (target->res_usage != RESOURCE_USAGE_TRANSFER_DST)
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_DST, target);
		if (source_rect.width == 0.0f)
			source_rect.width = source->width;
		if (source_rect.height == 0.0f)
			source_rect.height = source->height;
		if (target_rect.width == 0.0f)
			target_rect.width = target->width;
		if (target_rect.height == 0.0f)
			target_rect.height = target->height;

		VkImageBlit blit_info = {
			.srcOffsets[1] = {
			  .x = source_rect.width,
			  .y = source_rect.height,
			  .z = 1,
			},
			.srcSubresource = {
			  .aspectMask = image_options_to_vulkan_aspect_flags(source->options),
			  .baseArrayLayer = 0,
			  .layerCount = image_options_to_vulkan_layer_count(source->options),
			  .mipLevel = 0,
			},
			.dstOffsets[1] = {
			  .x = target_rect.width,
			  .y = target_rect.height,
			  .z = 1,
			},
			.dstSubresource = {
			  .aspectMask = image_options_to_vulkan_aspect_flags(target->options),
			  .baseArrayLayer = 0,
			  .layerCount = image_options_to_vulkan_layer_count(target->options),
			  .mipLevel = 0,
			},
		};
		vkCmdBlitImage(cmd->handle,
			source->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			target->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit_info, 0);
	}
}

void gfx_cmd_image_upload(GFX_Command *cmd, GFX_Image *image, uint32_t width, uint32_t height, void *pixels) {
	bool ok = cmd && cmd->handle && image && image->handle;
	if (ok) {
		uint32_t stride = pixel_format_to_stride[image->options.format];
		uint64_t size = width * height * stride * image_options_to_vulkan_layer_count(image->options);
		uint64_t start_offset = gfx_cmd_put(cmd, size, pixels);
		gfx_cmd_buffer_to_image(cmd, image, cmd->transient_buffer, start_offset, width, height);
	}
}

void gfx_cmd_buffer_upload(GFX_Command *cmd, GFX_Buffer *buffer, uint64_t offset, uint64_t size, void *data) {
	bool ok = cmd && cmd->handle && buffer && buffer->handle;
	if (ok) {
		uint64_t staging_offset = gfx_cmd_put(cmd, size, data);
		gfx_cmd_buffer_to_buffer(cmd, buffer, cmd->transient_buffer, offset, staging_offset, size);
	}
}

void gfx_cmd_viewport(GFX_Command *cmd, Rectangle area) {
	bool ok = cmd && cmd->handle;
	if (ok) {
		VkViewport viewports[] = {
			[0] = {
			  .x = area.x,
			  .y = area.y,
			  .width = area.width,
			  .height = area.height,
			}
		};
		vkCmdSetViewport(cmd->handle, 0, 1, viewports);
	}
}
void gfx_cmd_scissor(GFX_Command *cmd, Rectangle area) {
	bool ok = cmd && cmd->handle;
	if (ok) {
		VkRect2D scissors[] = {
			[0] = {
			  { .x = area.x, .y = area.y },
			  { .width = area.width, .height = area.height } //
			}
		};
		vkCmdSetScissor(cmd->handle, 0, 1, scissors);
	}
}

void gfx_cmd_shader_bind(GFX_Command *cmd, GFX_Shader *shader) {
	GFX_Pipeline *target = 0;

	bool ok = cmd && cmd->handle && shader && shader->first_pipeline;
	if (ok)
		gfx_cmd_pipeline_bind(cmd, shader->first_pipeline);
}

void gfx_cmd_pipeline_bind(GFX_Command *cmd, GFX_Pipeline *pipeline) {
	bool ok = cmd && cmd->handle && pipeline && pipeline->handle;
	if (ok) {
		VkPipelineBindPoint bind_point = pipeline->shader->modules[SHADER_STAGE_COMPUTE] ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
		vkCmdBindPipeline(cmd->handle, bind_point, pipeline->handle);

		cmd->active_shader = pipeline->shader;
	}
}

void gfx_cmd_dispatch(GFX_Command *cmd, uint32_t x, uint32_t y, uint32_t z) {
	bool ok = cmd && cmd->handle;
	if (ok)
		vkCmdDispatch(cmd->handle, x, y, z);
}

// :body
uint32_t gfx__find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

	for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index)
		if ((type_filter & (1 << index)) && (memory_properties.memoryTypes[index].propertyFlags & properties) == properties)
			return index;

	LOG_ERROR("Failed to find suitable memory type!");
	ASSERT(false);
	return 0;
}

bool gfx__instance_make(GFX_Device *device) {
	LOG_DEBUG("initializing vulkan instance.");
	ArenaTemp scratch = arena_scratch_begin(0);

	uint32_t required_extension_count = 0;
	const char **required_extensions = os_surface_vulkan_extensions(&required_extension_count);

	bool ok = true;
	if (ok) { // validate if required extensions are present
#ifdef DEV_BUILD
		const char **debug_extensions = arena_push_count(scratch.arena, const char *, required_extension_count + 1);
		memory_copy(debug_extensions, required_extensions, sizeof(*required_extensions) * required_extension_count);
		debug_extensions[required_extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		required_extensions = debug_extensions;
#endif

		uint32_t available_extension_count = 0;
		vkEnumerateInstanceExtensionProperties(0, &available_extension_count, 0);

		VkExtensionProperties *available_extensions = arena_push_count(scratch.arena, VkExtensionProperties, available_extension_count);
		vkEnumerateInstanceExtensionProperties(0, &available_extension_count, available_extensions);
		ok = gfx__validate_extensions(required_extensions, required_extension_count, available_extensions, available_extension_count);
	}
	static const char *requested_layers[] = {
#ifdef DEV_BUILD
		"VK_LAYER_KHRONOS_validation",
#endif
	};
	uint32_t requested_layer_count = countof(requested_layers);

	if (ok) { // validate if required layers are present
		uint32_t available_layer_count;
		vkEnumerateInstanceLayerProperties(&available_layer_count, 0);

		VkLayerProperties *available_layers = arena_push_count(scratch.arena, VkLayerProperties, available_layer_count);
		vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers);

		for (uint32_t request_index = 0; request_index < requested_layer_count; ++request_index) {
			bool found = false;
			for (uint32_t layer_index = 0; layer_index < available_layer_count; ++layer_index) {
				if (strcmp(available_layers[layer_index].layerName, requested_layers[request_index]) == 0) {
					found = true;
					break;
				}
			}

			if (found == false) {
				LOG_ERROR("layer '%s' not found, aborting", requested_layers[request_index]);
				ok = false;
			}
		}
	}

	if (ok) { // create vulkan instance
		VkApplicationInfo app_info = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "unnamed",
			.applicationVersion = 1,
			.pEngineName = "unnamed",
			.engineVersion = 1,
			.apiVersion = VK_MAKE_VERSION(1, 3, 0)
		};

		VkInstanceCreateInfo instance_info = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &app_info,
			.enabledExtensionCount = required_extension_count,
			.ppEnabledExtensionNames = required_extensions,
			.ppEnabledLayerNames = requested_layers,
			.enabledLayerCount = requested_layer_count,

#ifdef DEV_BUILD
			.pNext = &debug_utils_create_info,
#endif
		};

		ok = vkCreateInstance(&instance_info, 0, &device->instance) == VK_SUCCESS;
	}

	if (ok) { // load debug extension pointers & craete debug util
#ifdef DEV_BUILD
		gfx__load_debug_extensions(device);
		vkCreateDebugUtilsMessenger(device->instance, &debug_utils_create_info, 0, &device->debug_messenger);
#endif
	}

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__device_suitable(GFX_Device *device, VkPhysicalDevice gpu) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = true;
	if (ok) { // check if vulkan 1.3 is supported
		vkGetPhysicalDeviceProperties(gpu, &device->info.properties);
		device->info.limits = device->info.properties.limits;

		ok = device->info.properties.apiVersion >= VK_API_VERSION_1_3;
	}

	/* if (ok) { // check for present support  */
	/* 	uint32_t queue_family_count = 0; */
	/* 	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, 0); */

	/* 	VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count); */
	/* 	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_family_properties); */

	/* 	VkBool32 supports_present = VK_FALSE; */
	/* 	for (uint32_t queue_family_index = 0; queue_family_index < queue_family_count; ++queue_family_index) { */
	/* 		vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_family_index, device->surface.handle, &supports_present); */

	/* 		if ((queue_family_properties[queue_family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supports_present) */
	/* 			break; */
	/* 	} */

	/* 	if (supports_present == false) */
	/* 		ok = false; */
	/* } */

	if (ok) { // validate if required device extensions are present
		uint32_t available_extension_count = 0;
		vkEnumerateDeviceExtensionProperties(gpu, 0, &available_extension_count, 0);

		VkExtensionProperties *available_extensions = arena_push_count(scratch.arena, VkExtensionProperties, available_extension_count);
		vkEnumerateDeviceExtensionProperties(gpu, 0, &available_extension_count, available_extensions);

		ok = gfx__validate_extensions(required_device_extensions, countof(required_device_extensions), available_extensions, available_extension_count);
	}

	if (ok) { // validate if desired features are present
		VkPhysicalDeviceVulkan13Features vk13_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		};
		VkPhysicalDeviceVulkan12Features vk12_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &vk13_features,
		};
		VkPhysicalDeviceFeatures2 query_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vk12_features,
		};
		vkGetPhysicalDeviceFeatures2(gpu, &query_features);

		if (vk13_features.dynamicRendering == false || vk12_features.bufferDeviceAddress == false)
			ok = false;
	}

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__device_make(GFX_Device *device) {
	LOG_DEBUG("initializing vulkan device.");
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = device;
	if (ok) { // select physical device
		LOG_DEBUG("finding suitable device...");
		uint32_t physical_device_count = 0;
		vkEnumeratePhysicalDevices(device->instance, &physical_device_count, 0);

		VkPhysicalDevice *physical_devices = arena_push_count(scratch.arena, VkPhysicalDevice, physical_device_count);
		vkEnumeratePhysicalDevices(device->instance, &physical_device_count, physical_devices);

		for (uint32_t physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index) {
			if (gfx__device_suitable(device, physical_devices[physical_device_index])) {
				device->info.gpu = physical_devices[physical_device_index];
				break;
			}
		}

		ok = device->info.gpu != 0;
		if (ok == false)
			LOG_ERROR("failed to find suitable graphics card with Vulkan 1.3 support.");
	}

	if (ok) { // find queue indices
		LOG_INFO("Device: %s", device->info.properties.deviceName);

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device->info.gpu, &queue_family_count, 0);

		VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(device->info.gpu, &queue_family_count, queue_family_properties);

		device->graphics_index = -1, device->present_index = -1,
		device->transfer_index = -1, device->compute_index = -1;

		for (uint32_t index = 0; index < queue_family_count; ++index) {
			VkQueueFlags flags = queue_family_properties[index].queueFlags;

			/* VkBool32 present_support = false; */
			/* vkGetPhysicalDeviceSurfaceSupportKHR(device->info.gpu, index, device->surface.handle, &present_support); */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) && device->graphics_index == -1) {
				device->graphics_index = index;
				/* ASSERT(present_support && "grahpics index does not support presenting"); */
			}

			/* if (present_support && device->present_index == -1) */
			/* 	device->present_index = index; */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_TRANSFER_BIT) && device->transfer_index == -1) // dedicated transfer
				device->transfer_index = index;

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_COMPUTE_BIT) && device->compute_index == -1) // dedicated compute
				device->compute_index = index;
		}

		if (device->graphics_index == -1) {
			LOG_ERROR("failed to find graphics queue.");
			ok = false;
		}
	}

	if (ok) { // default to grahpics if compute/transfer queues aren't available
		if (device->transfer_index == -1)
			device->transfer_index = device->graphics_index;
		if (device->compute_index == -1)
			device->compute_index = device->graphics_index;
	}

	if (ok) { // create vulkan device
		float queue_priortiy = 0.5f;

		VkDeviceQueueCreateInfo queue_infos[] = {
			{
			  // grahpics queue
			  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			  .queueFamilyIndex = device->graphics_index,
			  .queueCount = 1,
			  .pQueuePriorities = &queue_priortiy,
			},
			{
			  // transfer queue
			  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			  .queueFamilyIndex = device->transfer_index,
			  .queueCount = 1,
			  .pQueuePriorities = &queue_priortiy,
			},
			{
			  // compute queue
			  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			  .queueFamilyIndex = device->compute_index,
			  .queueCount = 1,
			  .pQueuePriorities = &queue_priortiy,
			}
		};

		VkPhysicalDeviceVulkan13Features vk13_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.dynamicRendering = VK_TRUE,
		};
		VkPhysicalDeviceVulkan12Features vk12_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &vk13_features,
			.runtimeDescriptorArray = VK_TRUE,
			.descriptorBindingPartiallyBound = VK_TRUE,
			.shaderSampledImageArrayNonUniformIndexing = VK_TRUE, // NOTE: Works perfectly fine without
			.descriptorIndexing = VK_TRUE,
			.bufferDeviceAddress = VK_TRUE,
		};
		VkPhysicalDeviceFeatures2 enable_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vk12_features,
			.features = { .samplerAnisotropy = true, .fillModeNonSolid = true },
		};

		VkDeviceCreateInfo device_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &enable_features,
			.queueCreateInfoCount = countof(queue_infos),
			.pQueueCreateInfos = queue_infos,
			.enabledExtensionCount = countof(required_device_extensions),
			.ppEnabledExtensionNames = required_device_extensions,
		};

		ok = vkCreateDevice(device->info.gpu, &device_info, 0, &device->handle) == VK_SUCCESS;
	}

	if (ok) { // get the queue handles from indices
		vkGetDeviceQueue(device->handle, device->graphics_index, 0, &device->graphics_queue);
		vkGetDeviceQueue(device->handle, device->transfer_index, 0, &device->transfer_queue);
		vkGetDeviceQueue(device->handle, device->compute_index, 0, &device->compute_queue);
	}

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__frame_resources_make(GFX_Device *device) {
	LOG_DEBUG("initializing vulkan frame resources.");

	bool ok = gfx_device_valid(device);
	if (ok) { // make graphics command pool
		VkCommandPoolCreateInfo cp_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = device->graphics_index
		};
		ok = vkCreateCommandPool(device->handle, &cp_create_info, 0, &device->graphics_command_pool) == VK_SUCCESS;
	}

	if (ok) { // allocate frame command buffers
		VkCommandBuffer buffers[MAX_FRAMES_IN_FLIGHT];
		VkCommandBufferAllocateInfo cb_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = device->graphics_command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = countof(buffers)
		};
		ok = vkAllocateCommandBuffers(device->handle, &cb_allocate_info, buffers) == VK_SUCCESS;

		for (uint32_t frame_index = 0; frame_index < countof(buffers); ++frame_index)
			device->frame_commands[frame_index].handle = buffers[frame_index];
	}

	if (ok) { // allocate transfer command buffers
		VkCommandBuffer buffers[MAX_TRANSFERS_IN_FLIGHT];
		VkCommandBufferAllocateInfo cb_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = device->graphics_command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = countof(buffers)
		};
		ok = vkAllocateCommandBuffers(device->handle, &cb_allocate_info, buffers) == VK_SUCCESS;

		for (uint32_t transfer_index = 0; transfer_index < countof(buffers); ++transfer_index)
			device->transfer_commands[transfer_index].handle = buffers[transfer_index];
	}

	if (ok) { // allocate fences
		VkFenceCreateInfo f_create_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};

		for (uint32_t frame_index = 0; frame_index < countof(device->frame_commands); ++frame_index)
			ok &= vkCreateFence(device->handle, &f_create_info, 0, &device->frame_commands[frame_index].in_flight_fence) == VK_SUCCESS;
		for (uint32_t transfer_index = 0; transfer_index < countof(device->transfer_commands); ++transfer_index)
			ok &= vkCreateFence(device->handle, &f_create_info, 0, &device->transfer_commands[transfer_index].in_flight_fence) == VK_SUCCESS;
	}

	if (ok) { // allocate frame descriptor pools
		VkDescriptorPoolSize sizes[] = {
			{
			  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			  .descriptorCount = 1024,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			  .descriptorCount = 1024,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			  .descriptorCount = 1024,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  .descriptorCount = 1024,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
			  .descriptorCount = 1024,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  .descriptorCount = 1024,
			},
		};

		VkDescriptorPoolCreateInfo dp_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.poolSizeCount = countof(sizes),
			.pPoolSizes = sizes,
			.maxSets = 1024,
		};

		for (uint32_t frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; ++frame_index)
			ok &= vkCreateDescriptorPool(device->handle, &dp_create_info, 0, &device->frame_commands[frame_index].descriptor_pool) == VK_SUCCESS;
	}
	return ok;
}

bool gfx__validate_extensions(const char *required[], uint32_t required_count, VkExtensionProperties *available, uint32_t available_count) {
	bool result = true;

	for (uint32_t required_index = 0; required_index < required_count; ++required_index) {
		bool found = false;
		for (uint32_t available_index = 0; available_index < available_count; ++available_index) {
			if (strcmp(available[available_index].extensionName, required[required_index]) == 0) {
				found = true;
				break;
			}
		}
		if (found == false) {
			LOG_ERROR("required extension '%s' not found, aborting", required[required_index]);
			result = false;
			break;
		}
	}

	return result;
}

void gfx__load_debug_extensions(GFX_Device *device) {
#define LOAD_EXTENSION(instance, name)                                    \
	name = (PFN_##name##EXT)vkGetInstanceProcAddr(instance, #name "EXT"); \
	if (!name) {                                                          \
		LOG_ERROR("Failed to load extension: " #name "EXT");              \
		name = STUB_##name;                                               \
	}

	LOAD_EXTENSION(device->instance, vkCreateDebugUtilsMessenger);
	LOAD_EXTENSION(device->instance, vkDestroyDebugUtilsMessenger);
	LOAD_EXTENSION(device->instance, vkSetDebugUtilsObjectName);
	LOAD_EXTENSION(device->instance, vkCmdBeginDebugUtilsLabel);
	LOAD_EXTENSION(device->instance, vkCmdEndDebugUtilsLabel);

#undef LOAD_EXTENSION
}

VKAPI_ATTR VkBool32 VKAPI_CALL gfx__debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
	void *pUserData) {
	switch (message_severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: {
			LOG_TRACE("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: {
			LOG_INFO("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
			LOG_WARN("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
			LOG_ERROR("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		default: {
			return VK_FALSE;
		} break;
	}
}
