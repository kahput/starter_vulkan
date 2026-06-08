#include "common.h"
#include "core/arena.h"
#include "core/input_types.h"
#include "core/logger.h"
#include "core/debug.h"
#include "os.h"
#include <vulkan/vulkan_core.h>

#define VK_USE_PLATFORM_XCB_KHR
#include "vulkan/vulkan.h"

#define MAX_FRAMES_IN_FLIGHT 2

typedef enum {
	PIXEL_FORMAT_RGBA8_UNORM,
	PIXEL_FORMAT_RGBA8_SRGB,
	PIXEL_FORMAT_RGBA16_FLOAT,
	PIXEL_FORMAT_R32_FLOAT,
	PIXEL_FORMAT_D32_FLOAT,
	PIXEL_FORMAT_D24_UNORM_S8_UINT,
} PixelFormat;

typedef enum {
	IMAGE_USAGE_SAMPLE = 0x1,
	IMAGE_USAGE_RENDER = 0x2,
	IMAGE_USAGE_STORAGE = 0x4,
	IMAGE_USAGE_TRANSFER = 0x8
} ImageUsageFlags;

typedef enum {
	BUFFER_USAGE_VERTEX = 0x1,
	BUFFER_USAGE_INDEX = 0x2,
	BUFFER_USAGE_UNIFORM = 0x4,
	BUFFER_USAGE_STORAGE = 0x8,
	BUFFER_USAGE_TRANSFER = 0x10
} BufferUsage;

typedef enum {
	BUFFER_MEMORY_LOCAL,
	BUFFER_MEMORY_SHARED,
} BufferMemory;

typedef enum {
	SHADER_STAGE_VERTEX,
	SHADER_STAGE_FRAGMENT,
	SHADER_STAGE_COMPUTE,

	SHADER_STAGE_COUNT,
} ShaderStage;

typedef struct {
	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;

	uint32_t width, height;

	VkImageCreateInfo image_info;
	VkImageViewCreateInfo view_info;
} VulkanImage;

typedef struct {
	Arena *arena;
	VkBuffer handle;
	VkDeviceMemory memory;

	uint8_t *mapped;

	uint64_t size;

	VkBufferCreateInfo info;

} VulkanBuffer;

#define MAX_DESCRIPTOR_SETS 4
typedef struct {
	VkPipeline handle;
	VkPipelineLayout layout;

	VkDescriptorSetLayout set_layouts[MAX_DESCRIPTOR_SETS];

	VkShaderModule shaders[SHADER_STAGE_COUNT];
} VulkanPipeline;

#define SWAPCHAIN_IMAGE_COUNT 3
typedef struct {
	VkSurfaceKHR handle;
	VkSwapchainKHR swapchain;

	VkSwapchainCreateInfoKHR swapchain_info;

	VkImage images[SWAPCHAIN_IMAGE_COUNT];
	VkImageView views[SWAPCHAIN_IMAGE_COUNT];
	VkImageViewCreateInfo view_infos[SWAPCHAIN_IMAGE_COUNT];
	uint32_t image_count;

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT]; // has to be MAX_FRAMES_IN_FLIGHT, as you need one for each frame index
	VkSemaphore render_done_semaphores[SWAPCHAIN_IMAGE_COUNT]; // has to be SWAPCHAIN_IMAGE_COUNT, as you need one for each swapchain image

	int32_t present_index;
	VkQueue present_queue;
} VulkanSurface;

typedef struct {
	VkInstance instance;

	VkPhysicalDevice physical_device;
	VkDevice logical_device;

	// Queues
	int32_t graphics_index, present_index;
	int32_t transfer_index, compute_index;

	VkQueue graphics_queue;

	// Commands
	VkCommandPool graphics_command_pool;
	VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

	// Syncrhonization primitives
	VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];

	// transient frame descriptor pools
	VkDescriptorPool frame_descriptor_pools[MAX_FRAMES_IN_FLIGHT];

	// engine push constant range
	VkPushConstantRange global_range;

	// Running
	uint32_t current_frame;

	bool initialized;
#ifdef DEV_BUILD
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
} GFX_Context;
bool gfx_startup(GFX_Context *context);
void gfx_shutdown(GFX_Context *context);

typedef struct {
	float2 position, uv;
	float4 color;
} Vertex2;
typedef struct {
	uint8_t *memory;
	uint32_t offset, capacity;
} SpriteBatch;

