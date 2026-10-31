#include "game.h"
#include "core/cmath.h"
#include "core/geom_types.h"
#include "core/input_types.h"
#include "draw.h"
#include "res.h"
#include "utils/anim.h"
#include "utils/input.h"
#include "wm.h"

// image imfilter(image, kernel, boundary_condition);
// rgb2gray(im
// normxcorr2(onion, peppers);
// image imedge(image, algorithm);

typedef enum {
	MAPPING_RIGHT,
	MAPPING_LEFT,
	MAPPING_UP,
	MAPPING_DOWN,
	MAPPING_JUMP,
} GameMapping;

float2 measure_text(RES_Font *font, string8 text) {
	float2 result = { 0 };

	bool ok = font != 0 && text.length;
	if (ok) {
		result.y = font->ascent + font->descent + font->line_gap;

		float xcursor = { 0 };
		for (uint32_t index = 0; index < text.length;) {
			UTF8Result utf8 = utf8_decode(text, index);
			index += utf8.length;

			if (utf8.codepoint == '\n') {
				result.x = maxf(result.x, xcursor);
				result.y += font->ascent + font->descent + font->line_gap;

				xcursor = 0.0f;
				continue;
			}
			if (utf8.codepoint < font->first_codepoint || utf8.codepoint - font->first_codepoint >= font->glyph_count) utf8.codepoint = '?';

			uint32_t cp = utf8.codepoint;
			RES_Glyph *glyph = &font->glyphs[cp - font->first_codepoint];
			xcursor += glyph->advance;
		}

		result.x = maxf(result.x, xcursor);
	}

	return result;
}

