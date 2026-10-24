#include "game.h"
#include "core/cmath.h"

// image imfilter(image, kernel, boundary_condition);
// rgb2gray(im
// normxcorr2(onion, peppers);
// image imedge(image, algorithm);

bool tick(Arena *permanent, Arena *frame) {
	if (permanent->offset == 0) arena_push_count(permanent, State, 1); // reserve space for state

	state = (State *)permanent->base;
	input_set_context(&state->input);

	if (state->initialized == false) {
		state->permanent = permanent, state->frame = frame;

		if (gfx_device_make(state->device) == false) return false;

		state->surface = os_surface_open(1280, 720, str8z(named("game")), OS_SURFACE_FLAG_RESIZEABLE);
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

		state->initialized = true;
		state->start_time = os_time_ns();
	}
	Camera *camera = &state->camera;
	GameState *gstate = &state->game;

	double time = (os_time_ns() - state->start_time) * 1e-9;
	float dt = time - state->last_frame;
	state->last_frame = time;

	input_update();

	uint2 resize = { 0 };
	for (OS_Event event; os_event_poll(&event);) {
		switch (event.type) {
			case OS_EVENT_TYPE_SURFACE_CLOSE:
				return false;
				break;
			case OS_EVENT_TYPE_SURFACE_RESIZE:
				resize.x = event.as.resize.width;
				resize.y = event.as.resize.height;
				break;

			case OS_EVENT_TYPE_KEY_PRESS:
			case OS_EVENT_TYPE_KEY_RELEASE:
				input_feed_key(event.as.key.key_code, event.type == OS_EVENT_TYPE_KEY_PRESS);
				break;

			case OS_EVENT_TYPE_MOUSE_MOVE:
				input_feed_mouse_motion(event.as.mouse_move.x, event.as.mouse_move.y);
				break;

			case OS_EVENT_TYPE_MOUSE_PRESS:
			case OS_EVENT_TYPE_MOUSE_RELEASE:
				if (event.type == OS_EVENT_TYPE_MOUSE_RELEASE) {
					uint32_t x = 0;
					(void)x;
				}
				input_feed_mouse_button(event.as.mouse_button.button, event.type == OS_EVENT_TYPE_MOUSE_PRESS);
				break;

			default:
				break;
		}
	}

	uint2 dims = os_surface_size(state->surface);
	Rectangle viewport = { .width = dims.x, .height = dims.y };

	float2 mouse_delta = as2(input_mouse_delta(), float2);
	float2 mouse_position = as2(input_mouse_position(), float2);
	mouse_delta.x /= dims.x;
	mouse_delta.y /= dims.y;

	DRAW_List *draw = drawlist_make(frame, RES_SHADER_QUAD2D, RES_SHADER_LINE3D);
	if (input_key_pressed(KEY_CODE_TAB)) gstate->viewport_state_editor = !gstate->viewport_state_editor;

	if (gstate->viewport_state_editor) {
		os_cursor_capture(state->surface, false);
		scene_camera_orbit(camera, mouse_delta);
	} else {
		os_cursor_capture(state->surface, true);

		static float azimuth = PIf * 3 / 2.f;
		static float theta = PIf / 3.f;
		static const float sensitivity = 1.0f;
		static const float spring_arm_length = 10.f;

		float yaw_delta = mouse_delta.x * sensitivity;
		float pitch_delta = mouse_delta.y * sensitivity;

		azimuth = fmodf(azimuth + yaw_delta, PIf * 2.f);
		if (azimuth < 0)
			azimuth += PIf * 2.f;

		theta = clampf(theta - pitch_delta, PIf / 4.f, PIf / 2.f);

		float3 *p = &gstate->player_position;

		float3 camera_offset = sub3(camera->position, gstate->player_position);

		float r = len3(camera_offset);
		if (r < EPSILON)
			r = EPSILON;

		float current_theta = acosf(camera_offset.y / r);
		float current_azimuth = atan2f(camera_offset.z, camera_offset.x); // [-pi, pi]

		if (current_azimuth < 0)
			current_azimuth += PIf * 2.f;

		float da = azimuth - current_azimuth;
		if (da > PIf)
			da -= PIf * 2.f;
		if (da < -PIf)
			da += PIf * 2.f;

		float t = 1.0f - expf(-15.0f * dt);

		current_azimuth += t * da;
		current_theta += t * (theta - current_theta);

		camera->position = (float3){
			(spring_arm_length * sinf(current_theta) * cosf(current_azimuth)) + p->x,
			spring_arm_length * cosf(current_theta),
			(spring_arm_length * sinf(current_theta) * sinf(current_azimuth)) + p->z,
		};

		camera->target.x = p->x;
		camera->target.z = p->z;

		camera->position.y += p->y;
		camera->target.y = p->y + 1.5f;
	}

	float3 camera_forward = normalize3(sub3(camera->target, camera->position));
	float3 camera_right = normalize3(cross3(camera_forward, FLOAT3_UP));
	float3 camera_up = cross3(camera_right, camera_forward);

	float3x3 camera_rotation = columns3x3(camera_right, camera_up, neg3(camera_forward));
	float4x4 world_from_view = affine4x4(
		camera_rotation,
		camera->position //
	);

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

	float2 input = {
		input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A),
		input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S),
	};

	float input_length_sq = lensq2(input);
	if (input_length_sq > EPSILON * EPSILON) {
		input = normalize2(input);

		float3 camera_position = camera->position;
		float3 camera_target = camera->target;

		camera_position.y = 0.0f;
		camera_target.y = 0.0f;

		float3 fwd = normalize3(sub3(camera_target, camera_position));
		float3 right = normalize3(cross3(fwd, camera->up));

		float3 move = add3(scale3(right, input.x * dt * 4.0f), scale3(fwd, input.y * dt * 4.0f));
		gstate->player_position = add3(gstate->player_position, move);

		// :emitter
		for (uint32_t particle_spawn_index = 0; particle_spawn_index < 1; ++particle_spawn_index) {
			Particle3 *particle = gstate->particles + gstate->particle_next;
			gstate->particle_next = (gstate->particle_next + 1) % countof(gstate->particles);
			particle->t = 0.0f;

			float random_angle = randf_range(0.0f, TAU);
			float random_radius = randf_range(0.0f, 0.3f);

			float3 spawn = { sinf(random_angle) * random_radius, 0.1f, cosf(random_angle) * random_radius };
			particle->position = add3(gstate->player_position, spawn);

			float3 particle_direction = neg3(normalize3(move));
			float3 right, up;
			particle_direction = orthobasis3(particle_direction, &right, &up);

			// clang-format off
			particle->velocity =
				add3(scale3(particle_direction, randf_range(0.5f, 1.0f)),
					add3(
						scale3(right, randf_range(-1.0f, 1.0f)),
						scale3(up, randf_range(-0.5f, 0.0f) 
                    ) //
                ));
			// clang-format on
		}
	}

	Mesh sphere = mesh_sphere(frame, float3(0.0f), 0.5f, 32, 16);
	Mesh capsule = mesh_capsule(frame, float3(0.0f, 1.0f, 0.0f), 2.0f, 0.5f, 32, 16);

	enum {
		MESH_SPHERE,
		MESH_CAPSULE,
		MESH_GDBOT
	};

	static Mesh gdbot = { 0 };
	if (gdbot.total_vertex_count == 0)
		gdbot = res_load_gltf(permanent, s("resources/models/gdbot.glb"));

	Mesh meshes[] = {
		[MESH_SPHERE] = sphere,
		[MESH_CAPSULE] = capsule,
		[MESH_GDBOT] = gdbot,
	};

	typedef uint32_t RID;
	typedef struct {
		RID mesh;
		Transform3 transform;
	} MeshInstance;

	MeshInstance instances[512];
	uint32_t instance_count = 0;

	float3 camera_offset = sub3(camera->position, gstate->player_position);
	float current_azimuth = atan2f(-camera_offset.z, camera_offset.x) - PI * 0.5f; // [-pi, pi]
	instances[instance_count++] = (MeshInstance){ MESH_GDBOT, { gstate->player_position, axisangle4q(FLOAT3_UP, current_azimuth), float3(1.0f) } };

	// :particle
	for (uint32_t particle_index = 0; particle_index < countof(gstate->particles); ++particle_index) {
		Particle3 *particle = gstate->particles + particle_index;
		particle->t += dt;
		if (particle->t > 0.7f) {
			int32_t last = gstate->particle_next - 1;
			if (last < 0) last += countof(gstate->particles);
			gstate->particle_next = last;

			*particle = gstate->particles[last];
			continue;
		}
		particle->position = sadd3(particle->position, particle->velocity, dt);
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			{ particle->position, identity4q(), scale3(float3(0.3f), 0.7f - particle->t) },
		};
	}

	RES_Font *font = res_font(&state->cache, RES_FONT_IBM_PLEX_MONO, 16);
	static uint32_t fps = 0, fps_sum, frame_counter = 0;
	frame_counter += 1, fps_sum += (int32_t)roundf(1.0f / dt);
	if (frame_counter > 30) {
		fps = fps_sum / frame_counter;
		frame_counter = 0, fps_sum = 0;
	}

	draw2d_textf(font, float2(12.0f), WHITE, "FPS: %u", fps);

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
			gfx_cmd_bind(device, 0, set0, countof(set0));
			gfx_cmd_bind(device, 1, set1, countof(set1));
			gfx_cmd_draw_instanced(cmd, 0, 6, 0, draw->line3d.instances.offset / sizeof(DRAW_LineInstance3D));
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
				gfx_cmd_bind(device, 1, set1, countof(set0));

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

				DRAW_QuadInstance3D *base =
					(DRAW_QuadInstance3D *)((uint8_t *)qch->instances.base + batch->instance_offset);

				for (uint32_t qi = 0; qi < batch->instance_count; ++qi) {
					DRAW_QuadInstance3D *quad = base + qi;
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
					storage_data(1, base, batch->instance_count * sizeof(DRAW_QuadInstance3D)),
				};
				Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), state->sampler_nearest) };
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