void push_rect(SpriteBatch *buffer, Rectangle rect, Color color) {
	float4 f_color = {
		.x = color.r / 255.f,
		.y = color.g / 255.f,
		.z = color.b / 255.f,
		.w = color.a / 255.f,
	};

	float x0 = (rect.x - 150.f) / 150.f;
	float y0 = (rect.y - 150.f) / 150.f;
	float x1 = (rect.x + rect.width - 150.f) / 150.f;
	float y1 = (rect.y + rect.height - 150.f) / 150.f;

	/* float u0 = src.x / image.width; */
	/* float v0 = src.y / image.height; */
	/* float u1 = (src.x + src.width) / image.width; */
	/* float v1 = (src.y + src.height) / image.height; */

	// clang-format off
    Vertex2 quad[] = {
        // pos      // tex
        (Vertex2){.position = {x0, y1}, .uv = {0.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {1.0f, 0.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x0, y0}, .uv = {0.0f, 0.0f}, .color = f_color }, // , .image_id = image_index}, 

        (Vertex2){.position = {x0, y1}, .uv = {0.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y1}, .uv = {1.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {1.0f, 0.0f}, .color = f_color }, // , .image_id = image_index}
    };
	// clang-format on

	memory_copy(buffer->memory + buffer->offset, quad, sizeof(quad));
	buffer->offset += sizeof(quad);
}

VulkanImage vulkan_image_make(GFX_Context *context, uint32_t width, uint32_t height, PixelFormat format, ImageUsageFlags usage);
VulkanBuffer vulkan_buffer_make(GFX_Context *context, uint64_t size, BufferMemory memory, BufferUsage usage);
VulkanSurface vulkan_surface_make(GFX_Context *context, OS_Surface *surface);
VulkanPipeline vulkan_compute_pipeline_make(GFX_Context *context, uint8_t *bytecode, uint64_t bytecode_size);
VulkanPipeline vulkan_grahpics_pipeline_make(GFX_Context *context, uint8_t *vertexcode, uint64_t vertexcode_size, uint8_t *fragmentcode, uint64_t fragmentcode_size);

void vulkan_image_barrier(VkCommandBuffer command_buffer, VkImage image, VkImageSubresourceRange range, VkImageLayout src, VkImageLayout dst);

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	os_display_startup();
	OS_Surface *main = os_surface_open(640, 360, s("vulkan_scratch"), 0);
	OS_Surface *popup = os_surface_open(300, 300, s("popup window"), 0);

	uint64_t start_time = os_time_ns();

	ArenaTemp scratch = arena_scratch_begin(0);

	GFX_Context context[] = { 0 };
	gfx_startup(context);
	VulkanSurface swapchains[] = { vulkan_surface_make(context, main), vulkan_surface_make(context, popup) };

	VulkanImage compute_image = vulkan_image_make(context, 640, 360, PIXEL_FORMAT_RGBA16_FLOAT, IMAGE_USAGE_STORAGE | IMAGE_USAGE_TRANSFER);

	// create compute pipeline
	uint64_t offset = arena_mark(scratch.arena);
	String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/test.compute.spv"));
	OS_Timestamp compute_ts = os_file_last_modified(s("assets/shaders/compute/bin/test.compute.spv"));
	VulkanPipeline c_pipeline = vulkan_compute_pipeline_make(context, compute_bytecode.text, compute_bytecode.length);
	arena_rewind(scratch.arena, offset);

	// create graphics pipeline
	offset = arena_mark(scratch.arena);
	String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/batch.vertex.spv"));
	String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/flat.fragment.spv"));
	VulkanPipeline g_pipeline = vulkan_grahpics_pipeline_make(context, vertex_bytecode.text, vertex_bytecode.length, fragment_bytecode.text, fragment_bytecode.length);
	arena_rewind(scratch.arena, offset);

	VulkanBuffer scratch_buffers[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t index = 0; index < countof(scratch_buffers); ++index) {
		scratch_buffers[index] = vulkan_buffer_make(context, MiB(1), BUFFER_MEMORY_SHARED, BUFFER_USAGE_STORAGE);
		vkMapMemory(context->logical_device, scratch_buffers[index].memory, 0, scratch_buffers[index].size, 0, (void **)&scratch_buffers[index].mapped);
	}

	SpriteBatch batch = {
		.memory = scratch_buffers[context->current_frame].mapped,
		.capacity = MiB(1),
	};

	bool is_open = true;
	while (is_open) {
		OS_Event event = { 0 };
		while (os_event_poll(&event)) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE:
					is_open = false;
					break;

				case OS_EVENT_TYPE_KEY_RELEASE:
					if (event.as.key.key_code == KEY_CODE_ESCAPE)
						is_open = false;
				default:
					break;
			}
		}

		push_rect(&batch, rect(0, 0, 50, 50), WHITE);
		push_rect(&batch, rect(60, 0, 50, 100), GREEN);

		// Frame resources
		VkFence *fence = &context->in_flight_fences[context->current_frame];
		VkCommandBuffer command_buffer = context->command_buffers[context->current_frame];
		VkDescriptorPool descriptor_pool = context->frame_descriptor_pools[context->current_frame];

		// Wait for frame resource availability
		vkWaitForFences(context->logical_device, 1, fence, VK_TRUE, UINT64_MAX);

		// Swapchain image acquisition
		uint32_t image_indices[SWAPCHAIN_IMAGE_COUNT];
		VkResult result = vkAcquireNextImageKHR(
			context->logical_device,
			swapchains[0].swapchain,
			UINT64_MAX,
			swapchains[0].image_available_semaphores[context->current_frame],
			VK_NULL_HANDLE, &image_indices[0]);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) // TODO: Handle out of date swapchain
			break;
		result = vkAcquireNextImageKHR(
			context->logical_device,
			swapchains[1].swapchain,
			UINT64_MAX,
			swapchains[1].image_available_semaphores[context->current_frame],
			VK_NULL_HANDLE, &image_indices[1]);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) // TODO: Handle out of date swapchain
			break;

		// reset frame resources
		vkResetCommandBuffer(command_buffer, 0);
		vkResetFences(context->logical_device, 1, fence);
		vkResetDescriptorPool(context->logical_device, descriptor_pool, 0);

		// Begin command recording
		VkCommandBufferBeginInfo cb_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};
		if (vkBeginCommandBuffer(command_buffer, &cb_begin_info) != VK_SUCCESS) {
			LOG_ERROR("failed to begin command buffer recording.");
			break;
		}
		// transitoin sawpchain images for drawing
		vulkan_image_barrier(
			command_buffer,
			swapchains[0].images[image_indices[0]],
			swapchains[0].view_infos[image_indices[0]].subresourceRange,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		vulkan_image_barrier(command_buffer,
			swapchains[1].images[image_indices[1]],
			swapchains[1].view_infos[image_indices[1]].subresourceRange,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		// transition compute storage image
		vulkan_image_barrier(command_buffer, compute_image.handle, compute_image.view_info.subresourceRange, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		// Bind compute pipeline & compute descriptor set
		VkDescriptorSetAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = c_pipeline.set_layouts + 0,
		};

		VkDescriptorSet compute_set = 0;
		vkAllocateDescriptorSets(context->logical_device, &alloc_info, &compute_set);
		VkDescriptorImageInfo image_info = {
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.imageView = compute_image.view,
		};
		VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = compute_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &image_info,
		};
		vkUpdateDescriptorSets(context->logical_device, 1, &write, 0, 0);

		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, c_pipeline.handle);
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, c_pipeline.layout, 0, 1, &compute_set, 0, 0);

		vkCmdDispatch(command_buffer, 40, 23, 1);
		vulkan_image_barrier(command_buffer, compute_image.handle, compute_image.view_info.subresourceRange, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VkImageBlit blit_info = {
			.srcOffsets[1] = {
			  .x = 640,
			  .y = 360,
			  .z = 1,
			},
			.srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseArrayLayer = 0, .layerCount = 1, .mipLevel = 0 },
			.dstOffsets[1] = {
			  .x = 640,
			  .y = 360,
			  .z = 1,
			},
			.dstSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseArrayLayer = 0, .layerCount = 1, .mipLevel = 0 },
		};
		vkCmdBlitImage(command_buffer, compute_image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchains[0].images[image_indices[0]], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_info, 0);

		for (uint32_t index = 0; index < countof(swapchains); ++index) {
			VulkanSurface *swapchain = &swapchains[index];
		}

		// Bind grahpics pipeline & grahpics descriptor set
		alloc_info = (VkDescriptorSetAllocateInfo){
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = g_pipeline.set_layouts + 0,
		};

		VkDescriptorSet grahpics_set = 0;
		vkAllocateDescriptorSets(context->logical_device, &alloc_info, &grahpics_set);
		VkDescriptorBufferInfo buffer_info = {
			.buffer = scratch_buffers[context->current_frame].handle,
			.offset = 0,
			.range = batch.offset,
		};
		write = (VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = grahpics_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &buffer_info,
		};
		vkUpdateDescriptorSets(context->logical_device, 1, &write, 0, 0);

		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeline.handle);
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeline.layout, 0, 1, &grahpics_set, 0, 0);

		VkRenderingAttachmentInfo attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = swapchains[1].views[image_indices[1]],
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.clearValue.color = { 1.0f, 0.0f, 1.0f, 1.0f },
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		};

		VkRenderingInfo renderpass_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = { .extent = { .width = 300, .height = 300 } },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &attachment_info,
		};
		vkCmdBeginRendering(command_buffer, &renderpass_info);

		VkViewport viewport = {
			.width = 300.f,
			.height = 300.f,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(command_buffer, 0, 1, &(VkRect2D){ .extent = { 300, 300 } });

		vkCmdDraw(command_buffer, batch.offset / sizeof(Vertex2), 1, 0, 0);

		vkCmdEndRendering(command_buffer);

		// transition swapchain images for presenting
		vulkan_image_barrier(
			command_buffer,
			swapchains[0].images[image_indices[0]],
			swapchains[0].view_infos[image_indices[0]].subresourceRange,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		vulkan_image_barrier(
			command_buffer,
			swapchains[1].images[image_indices[1]],
			swapchains[1].view_infos[image_indices[1]].subresourceRange,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
			LOG_INFO("failed to record command buffer.");
			break;
		}

		VkSemaphore wait_semaphores[] = { swapchains[0].image_available_semaphores[context->current_frame], swapchains[1].image_available_semaphores[context->current_frame] };
		VkSemaphore signal_semaphores[] = { swapchains[0].render_done_semaphores[image_indices[0]], swapchains[1].render_done_semaphores[image_indices[1]] };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };

		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = countof(wait_semaphores),
			.pWaitSemaphores = wait_semaphores,
			.pWaitDstStageMask = wait_stages,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
			.signalSemaphoreCount = countof(signal_semaphores),
			.pSignalSemaphores = signal_semaphores,
		};

		if (vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->in_flight_fences[context->current_frame]) != VK_SUCCESS) {
			LOG_ERROR("failed to submit command buffer to queue.");
			break;
		}

		VkSwapchainKHR swapchain_handles[] = { swapchains[0].swapchain, swapchains[1].swapchain };
		VkPresentInfoKHR present_info = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = countof(signal_semaphores),
			.pWaitSemaphores = signal_semaphores,
			.swapchainCount = countof(swapchains),
			.pSwapchains = swapchain_handles,
			.pImageIndices = image_indices,
		};
		result = vkQueuePresentKHR(swapchains[0].present_queue, &present_info);
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			uint32_t x = 0;
			(void)x;
			break;
		}

		OS_Timestamp current_ts = os_file_last_modified(s("assets/shaders/compute/bin/test.compute.spv"));
		if (compute_ts != current_ts) {
			LOG_INFO("hot-reloading...");
			vkDeviceWaitIdle(context->logical_device); // TODO: Proper synchronization for hot-reload
			// Destroy compute pipeline
			{
				for (uint32_t index = 0; index < countof(c_pipeline.shaders); ++index)
					if (c_pipeline.shaders[index])
						vkDestroyShaderModule(context->logical_device, c_pipeline.shaders[index], NULL);

				for (uint32_t set_index = 0; set_index < countof(c_pipeline.set_layouts); ++set_index)
					if (c_pipeline.set_layouts[set_index])
						vkDestroyDescriptorSetLayout(context->logical_device, c_pipeline.set_layouts[set_index], NULL);

				if (c_pipeline.layout)
					vkDestroyPipelineLayout(context->logical_device, c_pipeline.layout, NULL);
				if (c_pipeline.handle)
					vkDestroyPipeline(context->logical_device, c_pipeline.handle, NULL);

				memory_zero_struct(c_pipeline);
			}

			uint64_t offset = arena_mark(scratch.arena);
			compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/test.compute.spv"));
			c_pipeline = vulkan_compute_pipeline_make(context, compute_bytecode.text, compute_bytecode.length);
			arena_rewind(scratch.arena, offset);

			compute_ts = current_ts;
		}

		context->current_frame = (context->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

		batch.offset = 0;
		batch.memory = scratch_buffers[context->current_frame].mapped;
	}

	vkDeviceWaitIdle(context->logical_device);

	// destroy scratch buffers
	for (uint32_t index = 0; index < countof(scratch_buffers); ++index) {
		if (scratch_buffers[index].memory) {
			vkUnmapMemory(context->logical_device, scratch_buffers[index].memory);
			vkFreeMemory(context->logical_device, scratch_buffers[index].memory, NULL);
		}
		if (scratch_buffers[index].handle)
			vkDestroyBuffer(context->logical_device, scratch_buffers[index].handle, NULL);
	}

	// destroy images
	{
		if (compute_image.memory)
			vkFreeMemory(context->logical_device, compute_image.memory, 0);
		if (compute_image.view)
			vkDestroyImageView(context->logical_device, compute_image.view, 0);
		if (compute_image.handle)
			vkDestroyImage(context->logical_device, compute_image.handle, 0);

		memory_zero_struct(compute_image);
	}

	// Destroy compute pipeline
	{
		for (uint32_t index = 0; index < countof(c_pipeline.shaders); ++index)
			if (c_pipeline.shaders[index])
				vkDestroyShaderModule(context->logical_device, c_pipeline.shaders[index], NULL);
		for (uint32_t set_index = 0; set_index < countof(c_pipeline.set_layouts); ++set_index)
			if (c_pipeline.set_layouts[set_index])
				vkDestroyDescriptorSetLayout(context->logical_device, c_pipeline.set_layouts[set_index], NULL);

		if (c_pipeline.layout)
			vkDestroyPipelineLayout(context->logical_device, c_pipeline.layout, NULL);
		if (c_pipeline.handle)
			vkDestroyPipeline(context->logical_device, c_pipeline.handle, NULL);

		memory_zero_struct(c_pipeline);
	}

	// destroy graphics pipeline
	{
		for (uint32_t index = 0; index < countof(g_pipeline.shaders); ++index)
			if (g_pipeline.shaders[index])
				vkDestroyShaderModule(context->logical_device, g_pipeline.shaders[index], NULL);
		for (uint32_t set_index = 0; set_index < countof(g_pipeline.set_layouts); ++set_index)
			if (g_pipeline.set_layouts[set_index])
				vkDestroyDescriptorSetLayout(context->logical_device, g_pipeline.set_layouts[set_index], NULL);

		if (g_pipeline.layout)
			vkDestroyPipelineLayout(context->logical_device, g_pipeline.layout, NULL);
		if (g_pipeline.handle)
			vkDestroyPipeline(context->logical_device, g_pipeline.handle, NULL);

		memory_zero_struct(g_pipeline);
	}

	// Destroy swapchain/surface
	for (uint32_t index = 0; index < countof(swapchains); ++index) {
		for (uint32_t semaphore_index = 0; semaphore_index < countof(swapchains[index].image_available_semaphores); ++semaphore_index)
			if (swapchains[index].image_available_semaphores[semaphore_index])
				vkDestroySemaphore(context->logical_device, swapchains[index].image_available_semaphores[semaphore_index], NULL);
		for (uint32_t semaphore_index = 0; semaphore_index < countof(swapchains[index].render_done_semaphores); ++semaphore_index)
			if (swapchains[index].render_done_semaphores[semaphore_index])
				vkDestroySemaphore(context->logical_device, swapchains[index].render_done_semaphores[semaphore_index], NULL);

		for (uint32_t image_index = 0; image_index < swapchains[index].image_count; ++image_index)
			if (swapchains[index].views[image_index])
				vkDestroyImageView(context->logical_device, swapchains[index].views[image_index], NULL);

		if (swapchains[index].swapchain)
			vkDestroySwapchainKHR(context->logical_device, swapchains[index].swapchain, NULL);
		if (swapchains[index].handle)
			vkDestroySurfaceKHR(context->instance, swapchains[index].handle, NULL);
		memory_zero(swapchains + index, sizeof(swapchains[index]));
	}

	gfx_shutdown(context);

	arena_scratch_end(scratch);
	os_surface_close(popup);
	os_surface_close(main);
	os_display_shutdown();
	return 0;
}

