#include "core/arena.h"
#include "core/logger.h"

#include "platform.h"
#include "renderer/vk_renderer.h"

#include "common.h"
#include <string.h>

void draw_frame(Arena *arena, VulkanContext *context, Platform *platform, Buffer *vertex_buffer);
void resize_callback(Platform *platform, uint32_t width, uint32_t height);
void update_uniforms(VulkanContext *context, Platform *platform);

// clang-format off
const Vertex vertices[] = {
	// Front (+Z)
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }}, // Bottom-left
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }}, // Bottom-right
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }}, // Top-right
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},

	// Back (-Z)
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }}, // Bottom-right
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 0.0f }}, // Bottom-left
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }}, // Top-left
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},

	// Left (-X)
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},

	// Right (+X)
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},

	// Bottom (-Y)
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},

	// Top (+Y)
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 0.0f }}
	};
// clang-format on 

const uint16_t indices[6] = {
	0, 1, 2, 2, 3, 0
};

static struct State {
	bool resized;
	uint64_t start_time;

	Arena *permanent, *frame;
} state;

int main(void) {
	state.permanent = arena_alloc();
	state.frame = arena_alloc();

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);
	logger_set_level(LOG_LEVEL_DEBUG);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VulkanContext context = { 0 };
	Platform *platform = platform_startup(state.permanent, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	state.start_time = platform_time_ms(platform);

	LOG_INFO("Logical pixel dimensions { %d, %d }", platform->logical_width, platform->logical_height);
	LOG_INFO("Physical pixel dimensions { %d, %d }", platform->physical_width, platform->physical_height);
	platform_set_physical_dimensions_callback(platform, resize_callback);

	vulkan_renderer_create(state.permanent, platform, &context);

	// ================== DYNAMIC ==================
	vulkan_create_descriptor_set_layout(&context);
	vulkan_create_graphics_pipline(&context);

	vulkan_create_texture_image(&context);
	vulkan_create_texture_image_view(&context);
	vulkan_create_texture_sampler(&context);

	Buffer* vertex_buffer = vulkan_create_buffer(state.permanent, &context, BUFFER_TYPE_VERTEX, sizeof(vertices), (void*)vertices);
	vertex_buffer->vertex_count = array_count(vertices);
	// vulkan_create_index_buffer(vulkan_arena, &context);

	vulkan_create_uniform_buffers(&context);
	vulkan_create_descriptor_pool(&context);
	vulkan_create_descriptor_set(&context);


	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
		draw_frame(state.frame, &context, platform, vertex_buffer);
	}

	vkDeviceWaitIdle(context.device.logical);

	return 0;
}

void draw_frame(Arena *arena, VulkanContext *context, Platform *platform, Buffer *vertex_buffer) {
	vkWaitForFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame], VK_TRUE, UINT64_MAX);

	uint32_t image_index = 0;
	VkResult result = vkAcquireNextImageKHR(context->device.logical, context->swapchain.handle, UINT64_MAX, context->image_available_semaphores[context->current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		vulkan_recreate_swapchain(context, platform);
		LOG_INFO("Recreating Swapchain");
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		LOG_ERROR("Failed to acquire swapchain image!");
	}

	vkResetFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame]);

	vkResetCommandBuffer(context->command_buffers[context->current_frame], 0);
	vulkan_command_buffer_draw(context, vertex_buffer, image_index);

	update_uniforms(context, platform);

	VkSemaphore wait_semaphores[] = { context->image_available_semaphores[context->current_frame] };
	VkSemaphore signal_semaphores[] = { context->render_finished_semaphores[image_index] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = array_count(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &context->command_buffers[context->current_frame],
		.signalSemaphoreCount = array_count(signal_semaphores),
		.pSignalSemaphores = signal_semaphores
	};

	if (vkQueueSubmit(context->device.graphics_queue, 1, &submit_info, context->in_flight_fences[context->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to submit draw command buffer");
		return;
	}

	VkSwapchainKHR swapchains[] = { context->swapchain.handle };
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signal_semaphores,
		.swapchainCount = 1,
		.pSwapchains = swapchains,
		.pImageIndices = &image_index,
	};

	context->current_frame = (context->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	result = vkQueuePresentKHR(context->device.present_queue, &present_info);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || state.resized) {
		vulkan_recreate_swapchain(context, platform);
		LOG_INFO("Recreating Swapchain");
		state.resized = false;
		return;
	} else if (result != VK_SUCCESS) {
		LOG_ERROR("Failed to acquire swapchain image!");
		return;
	}
}

void update_uniforms(VulkanContext *context, Platform *platform) {
	uint64_t current_time = platform_time_ms(platform);
	double time = (double)(current_time - state.start_time) / 1000.;

	vec3 axis = { 1.0f, 1.0f, 0.0f };

	MVPObject mvp = { 0 };
	glm_mat4_identity(mvp.model);
	glm_rotate(mvp.model, time, axis);

	vec3 eye = { 0.0f, 0.0f, 3.0f }, center = { 0.0f, 0.0f, 0.0f }, up = { 0.0f, 1.0f, 0.0f };
	glm_mat4_identity(mvp.view);
	glm_lookat(eye, center, up, mvp.view);

	glm_mat4_identity(mvp.projection);
	glm_perspective(glm_rad(45.f), (float)context->swapchain.extent.width / (float)context->swapchain.extent.height, 0.1f, 1000.f, mvp.projection);
	mvp.projection[1][1] *= -1;

	mempcpy(context->uniform_buffers_mapped[context->current_frame], &mvp, sizeof(MVPObject));
}

void resize_callback(Platform *platform, uint32_t width, uint32_t height) {
	state.resized = true;
}