bool tick(Arena *permanent, Arena *frame) {
	if (permanent->offset == 0) arena_push_count(permanent, State, 1); // reserve space for state

	state = (State *)permanent->base;
	input_set_context(&state->input);

	if (state->initialized == false) {
		state->permanent = permanent, state->frame = frame;

		if (gfx_device_make(state->device) == false) return false;

		state->surface = wm_surface_open(1280, 720, str8z(named("game")), WM_SURFACE_FLAG_RESIZEABLE);
		state->swapchain = gfx_swapchain_make(state->device, state->surface, named("main"));
		state->depthbuffer = gfx_image_make(state->device, 1280, 720,
			(ImageOptions){
			  .debug_name = named("target:main_depth"),
			  .format = PIXEL_FORMAT_DEPTH,
			  .usage = IMAGE_USAGE_RENDER,
			});

		state->white_texture = gfx_image_make(state->device, 1, 1, (ImageOptions){ .pixels = (uint8_t[]){ 255, 255, 255, 255 } });
		state->sampler_nearest = gfx_sampler_make(state->device, sampler_opt(named("sampler:nearest"), FILTER_NEAREST, WRAP_MODE_CLAMP));
		state->sampler_linear = gfx_sampler_make(state->device, sampler_opt(named("sampler:linear"), FILTER_LINEAR, WRAP_MODE_CLAMP));

		Arena *resource_arena = arena_push_count(permanent, Arena, 1);
		resource_arena->base = arena_push(state->permanent, MiB(8), alignof(long double), true);
		resource_arena->capacity = MiB(8);

		state->cache = res_cache_make(resource_arena, state->device, &registry);

		state->camera = (Camera){
			.projection = CAMERA_PROJECTION_PERSPECTIVE,
			.position = { 0.0f, 3.0f, 8.f },
			.target = { 0.0f, 1.5f, 0.0f },
			.up = FLOAT3_UP,
			.fovy = 45.f,
			.near = 0.1f,
			.far = 500.0f,
		};

		state->last_frame = 0.0f;

		state->game.player = (Player){
			.transform = transform3(float3(0.0f), identity4q(), float3(1.0f)),
		};
		state->game.viewport_state_editor = true;

		state->initialized = true;
		state->start_time = os_time_ns();
	}
	Camera *camera = &state->camera;
	GameState *gstate = &state->game;

	double time = (os_time_ns() - state->start_time) * 1e-9;
	float dt = time - state->last_frame;
	state->last_frame = time;

	input_begin_frame();
	uint2 resize = { 0 };
	for (WM_Event event; wm_poll(&event);) {
		switch (event.type) {
			case WM_EVENT_TYPE_SURFACE_CLOSE:
				return false;
				break;
			case WM_EVENT_TYPE_SURFACE_RESIZE:
				resize.x = event.as.resize.width;
				resize.y = event.as.resize.height;
				break;

			case WM_EVENT_TYPE_KEY_PRESS:
			case WM_EVENT_TYPE_KEY_RELEASE: {
				state->input.keys[event.as.key.key_code].curr = event.type == WM_EVENT_TYPE_KEY_PRESS;
			} break;

			case WM_EVENT_TYPE_MOUSE_MOTION: {
				state->input.mouse.x = event.as.mouse_motion.x;
				state->input.mouse.y = event.as.mouse_motion.y;
				state->input.mouse.dx += event.as.mouse_motion.dx;
				state->input.mouse.dy += event.as.mouse_motion.dy;
			} break;

			case WM_EVENT_TYPE_MOUSE_PRESS:
			case WM_EVENT_TYPE_MOUSE_RELEASE: {
				state->input.mouse.buttons[event.as.mouse_button.button].curr = event.type == WM_EVENT_TYPE_MOUSE_PRESS;
			} break;

			default:
				break;
		}
	}

	static GP_Slot gp_now[GP_SLOT_MAX], gp_was[GP_SLOT_MAX];
	memory_copy(gp_was, gp_now, sizeof(gp_now));
	gp_poll(gp_now);

	uint2 dims = wm_surface_size(state->surface);
	Rectangle viewport = { .width = dims.x, .height = dims.y };

	float2 mouse_delta = input_mouse_delta();
	float2 mouse_position = input_mouse_position();
	mouse_delta.x /= dims.x;
	mouse_delta.y /= dims.y;

	DRAW_List *draw = drawlist_make(frame, RES_SHADER_QUAD2D, RES_SHADER_LINE3D);
	if (input_key_pressed(KEY_CODE_TAB)) gstate->viewport_state_editor = !gstate->viewport_state_editor;

	if (gstate->viewport_state_editor) {
		wm_cursor_capture(state->surface, false);
		scene_camera_orbit(camera, mouse_delta);
	} else {
		Player *player = &gstate->player;
		Transform3 *transform = &gstate->player.transform;
		wm_cursor_capture(state->surface, true);

		// :settings
		float sensitivity = 0.5f, spring_arm_length = 8.f;
		float accel_speed = 48.0f, max_walk_speed = 3.0f, max_run_speed = 6.0f;
		float xz_apex = 2.0f, t_down = 0.3f, h_apex = 1.5f;

		// derived settings
		float t_up = xz_apex / max_run_speed;
		float jump_speed = 2.0f * h_apex / t_up;
		float g_up = -2.0f * h_apex / (t_up * t_up);
		float g_down = -2.0f * h_apex / (t_down * t_down);

		// :input
		float2 lstick = { gp_now[0].axes[GAMEPAD_AXIS_LX] / 32060.0f, gp_now[0].axes[GAMEPAD_AXIS_LY] / 32060.0f };
		float2 rstick = { gp_now[0].axes[GAMEPAD_AXIS_RX] / 32060.0f, gp_now[0].axes[GAMEPAD_AXIS_RY] / 32060.0f };

		lstick.x = fabsf(lstick.x) <= 0.2f ? 0.0f : (lstick.x - signf(lstick.x) * 0.2f) / 0.8f;
		lstick.y = fabsf(lstick.y) <= 0.2f ? 0.0f : (lstick.y - signf(lstick.y) * 0.2f) / 0.8f;

		float2 look_input = scale2(mouse_delta, sensitivity);
		/* float2 move_input = { */
		/* 	input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A), */
		/* 	input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S), */
		/* }; */
		float2 move_input = lstick;
		bool jump_requested = (gp_now[0].buttons & (1 << GAMEPAD_BUTTON_SOUTH)) && (gp_was[0].buttons & (1 << GAMEPAD_BUTTON_SOUTH)) == 0;
		bool jump_held = gp_now[0].buttons & (1 << GAMEPAD_BUTTON_SOUTH);
		/* bool sprint_held = input_key_down(KEY_CODE_LEFTSHIFT); */
		bool moving = lensq2(move_input) > EPSILON * EPSILON;
		if (moving) move_input = limit2(move_input, 1.0f);

		{ // :movement
			float3 desired;
			{
				float3 camera_position = camera->position;
				float3 camera_target = camera->target;

				camera_position.y = 0.0f;
				camera_target.y = 0.0f;

				float3 forward = normalize3(sub3(camera_target, camera_position));
				float3 right = normalize3(cross3(forward, camera->up));
				desired = add3(
					scale3(right, move_input.x),
					scale3(forward, move_input.y) //
				);
			}

			bool was_grounded = transform->translation.y <= EPSILON;

			transform->scale = lerp3(transform->scale, float3(1.0f), 1.0f - expf(-10.0f * dt));
			if (moving) {
				quat4 face = axisangle4q(FLOAT3_UP, atan2f(desired.x, desired.z));
				transform->rotation = slerp4q(transform->rotation, face, 1.0f - expf(-15.0f * dt));
			}

			if (jump_requested && was_grounded) {
				player->velocity.y = jump_speed;
				player->transform.scale = float3(0.5, 1.5, 0.5);
			}

			// velocity
			float3 old_velocity = player->velocity;
			float3 velocity = { old_velocity.x, 0.0f, old_velocity.z };
			velocity = approach3(velocity, scale3(desired, max_run_speed), accel_speed * dt);
			velocity.y = old_velocity.y + (jump_held && old_velocity.y > 0.0f ? g_up : g_down) * dt;
			player->velocity = velocity;

			// TODO: move_and_slide/move_and_collide goes here. E.g,
			// SlideResult3 move_reuslt = move_and_slide(collision_world, player_collider, player->velocity, dt);
			transform->translation = add3(transform->translation, scale3(add3(old_velocity, velocity), 0.5f * dt));
			if (transform->translation.y <= EPSILON) {
				if (was_grounded == false) transform->scale = float3(1.25f, 0.75f, 1.25f);
				transform->translation.y = 0.0f;
				player->velocity.y = 0.0f;
			}

			// :emitter
			static float emit_accumulator = 0.0f;

			bool grounded = transform->translation.y <= EPSILON;
			if (grounded && moving) {
				emit_accumulator += dt * 24.0f;

				while (gstate->particle_count < countof(gstate->particles) && emit_accumulator >= 1.0f) {
					Particle3 *particle = 0;
					for (uint32_t index = 0; index < countof(gstate->particles); ++index) {
						if (gstate->particles[index].t <= 0.0f) {
							particle = &gstate->particles[index];
							gstate->particle_count++;
							break;
						}
					}
					ASSERT(particle);

					particle->lifetime = particle->t = 0.7f;

					float random_angle = randf_range(0.0f, TAU);
					float random_radius = 0.3f * randf_range(0.0f, 1.0f);

					float3 spawn = { sinf(random_angle) * random_radius, 0.1f, cosf(random_angle) * random_radius };
					particle->position = add3(transform->translation, spawn);

					float3 particle_direction = neg3(normalize3(player->velocity));
					float3 right, up;
					particle_direction = orthobasis3(particle_direction, &right, &up);

					particle->velocity = scale3(particle->velocity, randf_range(0.8f, 1.0f));
					particle->velocity = sadd3(particle->velocity, right, randf_range(-0.3f, 0.3f));

					emit_accumulator -= 1.0f;
				}
			}
		}

		{ // :camera
			static float desired_azimuth = PIf * 3 / 2.f;
			static float desired_theta = PIf / 3.f;

			float yaw = look_input.x, pitch = look_input.y;

			desired_azimuth = fmodf(desired_azimuth + yaw, PIf * 2.f);
			if (desired_azimuth < 0)
				desired_azimuth += PIf * 2.f;

			desired_theta = clampf(desired_theta - pitch, PIf / 8.f, PIf - PIf / 4);

			float3 target = player->transform.translation;
			target.y = 2.5f;
			float3 camera_offset = sub3(camera->position, target);

			float r = len3(camera_offset);
			if (r < EPSILON)
				r = EPSILON;

			float current_theta = acosf(camera_offset.y / r);
			float current_azimuth = atan2f(camera_offset.z, camera_offset.x); // [-pi, pi]

			if (current_azimuth < 0)
				current_azimuth += PIf * 2.f;

			float da = desired_azimuth - current_azimuth;
			if (da > PIf)
				da -= PIf * 2.f;
			if (da < -PIf)
				da += PIf * 2.f;

			float t = 1.0f - expf(-15.0f * dt);

			current_azimuth += t * da;
			current_theta += t * (desired_theta - current_theta);

			camera->target = target;
			float3 spherical = {
				(spring_arm_length * sinf(current_theta) * cosf(current_azimuth)),
				spring_arm_length * cosf(current_theta),
				(spring_arm_length * sinf(current_theta) * sinf(current_azimuth)),
			};

			camera->position = add3(spherical, camera->target);
		}
	}

	float3 camera_forward = normalize3(sub3(camera->target, camera->position));
	float3 camera_right = normalize3(cross3(camera_forward, FLOAT3_UP));
	float3 camera_up = cross3(camera_right, camera_forward);

	float3x3 camera_rotation = columns3x3(camera_right, camera_up, neg3(camera_forward));
	float4x4 world_from_view = affine4x4(camera_rotation, camera->position);

	float4x4 view_from_world = affine4x4(
		transpose3x3(camera_rotation),
		float3(-dot3(camera_right, camera->position), -dot3(camera_up, camera->position), dot3(camera_forward, camera->position)) //
	);

	float4x4 clip_from_view = camera_proj(&state->camera, viewport.width / viewport.height);
	float4x4 clip_from_world = mul4x4(clip_from_view, view_from_world);

	int32_t seg_count = 32;
	for (int32_t z = -seg_count; z <= seg_count; ++z)
		draw3d_line(float3(-seg_count, 0.0f, z), float3(seg_count, 0.0f, z), 1.0f, z == 0 ? RED : GRAY);
	for (int32_t x = -seg_count; x <= seg_count; ++x)
		draw3d_line(float3(x, 0.0f, -seg_count), float3(x, 0.0f, seg_count), 1.0f, x == 0 ? GREEN : GRAY);

	uint32_t segments = 32, rings = 16;
	float radius = 0.5f, height = 2.0f;

	Mesh sphere = mesh_sphere(frame, float3(0.0f), 0.5f, 32, 16);
	Mesh capsule = mesh_capsule(frame, float3(0.0f, 1.0f, 0.0f), 2.0f, 0.5f, 32, 16);

	enum {
		MESH_SPHERE,
		MESH_CAPSULE,
		MESH_GDBOT,
		MESH_CLOUD,
	};

	static Mesh gdbot = { 0 }, cloud;
	static Skeleton gdbot_skeleton = {
		0,
	};
	static AnimationClip gdbot_animations = { 0 };
	if (gdbot.total_vertex_count == 0) gdbot = res_load_gltf(permanent, s("resources/models/gdbot.glb"));
	if (cloud.total_vertex_count == 0) cloud = res_load_gltf(permanent, s("resources/models/cloud.glb"));

	Mesh meshes[] = {
		[MESH_SPHERE] = sphere,
		[MESH_CAPSULE] = capsule,
		[MESH_GDBOT] = gdbot,
		[MESH_CLOUD] = cloud,
	};

	typedef struct {
		RID mesh;
		Transform3 transform;
	} MeshInstance;

	MeshInstance instances[512];
	uint32_t instance_count = 0;

	instances[instance_count++] = (MeshInstance){ MESH_CAPSULE, gstate->player.transform };

	// :particle
	for (uint32_t particle_index = 0; particle_index < countof(gstate->particles); ++particle_index) {
		Particle3 *particle = gstate->particles + particle_index;
		if (particle->t <= 0.0f) continue;

		particle->t -= dt;
		if (particle->t <= 0.0f) {
			*particle = (Particle3){ 0 };
			gstate->particle_count--;
			continue;
		}

		particle->position = sadd3(particle->position, particle->velocity, dt);

		float age = clampf(1.0f - particle->t / particle->lifetime, 0.0f, 1.0f);

		float fade_in = smoothstepf(0.0f, 0.12f, age);
		float fade_out = 1.0f - smoothstepf(0.45f, 1.0f, age);
		float scale = fade_in * fade_out;

		instances[instance_count++] =
			(MeshInstance){
				MESH_CLOUD,
				transform3(particle->position, identity4q(), float3(scale))
			};
	}

	GFX_Device *device = state->device;
	GFX_Swapchain *swapchain = state->swapchain;

	if (resize.x && resize.y) {
		gfx_swapchain_resize(device, swapchain, resize.x, resize.y);
		gfx_image_resize(device, state->depthbuffer, resize.x, resize.y);
	}
	res_cache_tick(frame, &state->cache);

	GFX_CommandEncoder *cmd = gfx_frame_begin(state->device);
	if (cmd == 0) return false;

	GFX_Image *backbuffer = gfx_swapchain_backbuffer(device, cmd, swapchain);
	if (backbuffer) {
		gfx_cmd_draw_begin(cmd,
			(GFX_DrawPassInfo){
			  .debug_name = "main",
			  .colors[0] = { .target = backbuffer, .load = LOAD_OP_CLEAR, .store = STORE_OP_STORE, .clear = hex(0x101010) },
			  .depth = { .target = state->depthbuffer, .clear = 1.0f, .load = LOAD_OP_CLEAR, .store = STORE_OP_STORE },
			});

		typedef struct {
			float4x4 view;
			float4x4 proj;
			float4 camera_position;
			float2 viewport;
			float fog_density;
			float ambient_strength;
			float fog_gradient;
			float time;
		} FrameData;

		FrameData fd = {
			.viewport = { dims.x, dims.y },
			.time = time,
			.view = view_from_world,
			.proj = clip_from_view,
			.camera_position = make4_from3(state->camera.position, 0.0f),
		};

		if (draw->line3d.instances.offset) {
			RES_Shader *line3d = res_shader(&state->cache, RES_SHADER_LINE3D);
			ASSERT(line3d);
			gfx_cmd_shader_bind(device, line3d->handle, 0);

			Uniform set1[] = {
				storage_data(0, draw->line3d.instances.base, draw->line3d.instances.offset),
			};
			Uniform set0[] = { uniform_data(0, &fd, sizeof(fd)) };
			gfx_cmd_bind(device, 0, set0, countof(set0));
			gfx_cmd_bind(device, 1, set1, countof(set1));
			gfx_cmd_draw_instanced(cmd, 0, 6, 0, draw->line3d.instances.offset / sizeof(DRAW_LineInstance));
		}

		{
			RES_Shader *unlit = res_shader(&state->cache, RES_SHADER_UNLIT);
			gfx_cmd_shader_bind(device, unlit->handle, 0);

			VertexLayout shader_vlayout = vertex_layout(
				{ VERTEX_SEMANTIC_POSITION, DATA_FORMAT_FLOAT32x4 },
				{ VERTEX_SEMANTIC_NORMAL, DATA_FORMAT_FLOAT32x4 },
				{ VERTEX_SEMANTIC_UV0, DATA_FORMAT_FLOAT32x2 },
				{ VERTEX_SEMANTIC_UV1, DATA_FORMAT_FLOAT32x2 } //
			);

			Uniform set0[] = { uniform_data(0, &fd, sizeof(fd)) };
			gfx_cmd_bind(device, 0, set0, countof(set0));

			uint64_t index_buffer_offsets[countof(meshes)];
			uint64_t vertex_buffer_offsets[countof(meshes)];
			uint64_t vertex_buffer_sizes[countof(meshes)];

			for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
				Mesh *mesh = meshes + mesh_index;

				uint64_t vertex_buffer_size = mesh->total_vertex_count * shader_vlayout.stride;
				uint64_t vertex_buffer_offset = alignup(cmd->transient_arena->offset, 256);
				void *vertices = arena_push(cmd->transient_arena, alignup(vertex_buffer_size, 256), 256, 0);
				mesh_pack_into(mesh, &shader_vlayout, vertices, vertex_buffer_size);

				index_buffer_offsets[mesh_index] = gfx_cmd_put(cmd, mesh->total_index_count * index_format_to_size(mesh->index_format), mesh->indices);
				vertex_buffer_offsets[mesh_index] = vertex_buffer_offset;
				vertex_buffer_sizes[mesh_index] = vertex_buffer_size;
			}

			for (uint32_t instance_index = 0; instance_index < instance_count; ++instance_index) {
				MeshInstance *instance = &instances[instance_index];
				RID mesh_index = instance->mesh;
				if (mesh_index >= countof(meshes)) continue;

				Mesh mesh = meshes[mesh_index];

				Uniform set1[] = { storage_buffers(0, cmd->transient_buffer, vertex_buffer_offsets[mesh_index], vertex_buffer_sizes[mesh_index]) };
				gfx_cmd_bind(device, 1, set1, countof(set1));

				float4x4 transform = transform4x4(instance->transform);
				gfx_cmd_push_constant(cmd, sizeof(transform), transform.elements);

				if (mesh.index_format == INDEX_FORMAT32)
					gfx_cmd_bind_index_buffer32(cmd, cmd->transient_buffer, index_buffer_offsets[mesh_index]);
				else
					gfx_cmd_bind_index_buffer16(cmd, cmd->transient_buffer, index_buffer_offsets[mesh_index]);

				for (uint32_t part_index = 0; part_index < mesh_part_count(mesh); ++part_index) {
					MeshPart part = meshpart(mesh, part_index);

					if (part.index_count) {
						gfx_cmd_draw_indexed(cmd, part.index_offset, part.index_count, part.vertex_offset);
					} else {
						gfx_cmd_draw(cmd, part.vertex_offset, part.vertex_count);
					}
				}
			}
		}

		{
			DRAW_Channel *qch = &draw->quad2d;
			FrameData fd = {
				.view = identity4x4(),
				.proj = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
				.viewport = as2(dims, float2),
				.time = time,
			};

			for (DRAW_Batch *batch = qch->first_batch; batch; batch = batch->next) {
				RES_Shader *shader = res_shader(&state->cache, batch->shader);
				gfx_cmd_shader_bind(device, shader->handle, 0);

				GFX_Image *images[32] = { 0 };
				uint32_t image_count = 1;
				for (uint32_t i = 0; i < 32; ++i)
					images[i] = state->white_texture;

				DRAW_QuadInstance *base =
					(DRAW_QuadInstance *)((uint8_t *)qch->instances.base + batch->instance_offset);

				for (uint32_t qi = 0; qi < batch->instance_count; ++qi) {
					DRAW_QuadInstance *quad = base + qi;
					if (quad->imageid && quad->imageid != indexof(device->image_pool, state->white_texture)) {
						int32_t found_index = -1;
						for (uint32_t ii = 1; ii < image_count; ++ii)
							if (indexof(device->image_pool, images[ii]) == quad->imageid) {
								found_index = ii;
								break;
							}
						if (found_index == -1) {
							ASSERT(image_count < countof(images));
							found_index = image_count++;
							images[found_index] = &device->image_pool[quad->imageid];
							gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, device->image_pool + quad->imageid);
						}
						quad->imageid = found_index;
					}
				}

				Uniform uniforms0[] = {
					uniform_data(0, &fd, sizeof(fd)),
					storage_data(1, base, batch->instance_count * sizeof(DRAW_QuadInstance)),
				};
				Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), state->sampler_linear) };
				gfx_cmd_bind(device, 0, uniforms0, countof(uniforms0));
				gfx_cmd_bind(device, 1, uniforms1, countof(uniforms1));

				gfx_cmd_draw_instanced(cmd, 0, 6, 0, batch->instance_count);
			}
		}

		gfx_cmd_draw_end(cmd);
	}

	gfx_frame_end(device, cmd);

	return true;
}
