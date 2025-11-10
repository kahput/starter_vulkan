#include "core/arena.h"
#include "core/logger.h"

#include "platform.h"
#include "renderer/vk_renderer.h"

#include "common.h"
#include <string.h>

void draw_frame(struct arena *arena, VulkanState *vk_state, struct platform *platform);
void resize_callback(struct platform *platform, uint32_t width, uint32_t height);
void update_uniforms(VulkanState *vk_state, Platform *platform);

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
	logger_set_level(LOG_LEVEL_INFO);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VulkanState vk_state = { 0 };
	Platform *platform = platform_startup(state.permanent, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	state.start_time = platform_time_ms(platform);

	LOG_INFO("Logical pixel dimensions { %d, %d }", platform->logical_width, platform->logical_height);
	LOG_INFO("Physical pixel dimensions { %d, %d }", platform->physical_width, platform->physical_height);
	platform_set_physical_dimensions_callback(platform, resize_callback);

	vk_create_instance(&vk_state, platform);
	vk_create_surface(platform, &vk_state);

	vk_create_device(&vk_state);

	vk_create_swapchain(&vk_state, platform);
	vk_create_render_pass(&vk_state);
	vk_create_depth_resources(&vk_state);
	vk_create_framebuffers(&vk_state);

	vk_create_descriptor_set_layout(&vk_state);
	vk_create_graphics_pipline(&vk_state);

	vk_create_command_pool(&vk_state);

	vk_create_texture_image(&vk_state);
	vk_create_texture_image_view(&vk_state);
	vk_create_texture_sampler(&vk_state);

	vk_create_vertex_buffer(&vk_state);
	// vk_create_index_buffer(vk_arena, &vk_state);

	vk_create_uniform_buffers(&vk_state);
	vk_create_descriptor_pool(&vk_state);
	vk_create_descriptor_set(&vk_state);

	vk_create_command_buffer(&vk_state);
	vk_create_sync_objects(&vk_state);

	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
		draw_frame(state.frame, &vk_state, platform);
	}

	vkDeviceWaitIdle(vk_state.device.logical);

	return 0;
}

void draw_frame(struct arena *arena, VulkanState *vk_state, struct platform *platform) {
	vkWaitForFences(vk_state->device.logical, 1, &vk_state->in_flight_fences[vk_state->current_frame], VK_TRUE, UINT64_MAX);

	uint32_t image_index = 0;
	VkResult result = vkAcquireNextImageKHR(vk_state->device.logical, vk_state->swapchain.handle, UINT64_MAX, vk_state->image_available_semaphores[vk_state->current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		vk_recreate_swapchain(vk_state, platform);
		LOG_INFO("Recreating Swapchain");
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		LOG_ERROR("Failed to acquire swapchain image!");
	}

	vkResetFences(vk_state->device.logical, 1, &vk_state->in_flight_fences[vk_state->current_frame]);

	vkResetCommandBuffer(vk_state->command_buffers[vk_state->current_frame], 0);
	vk_record_command_buffers(vk_state, image_index);

	update_uniforms(vk_state, platform);

	VkSemaphore wait_semaphores[] = { vk_state->image_available_semaphores[vk_state->current_frame] };
	VkSemaphore signal_semaphores[] = { vk_state->render_finished_semaphores[image_index] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = array_count(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &vk_state->command_buffers[vk_state->current_frame],
		.signalSemaphoreCount = array_count(signal_semaphores),
		.pSignalSemaphores = signal_semaphores
	};

	if (vkQueueSubmit(vk_state->device.graphics_queue, 1, &submit_info, vk_state->in_flight_fences[vk_state->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to submit draw command buffer");
		return;
	}

	VkSwapchainKHR swapchains[] = { vk_state->swapchain.handle };
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signal_semaphores,
		.swapchainCount = 1,
		.pSwapchains = swapchains,
		.pImageIndices = &image_index,
	};

	vk_state->current_frame = (vk_state->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	result = vkQueuePresentKHR(vk_state->device.present_queue, &present_info);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || state.resized) {
		vk_recreate_swapchain(vk_state, platform);
		LOG_INFO("Recreating Swapchain");
		state.resized = false;
		return;
	} else if (result != VK_SUCCESS) {
		LOG_ERROR("Failed to acquire swapchain image!");
		return;
	}
}

void update_uniforms(VulkanState *vk_state, Platform *platform) {
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
	glm_perspective(glm_rad(45.f), (float)vk_state->swapchain.extent.width / (float)vk_state->swapchain.extent.height, 0.1f, 1000.f, mvp.projection);
	mvp.projection[1][1] *= -1;

	mempcpy(vk_state->uniform_buffers_mapped[vk_state->current_frame], &mvp, sizeof(MVPObject));
}

void resize_callback(struct platform *platform, uint32_t width, uint32_t height) {
	state.resized = true;
}