VkImageUsageFlags gfx__to_vk_image_usage(PixelFormat format, ImageUsageFlags usage) {
	VkImageUsageFlags result = 0;
	if (FLAG_GET(usage, IMAGE_USAGE_RENDER)) {
		if (format == PIXEL_FORMAT_D24_UNORM_S8_UINT || format == PIXEL_FORMAT_D32_FLOAT)
			result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		else
			result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (FLAG_GET(usage, IMAGE_USAGE_SAMPLE))
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (FLAG_GET(usage, IMAGE_USAGE_STORAGE))
		result |= VK_IMAGE_USAGE_STORAGE_BIT;

	if (FLAG_GET(usage, IMAGE_USAGE_TRANSFER))
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	return result;
}

VkBufferUsageFlags gfx__to_vk_buffer_usage(BufferUsage usage) {
	VkBufferUsageFlags result = 0;
	if (FLAG_GET(usage, BUFFER_USAGE_UNIFORM))
		result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_STORAGE))
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_VERTEX))
		result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_INDEX))
		result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_TRANSFER))
		result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	return result;
}

VkFormat gfx__pixel_format_to_vk_format(PixelFormat format) {
	switch (format) {
		case PIXEL_FORMAT_RGBA8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case PIXEL_FORMAT_RGBA8_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case PIXEL_FORMAT_RGBA16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case PIXEL_FORMAT_R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case PIXEL_FORMAT_D32_FLOAT:
			return VK_FORMAT_D32_SFLOAT;
		case PIXEL_FORMAT_D24_UNORM_S8_UINT:
			return VK_FORMAT_D24_UNORM_S8_UINT;
	}

	return VK_FORMAT_UNDEFINED;
}

VkImageAspectFlags gfx__pixel_format_to_aspect(PixelFormat format) {
	if (format == PIXEL_FORMAT_D24_UNORM_S8_UINT || format == PIXEL_FORMAT_D32_FLOAT)
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	return VK_IMAGE_ASPECT_COLOR_BIT;
}

uint32_t gfx__find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

	for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
		if ((type_filter & (1 << index)) && (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
			return index;
		}
	}

	LOG_ERROR("Failed to find suitable memory type!");
	ASSERT(false);
	return 0;
}

VulkanImage vulkan_image_make(GFX_Context *context, uint32_t width, uint32_t height, PixelFormat format, ImageUsageFlags usage) {
	VulkanImage result = { 0 };
	LOG_DEBUG("creating vulkan image.");

	bool ok = true;
	VkImageUsageFlags vk_usage = gfx__to_vk_image_usage(format, usage);
	VkFormat vk_format = gfx__pixel_format_to_vk_format(format);
	VkImageAspectFlags aspect = gfx__pixel_format_to_aspect(format);
	result.width = width, result.height = height;

	if (ok) { // make vulkan image handle
		result.image_info = (VkImageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.flags = 0,
			.format = vk_format,
			.extent = {
			  .width = width,
			  .height = height,
			  .depth = 1,
			},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = 1,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};

		ok = vkCreateImage(context->logical_device, &result.image_info, 0, &result.handle) == VK_SUCCESS;
	}

	if (ok) { // allocate memory
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(context->logical_device, result.handle, &memory_requirements);

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = gfx__find_memory_type(context->physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
		};

		ok = vkAllocateMemory(context->logical_device, &allocate_info, 0, &result.memory) == VK_SUCCESS;
	}

	if (ok) // bind memory to handle
		vkBindImageMemory(context->logical_device, result.handle, result.memory, 0);

	if (ok) { // create image view
		result.view_info = (VkImageViewCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = result.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = result.image_info.format,
			.components = {
			  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
			  .aspectMask = aspect,
			  .baseMipLevel = 0,
			  .levelCount = 1,
			  .baseArrayLayer = 0,
			  .layerCount = 1,
			}
		};

		ok = vkCreateImageView(context->logical_device, &result.view_info, 0, &result.view) == VK_SUCCESS;
	}

	if (ok == false) { // remove half-made resources on error
		if (result.view)
			vkDestroyImageView(context->logical_device, result.view, 0);
		if (result.memory)
			vkFreeMemory(context->logical_device, result.memory, 0);
		if (result.handle)
			vkDestroyImage(context->logical_device, result.handle, 0);

		memory_zero(&result, sizeof(VulkanImage));
	}

	/* LOG_INFO("image loaded successfuly (%ux%u | %s)", indexof(context->image_pool, image), width, height, image_format_to_string[format]); */
	return result;
}

VulkanBuffer vulkan_buffer_make(GFX_Context *context, uint64_t size, BufferMemory memory, BufferUsage usage) {
	VulkanBuffer result = { 0 };
	LOG_DEBUG("creating vulkan buffer.");

	bool ok = true;
	VkBufferUsageFlags vk_usage = gfx__to_vk_buffer_usage(usage);
	VkMemoryPropertyFlags memory_flags = memory == BUFFER_MEMORY_LOCAL
		? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		: VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (ok) { // create buffer handle
		result.size = size;

		uint32_t family_indices[] = { context->graphics_index };

		result.info = (VkBufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = result.size,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 1,
			.pQueueFamilyIndices = family_indices
		};

		ok = vkCreateBuffer(context->logical_device, &result.info, 0, &result.handle) == VK_SUCCESS;
	}

	if (ok) { // allocate buffer memory
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(context->logical_device, result.handle, &memory_requirements);

		uint32_t memory_type_index = gfx__find_memory_type(context->physical_device, memory_requirements.memoryTypeBits, memory_flags);
		size_t allocation_size = memory_requirements.size;

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = memory_type_index,
		};

		ok = vkAllocateMemory(context->logical_device, &allocate_info, 0, &result.memory) == VK_SUCCESS;
	}

	if (ok) // bind memory
		ok = vkBindBufferMemory(context->logical_device, result.handle, result.memory, 0) == VK_SUCCESS;

	if (ok == false) { // remove half-made resources on error
		if (result.memory)
			vkFreeMemory(context->logical_device, result.memory, 0);
		if (result.handle)
			vkDestroyBuffer(context->logical_device, result.handle, 0);
		memory_zero(&result, sizeof(VulkanBuffer));
	}

	return result;
}

VulkanPipeline vulkan_compute_pipeline_make(GFX_Context *context, uint8_t *bytecode, uint64_t bytecode_size) {
	LOG_DEBUG("creating vulkan compute pipeline.");
	VulkanPipeline result = { 0 };

	bool ok = bytecode && bytecode_size > 0;
	if (ok == false)
		LOG_WARN("invalid shader bytecode passed.");

	if (ok) { // create shader module
		VkShaderModuleCreateInfo csm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)bytecode,
			.codeSize = bytecode_size,
		};

		ok = vkCreateShaderModule(context->logical_device, &csm_create_info, NULL, &result.shaders[SHADER_STAGE_COMPUTE]) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create compute pipeline shader module.");
	}

	if (ok) { // create descriptor set layouts
		// TODO: Don't hard-code the layouts

		VkDescriptorSetLayoutBinding bindings[] = {
			[0] = {
			  .binding = 0,
			  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  .descriptorCount = 1,
			  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		};

		VkDescriptorSetLayoutCreateInfo dsl_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = countof(bindings),
			.pBindings = bindings,
		};

		ok = vkCreateDescriptorSetLayout(context->logical_device, &dsl_create_info, NULL, result.set_layouts) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create compute pipeline descriptor set layout 0");
	}

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = result.set_layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &context->global_range,
		};

		ok = vkCreatePipelineLayout(context->logical_device, &pl_create_info, NULL, &result.layout) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create compute pipeline layout.");
	}

	if (ok) { // create compute pipeline
		VkPipelineShaderStageCreateInfo compute_stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = result.shaders[SHADER_STAGE_COMPUTE],
			.pName = "main",
		};

		VkComputePipelineCreateInfo cp_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = compute_stage,
			.layout = result.layout,
		};

		ok = vkCreateComputePipelines(context->logical_device, 0, 1, &cp_create_info, NULL, &result.handle) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create compute pipeline.");
	}

	if (ok == false) { // remove half-made resources on error
		for (uint32_t index = 0; index < countof(result.shaders); ++index)
			if (result.shaders[index])
				vkDestroyShaderModule(context->logical_device, result.shaders[index], NULL);
		for (uint32_t set_index = 0; set_index < countof(result.set_layouts); ++set_index)
			if (result.set_layouts[set_index])
				vkDestroyDescriptorSetLayout(context->logical_device, result.set_layouts[set_index], NULL);

		if (result.layout)
			vkDestroyPipelineLayout(context->logical_device, result.layout, NULL);
		if (result.handle)
			vkDestroyPipeline(context->logical_device, result.handle, NULL);

		memory_zero_struct(result);
	}

	return result;
}
VulkanPipeline vulkan_grahpics_pipeline_make(GFX_Context *context, uint8_t *vertexcode, uint64_t vertexcode_size, uint8_t *fragmentcode, uint64_t fragmentcode_size) {
	LOG_DEBUG("creating vulkan grahpics pipeline.");
	VulkanPipeline result = { 0 };

	bool ok = true;

	if (ok) { // check validitiy of shader code
		ok &= vertexcode && vertexcode_size > 0;
		ok &= fragmentcode && fragmentcode_size > 0;

		if (ok == false)
			LOG_WARN("invalid shader bytecode passed.");
	}

	if (ok) { // create shader module
		VkShaderModuleCreateInfo vsm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)vertexcode,
			.codeSize = vertexcode_size,
		};

		VkShaderModuleCreateInfo fsm_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.pCode = (void *)fragmentcode,
			.codeSize = fragmentcode_size,
		};

		ok &= vkCreateShaderModule(context->logical_device, &vsm_create_info, NULL, &result.shaders[SHADER_STAGE_VERTEX]) == VK_SUCCESS;
		ok &= vkCreateShaderModule(context->logical_device, &fsm_create_info, NULL, &result.shaders[SHADER_STAGE_FRAGMENT]) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create vertex/fragment shader module.");
	}

	if (ok) { // create descriptor set layouts
		// TODO: Don't hard-code the layouts

		VkDescriptorSetLayoutBinding bindings[] = {
			[0] = {
			  .binding = 0,
			  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			  .descriptorCount = 1,
			  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			},
		};

		VkDescriptorSetLayoutCreateInfo dsl_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = countof(bindings),
			.pBindings = bindings,
		};

		ok = vkCreateDescriptorSetLayout(context->logical_device, &dsl_create_info, NULL, result.set_layouts) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create pipeline descriptor set layout 0");
	}

	if (ok) { // create pipeline layout
		VkPipelineLayoutCreateInfo pl_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = result.set_layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &context->global_range,
		};

		ok = vkCreatePipelineLayout(context->logical_device, &pl_create_info, NULL, &result.layout) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create pipeline layout.");
	}

	if (ok) { // create graphics pipeline
		VkPipelineShaderStageCreateInfo shader_stages[] = {
			{
			  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			  .stage = VK_SHADER_STAGE_VERTEX_BIT,
			  .module = result.shaders[SHADER_STAGE_VERTEX],
			  .pName = "main",
			},
			{
			  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			  .module = result.shaders[SHADER_STAGE_FRAGMENT],
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
			.cullMode = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
		};

		VkPipelineMultisampleStateCreateInfo mss_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.sampleShadingEnable = VK_FALSE,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.minSampleShading = 1.0f,
		};

		VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
			{
			  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
			  .blendEnable = VK_TRUE,

			  // Color: result = src.rgb * src.a + dst.rgb * (1 - src.a)
			  .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			  .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			  .colorBlendOp = VK_BLEND_OP_ADD,

			  // Alpha: result = src.a * 1 + dst.a * (1 - src.a)
			  .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			  .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			  .alphaBlendOp = VK_BLEND_OP_ADD,
			},
		};

		VkPipelineColorBlendStateCreateInfo cbs_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = countof(color_blend_attachments),
			.pAttachments = color_blend_attachments,
		};

		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthBoundsTestEnable = VK_FALSE,
			.minDepthBounds = 0.0f,
			.maxDepthBounds = 1.0f
		};

		VkPipelineRenderingCreateInfo r_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = countof(color_blend_attachments),
			.pColorAttachmentFormats = (VkFormat[]){ VK_FORMAT_B8G8R8A8_SRGB },
		};

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
			.layout = result.layout,
		};

		ok = vkCreateGraphicsPipelines(context->logical_device, 0, 1, &gp_create_info, NULL, &result.handle) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create compute pipeline.");
	}

	if (ok == false) { // remove half-made resources on error
		for (uint32_t index = 0; index < countof(result.shaders); ++index)
			if (result.shaders[index])
				vkDestroyShaderModule(context->logical_device, result.shaders[index], NULL);
		for (uint32_t set_index = 0; set_index < countof(result.set_layouts); ++set_index)
			if (result.set_layouts[set_index])
				vkDestroyDescriptorSetLayout(context->logical_device, result.set_layouts[set_index], NULL);

		if (result.layout)
			vkDestroyPipelineLayout(context->logical_device, result.layout, NULL);
		if (result.handle)
			vkDestroyPipeline(context->logical_device, result.handle, NULL);

		memory_zero_struct(result);
	}

	return result;
}

VulkanSurface vulkan_surface_make(GFX_Context *context, OS_Surface *surface) {
	VulkanSurface result = { 0 };
	ArenaTemp scratch = arena_scratch_begin(0);
	LOG_DEBUG("creating vulkan surface.");

	bool ok = true;

	if (ok) { // create surface
		VkXcbSurfaceCreateInfoKHR surface_create_info = {
			.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
			.connection = os_native_display_handle(),
			.window = (uint32_t)(uint64_t)os_native_surface_handle(surface),
		}; // TODO: Have os decide this

		ok = vkCreateXcbSurfaceKHR(context->instance, &surface_create_info, 0, &result.handle) == VK_SUCCESS;
		if (ok == false)
			LOG_WARN("failed to create vulkan surface.");
	}

	VkSurfaceCapabilitiesKHR capabilities;

	uint32_t surface_format_count = 0;
	VkSurfaceFormatKHR *surface_formats;

	uint32_t present_mode_count = 0;
	VkPresentModeKHR *present_modes;

	if (ok) { // query surface suitability
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device, result.handle, &capabilities);

		vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, result.handle, &surface_format_count, 0);
		surface_formats = arena_push_count(scratch.arena, VkSurfaceFormatKHR, surface_format_count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, result.handle, &surface_format_count, surface_formats);

		vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device, result.handle, &present_mode_count, 0);
		present_modes = arena_push_count(scratch.arena, VkPresentModeKHR, present_mode_count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device, result.handle, &present_mode_count, present_modes);

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, 0);
		VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, queue_family_properties);

		result.present_index = -1;
		for (uint32_t index = 0; index < queue_family_count; ++index) {
			VkQueueFlags flags = queue_family_properties[index].queueFlags;

			VkBool32 present_support = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device, index, result.handle, &present_support);
			if (present_support && result.present_index == -1) {
				result.present_index = index;
				break;
			}
		}

		ok &= surface_format_count > 0; // valid surface formats available
		ok &= present_mode_count > 0; // valid present mode available
		ok &= result.present_index != -1; // supports present queue
	}

	if (ok) { // create swapchain
		// get queue handle
		vkGetDeviceQueue(context->logical_device, result.present_index, 0, &result.present_queue);

		VkSurfaceFormatKHR selected_format = surface_formats[0];
		for (uint32_t format_index = 0; format_index < surface_format_count; ++format_index) {
			if (surface_formats[format_index].format == VK_FORMAT_B8G8R8A8_SRGB &&
				surface_formats[format_index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { // ideal format
				selected_format = surface_formats[format_index];
				break;
			}
		}

		VkPresentModeKHR selected_present_mode = present_modes[0];
		for (uint32_t mode_index = 0; mode_index < present_mode_count; mode_index++) {
			if (present_modes[mode_index] == VK_PRESENT_MODE_MAILBOX_KHR) // ideal presentation mode
				selected_present_mode = present_modes[mode_index];
		}

		uint32x2 surface_size = os_surface_size(surface);
		float dpi = os_surface_dpi(surface);

		VkExtent2D selected_extents =
			capabilities.currentExtent.width != UINT32_MAX
			? capabilities.currentExtent
			: (VkExtent2D){ .width = (uint32_t)(surface_size.x * dpi), .height = (uint32_t)((float)surface_size.y * dpi) };

		uint32_t queue_family_indices[] = { (uint32_t)context->graphics_index, (uint32_t)result.present_index };

		uint32_t image_count = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
			image_count = capabilities.maxImageCount;

		image_count = MIN(image_count, SWAPCHAIN_IMAGE_COUNT);

		result.swapchain_info = (VkSwapchainCreateInfoKHR){
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = result.handle,
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
			result.swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			result.swapchain_info.queueFamilyIndexCount = 2;
			result.swapchain_info.pQueueFamilyIndices = queue_family_indices;
		} else
			result.swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

		ok = vkCreateSwapchainKHR(context->logical_device, &result.swapchain_info, NULL, &result.swapchain) == VK_SUCCESS;
	}

	if (ok) { // get the swapchain images & create image views
		vkGetSwapchainImagesKHR(context->logical_device, result.swapchain, &result.image_count, NULL);
		vkGetSwapchainImagesKHR(context->logical_device, result.swapchain, &result.image_count, result.images);

		for (uint32_t image_index = 0; image_index < result.image_count; ++image_index) {
			result.view_infos[image_index] = (VkImageViewCreateInfo){
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = result.images[image_index],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = result.swapchain_info.imageFormat,
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

			ok &= vkCreateImageView(context->logical_device, result.view_infos + image_index, NULL, &result.views[image_index]) == VK_SUCCESS;
		}
	}

	if (ok) { // create semaphores
		VkSemaphoreCreateInfo s_create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		for (uint32_t index = 0; index < countof(result.image_available_semaphores); ++index)
			ok &= vkCreateSemaphore(context->logical_device, &s_create_info, 0, result.image_available_semaphores + index) == VK_SUCCESS;
		for (uint32_t index = 0; index < countof(result.render_done_semaphores); ++index)
			ok &= vkCreateSemaphore(context->logical_device, &s_create_info, 0, result.render_done_semaphores + index) == VK_SUCCESS;
	}

	if (ok == false) { // remove half-made resources on error
		for (uint32_t index = 0; index < SWAPCHAIN_IMAGE_COUNT; ++index)
			if (result.views[index])
				vkDestroyImageView(context->logical_device, result.views[index], NULL);
		if (result.swapchain)
			vkDestroySwapchainKHR(context->logical_device, result.swapchain, NULL);
		if (result.handle)
			vkDestroySurfaceKHR(context->instance, result.handle, 0);
		memory_zero(&result, sizeof(VulkanSurface));
		LOG_ERROR("failed to create vulkan swapchain.");
	}

	arena_scratch_end(scratch);
	return result;
}

static inline void gfx__load_debug_extensions(GFX_Context *device);
static inline VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData);

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

VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {
	.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
	.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
	.pfnUserCallback = vulkan_debug_callback,
	.pUserData = 0
};

// EXTENSIONS
static VKAPI_ATTR VkResult VKAPI_CALL STUB_vkCreateDebugUtilsMessenger(VkInstance i, const VkDebugUtilsMessengerCreateInfoEXT *c, const VkAllocationCallbacks *a, VkDebugUtilsMessengerEXT *m) { return VK_ERROR_EXTENSION_NOT_PRESENT; }
static VKAPI_ATTR void VKAPI_CALL STUB_vkDestroyDebugUtilsMessenger(VkInstance i, VkDebugUtilsMessengerEXT m, const VkAllocationCallbacks *a) {}
static VKAPI_ATTR VkResult VKAPI_CALL STUB_vkSetDebugUtilsObjectName(VkDevice d, const VkDebugUtilsObjectNameInfoEXT *n) { return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL STUB_vkCmdBeginDebugUtilsLabel(VkCommandBuffer c, const VkDebugUtilsLabelEXT *l) {}
static VKAPI_ATTR void VKAPI_CALL STUB_vkCmdEndDebugUtilsLabel(VkCommandBuffer c) {}

PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger = STUB_vkCreateDebugUtilsMessenger;
PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessenger = STUB_vkDestroyDebugUtilsMessenger;
PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = STUB_vkSetDebugUtilsObjectName;
PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabel = STUB_vkCmdBeginDebugUtilsLabel;
PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabel = STUB_vkCmdEndDebugUtilsLabel;

bool gfx__vk_instance_make(GFX_Context *context);
bool gfx__vk_device_make(GFX_Context *context);
bool gfx__vk_command_buffers_make(GFX_Context *context);
bool gfx__vk_synchronization_objects_make(GFX_Context *context);
bool gfx__vk_descriptor_pool(GFX_Context *context);

bool gfx_startup(GFX_Context *context) {
	bool ok = true;

	if (ok)
		ok = gfx__vk_instance_make(context);

	if (ok)
		ok = gfx__vk_device_make(context);

	if (ok)
		ok = gfx__vk_command_buffers_make(context);

	if (ok)
		ok = gfx__vk_synchronization_objects_make(context);

	if (ok)
		ok = gfx__vk_descriptor_pool(context);

	if (ok)
		context->global_range = (VkPushConstantRange){
			.stageFlags = VK_SHADER_STAGE_ALL,
			.offset = 0,
			.size = 128
		};

	if (ok) {
		context->initialized = true;
	} else
		LOG_WARN("failed to initialize vulkan context.");

	return ok;
}

void gfx_shutdown(GFX_Context *context) {
#ifdef DEV_BUILD
	if (context->debug_messenger)
		vkDestroyDebugUtilsMessenger(context->instance, context->debug_messenger, 0);
#endif

	for (uint32_t index = 0; index < countof(context->frame_descriptor_pools); ++index) {
		if (context->frame_descriptor_pools[index])
			vkDestroyDescriptorPool(context->logical_device, context->frame_descriptor_pools[index], 0);
	}

	for (uint32_t index = 0; index < countof(context->in_flight_fences); ++index) {
		if (context->in_flight_fences[index])
			vkDestroyFence(context->logical_device, context->in_flight_fences[index], 0);
	}

	if (context->graphics_command_pool)
		vkDestroyCommandPool(context->logical_device, context->graphics_command_pool, 0);
	if (context->logical_device)
		vkDestroyDevice(context->logical_device, 0);
	if (context->instance)
		vkDestroyInstance(context->instance, 0);

	memory_zero(context, sizeof(GFX_Context));
}

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
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

void gfx__load_debug_extensions(GFX_Context *device) {
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

bool gfx__vk_instance_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan instance.");
	ArenaTemp scratch = arena_scratch_begin(0);

	uint32_t required_extension_count = 0;
	const char **required_extensions = os_surface_vulkan_extensions(&required_extension_count);

	bool ok = true;
	if (ok) { // validate if required extensions are present
#ifdef DEV_BUILD
		const char **debug_extensions = arena_push_count(scratch.arena, const char **, required_extension_count + 1);
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

		ok = vkCreateInstance(&instance_info, 0, &context->instance) == VK_SUCCESS;
	}

	if (ok) { // load debug extension pointers & craete debug util
#ifdef DEV_BUILD
		gfx__load_debug_extensions(context);
		vkCreateDebugUtilsMessenger(context->instance, &debug_utils_create_info, 0, &context->debug_messenger);
#endif
	}

	arena_scratch_end(scratch);
	return ok;
}

const char *required_device_extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

bool gfx__device_suitable(GFX_Context *context, VkPhysicalDevice device) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = true;
	if (ok) { // check if vulkan 1.3 is supported
		VkPhysicalDeviceProperties physical_device_properties = { 0 };
		vkGetPhysicalDeviceProperties(device, &physical_device_properties);

		ok = physical_device_properties.apiVersion >= VK_API_VERSION_1_3;
	}

	/* if (ok) { // check for present support  */
	/* 	uint32_t queue_family_count = 0; */
	/* 	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, 0); */

	/* 	VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count); */
	/* 	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_family_properties); */

	/* 	VkBool32 supports_present = VK_FALSE; */
	/* 	for (uint32_t queue_family_index = 0; queue_family_index < queue_family_count; ++queue_family_index) { */
	/* 		vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_family_index, context->surface.handle, &supports_present); */

	/* 		if ((queue_family_properties[queue_family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supports_present) */
	/* 			break; */
	/* 	} */

	/* 	if (supports_present == false) */
	/* 		ok = false; */
	/* } */

	if (ok) { // validate if required device extensions are present
		uint32_t available_extension_count = 0;
		vkEnumerateDeviceExtensionProperties(device, 0, &available_extension_count, 0);

		VkExtensionProperties *available_extensions = arena_push_count(scratch.arena, VkExtensionProperties, available_extension_count);
		vkEnumerateDeviceExtensionProperties(device, 0, &available_extension_count, available_extensions);

		ok = gfx__validate_extensions(required_device_extensions, countof(required_device_extensions), available_extensions, available_extension_count);
	}

	if (ok) { // validate if desired features are present
		VkPhysicalDeviceVulkan13Features vk13_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.dynamicRendering = VK_TRUE,
		};
		VkPhysicalDeviceFeatures2 vk_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vk13_features,
		};
		vkGetPhysicalDeviceFeatures2(device, &vk_features);

		if (vk13_features.dynamicRendering == false)
			ok = false;
	}

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__vk_device_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan device.");
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = true;
	if (ok) { // select physical device

		uint32_t physical_device_count = 0;
		vkEnumeratePhysicalDevices(context->instance, &physical_device_count, 0);

		VkPhysicalDevice *physical_devices = arena_push_count(scratch.arena, VkPhysicalDevice, physical_device_count);
		vkEnumeratePhysicalDevices(context->instance, &physical_device_count, physical_devices);

		for (uint32_t physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index) {
			if (gfx__device_suitable(context, physical_devices[physical_device_index])) {
				context->physical_device = physical_devices[physical_device_index];
				break;
			}
		}

		if (context->physical_device == 0) {
			LOG_ERROR("failed to find suitable graphics card with Vulkan 1.3 support.");
			ok = false;
		}
	}

	if (ok) { // find queue indices
		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, 0);

		VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, queue_family_properties);

		context->graphics_index = -1, context->present_index = -1,
		context->transfer_index = -1, context->compute_index = -1;

		for (uint32_t index = 0; index < queue_family_count; ++index) {
			VkQueueFlags flags = queue_family_properties[index].queueFlags;

			/* VkBool32 present_support = false; */
			/* vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device, index, context->surface.handle, &present_support); */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) && context->graphics_index == -1) {
				context->graphics_index = index;
				context->present_index = index;
				/* ASSERT(present_support && "grahpics index does not support presenting"); */
			}

			/* if (present_support && context->present_index == -1) */
			/* 	context->present_index = index; */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_TRANSFER_BIT) && context->transfer_index == -1) // dedicated transfer
				context->transfer_index = index;

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_COMPUTE_BIT) && context->compute_index == -1) // dedicated compute
				context->compute_index = index;
		}

		if (context->graphics_index == -1 || context->present_index == -1) {
			LOG_ERROR("failed to find graphics queues");
			ok = false;
		}
	}

	if (ok) { // create vulkan device
		float queue_priortiy = 0.5f;

		uint32_t queue_family_indices[] = { context->graphics_index }; // TODO: Multiple queues
		VkDeviceQueueCreateInfo queue_infos[] = {
			{
			  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			  .queueFamilyIndex = context->graphics_index,
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

		ok = vkCreateDevice(context->physical_device, &device_info, 0, &context->logical_device) == VK_SUCCESS;
	}

	if (ok) // get the queue handles from indices
		vkGetDeviceQueue(context->logical_device, context->graphics_index, 0, &context->graphics_queue);

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__vk_command_buffers_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan command buffers.");

	bool ok = true;
	if (ok) { // make graphics command pool
		VkCommandPoolCreateInfo cp_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = context->graphics_index
		};
		ok = vkCreateCommandPool(context->logical_device, &cp_create_info, 0, &context->graphics_command_pool) == VK_SUCCESS;
	}

	/* if (ok) { // make transfer command pool */
	/* 	VkCommandPoolCreateInfo cp_create_info = { */
	/* 		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, */
	/* 		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, */
	/* 		.queueFamilyIndex = context->graphics_index */
	/* 	}; */

	/* 	ok = vkCreateCommandPool(context->logical_device, &cp_create_info, 0, &context->transfer_command_pool) == VK_SUCCESS; */
	/* } */

	if (ok) { // allocate command buffers
		VkCommandBufferAllocateInfo cb_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = context->graphics_command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = countof(context->command_buffers)
		};
		ok = vkAllocateCommandBuffers(context->logical_device, &cb_allocate_info, context->command_buffers) == VK_SUCCESS;
	}

	return ok;
}

bool gfx__vk_synchronization_objects_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan synchronization objects.");

	bool ok = true;
	if (ok) { // make synchronization fences
		VkFenceCreateInfo f_create_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};

		for (uint32_t index = 0; index < countof(context->in_flight_fences); ++index)
			ok &= vkCreateFence(context->logical_device, &f_create_info, 0, context->in_flight_fences + index) == VK_SUCCESS;
	}

	return ok;
}

bool gfx__vk_descriptor_pool(GFX_Context *context) {
	LOG_DEBUG("initializing global vulkan descriptor pools.");

	bool ok = true;
	if (ok) { // create global descriptor pool per frame in flight
		VkDescriptorPoolSize sizes[] = {
			{
			  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  .descriptorCount = 1000,
			},
		};

		VkDescriptorPoolCreateInfo dp_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.poolSizeCount = countof(sizes),
			.pPoolSizes = sizes,
			.maxSets = 1000,
		};

		for (uint32_t frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; ++frame_index)
			ok &= vkCreateDescriptorPool(context->logical_device, &dp_create_info, 0, &context->frame_descriptor_pools[frame_index]) == VK_SUCCESS;
	}

	return ok;
}

void vulkan_image_barrier(VkCommandBuffer command_buffer, VkImage image, VkImageSubresourceRange range, VkImageLayout src, VkImageLayout dst) {
	VkPipelineStageFlags src_stage = 0;
	VkPipelineStageFlags dst_stage = 0;
	VkAccessFlags src_access = 0;
	VkAccessFlags dst_access = 0;

	switch (src) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			src_access = 0;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			src_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			src_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			src_access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			src_access = VK_ACCESS_SHADER_READ_BIT;
			break;

		default:
			/* LOG_WARN("unhandled source transition layout '%u', defaulting to ALL_COMMANDS", src); */
			src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			src_access = VK_ACCESS_MEMORY_WRITE_BIT;
			break;
	}

	switch (dst) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dst_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dst_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dst_access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dst_access = 0;
			break;

		default:
			/* LOG_WARN("unhandled target transition layout '%u', defaulting to ALL_COMMANDS", dst); */
			dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			dst_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			break;
	}

	VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = src,
		.newLayout = dst,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = range,
	};

	vkCmdPipelineBarrier(
		command_buffer,
		src_stage, dst_stage,
		0,
		0, NULL,
		0, NULL,
		1, &image_barrier);
}
