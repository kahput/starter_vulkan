#include "game.h"
#include "common.h"
#include "core/cmath.h"
#include "core/geom.h"
#include "core/geom_types.h"
#include "core/input_types.h"
#include "core/rid.h"
#include "draw.h"
#include "generated/res_generated.h"
#include "gpu.h"
#include "gpu/gpu_types.h"
#include "res.h"
#include "utils/anim.h"
#include "utils/input.h"
#include "wm.h"

// image imfilter(image, kernel, boundary_condition);
// rgb2gray(im
// normxcorr2(onion, peppers);
// image imedge(image, algorithm);

typedef enum {
	ACTION_RIGHT,
	ACTION_LEFT,
	ACTION_UP,
	ACTION_DOWN,

	ACTION_CAMERA_RIGHT,
	ACTION_CAMERA_LEFT,
	ACTION_CAMERA_UP,
	ACTION_CAMERA_DOWN,

	ACTION_JUMP,

	ACTION_MAX,
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
bool tick(GPU_Device *device, Arena *permanent, Arena *frame) {
	if (permanent->offset == 0) arena_push_count(permanent, State, 1); // reserve space for state

	state = (State *)permanent->base;

	if (state->initialized == false) {
		state->permanent = permanent, state->frame = frame;

		state->surface = wm_surface_open(1280, 720, str8z(named("game")), WM_SURFACE_FLAG_RESIZEABLE);
		state->swapchain = gpu_swapchain_make(device, state->surface, named("main"));
		state->depthbuffer = gpu_image_make(device, 1280, 720,
			(ImageOptions){
			  .debug_name = named("target:main_depth"),
			  .format = PIXEL_FORMAT_DEPTH,
			  .usage = IMAGE_USAGE_RENDER,
			});

		state->white_texture = gpu_image_make(device, 1, 1, (ImageOptions){ .pixels = (uint8_t[]){ 255, 255, 255, 255 } });
		state->sampler_nearest = gpu_sampler_make(device, sampler_opt(named("sampler:nearest"), FILTER_NEAREST, WRAP_MODE_CLAMP));
		state->sampler_linear = gpu_sampler_make(device, sampler_opt(named("sampler:linear"), FILTER_LINEAR, WRAP_MODE_CLAMP));

		Arena *resource_arena = arena_push_count(permanent, Arena, 1);
		resource_arena->base = arena_push(state->permanent, MiB(8), alignof(long double), true);
		resource_arena->capacity = MiB(8);

		state->cache = res_cache_make(resource_arena, device, &registry);

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

		InputAction action_map[] = {
			[ACTION_LEFT] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_A, 1.0f },
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_LEFT, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_AXIS, GAMEPAD_AXIS_LX, -1.0f },
			  { INPUT_MAPPING_GAMEPAD_BUTTON, GAMEPAD_BUTTON_LEFT, 1.0f },
			},
			[ACTION_RIGHT] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_D, 1.0f },
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_RIGHT, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_AXIS, GAMEPAD_AXIS_LX, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_BUTTON, GAMEPAD_BUTTON_RIGHT, 1.0f },
			},
			[ACTION_UP] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_W, 1.0f },
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_UP, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_AXIS, GAMEPAD_AXIS_LY, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_BUTTON, GAMEPAD_BUTTON_UP, 1.0f },
			},
			[ACTION_DOWN] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_S, 1.0f },
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_DOWN, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_AXIS, GAMEPAD_AXIS_LY, -1.0f },
			  { INPUT_MAPPING_GAMEPAD_BUTTON, GAMEPAD_BUTTON_DOWN, 1.0f },
			},
			[ACTION_JUMP] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_SPACE, 1.0f },
			  { INPUT_MAPPING_MOUSE_BUTTON, MOUSE_BUTTON_LEFT, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_BUTTON, GAMEPAD_BUTTON_SOUTH, 1.0f },
			},
			[ACTION_CAMERA_RIGHT] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_RIGHT, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_AXIS, GAMEPAD_AXIS_RX, 1.0f },
			},
			[ACTION_CAMERA_LEFT] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_LEFT, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_AXIS, GAMEPAD_AXIS_RX, -1.0f },
			},
			[ACTION_CAMERA_UP] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_UP, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_AXIS, GAMEPAD_AXIS_RY, 1.0f },
			},
			[ACTION_CAMERA_DOWN] = {
			  { INPUT_MAPPING_KEYBOARD_KEY, KEY_CODE_DOWN, 1.0f },
			  { INPUT_MAPPING_GAMEPAD_AXIS, GAMEPAD_AXIS_RY, -1.0f },
			},
		};

		input_action_map(&state->input, action_map, countof(action_map));

		state->initialized = true;
		state->start_time = os_time_ns();
	}
	Camera *camera = &state->camera;
	GameState *gstate = &state->game;

	double time = (os_time_ns() - state->start_time) * 1e-9;
	float dt = time - state->last_frame;
	state->last_frame = time;

	input_begin_frame(&state->input);
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
				state->input.keys_now[event.as.key.key_code] = event.type == WM_EVENT_TYPE_KEY_PRESS;
			} break;

			case WM_EVENT_TYPE_MOUSE_MOTION: {
				state->input.mouse.x = event.as.mouse_motion.x;
				state->input.mouse.y = event.as.mouse_motion.y;
				state->input.mouse.dx += event.as.mouse_motion.dx;
				state->input.mouse.dy += event.as.mouse_motion.dy;
			} break;

			case WM_EVENT_TYPE_MOUSE_PRESS:
			case WM_EVENT_TYPE_MOUSE_RELEASE: {
				state->input.mouse.buttons_now[event.as.mouse_button.button] = event.type == WM_EVENT_TYPE_MOUSE_PRESS;
			} break;

			default:
				break;
		}
	}
	gp_poll(state->input.pads_now);

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
		float accel_speed = 32.0f, max_walk_speed = 3.0f, max_run_speed = 4.0f;
		float xz_apex = 1.0f, t_down = 0.2f, h_apex = 2.0f;

		float3 circle = { transform->translation.x, 1.5f, transform->translation.z };
		draw3d_arc(circle, float2(1.0f), 32, FLOAT3_UP, 0.0f, TAU, 4.0f, WHITE);

		// derived settings
		float t_up = xz_apex / max_run_speed;
		float jump_speed = 2.0f * h_apex / t_up;
		float g_up = -2.0f * h_apex / (t_up * t_up);
		float g_down = -2.0f * h_apex / (t_down * t_down);

		// :input
		float2 move_input = input_action_vector(ACTION_RIGHT, ACTION_LEFT, ACTION_UP, ACTION_DOWN);
		float2 look_input = input_action_vector(ACTION_CAMERA_RIGHT, ACTION_CAMERA_LEFT, ACTION_CAMERA_DOWN, ACTION_CAMERA_UP);
		look_input = scale2(look_input, dt * sensitivity);

		bool jump_requested = input_action_pressed(ACTION_JUMP);
		bool jump_held = input_action_held(ACTION_JUMP);
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

			desired_theta = clamp(desired_theta - pitch, PIf / 8.f, PIf - PIf / 4);

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

	float4x4 world_from_view;
	float4x4 view_from_world;
	{
		float3 camera_forward = normalize3(sub3(camera->target, camera->position));
		float3 camera_right = normalize3(cross3(camera_forward, FLOAT3_UP));
		float3 camera_up = cross3(camera_right, camera_forward);

		float3x3 camera_rotation = columns3x3(camera_right, camera_up, neg3(camera_forward));
		world_from_view = affine4x4(camera_rotation, camera->position);

		view_from_world = affine4x4(
			transpose3x3(camera_rotation),
			float3(-dot3(camera_right, camera->position), -dot3(camera_up, camera->position), dot3(camera_forward, camera->position)) //
		);
	}

	float aspect = viewport.width / viewport.height;
	float4x4 clip_from_view = perspective(camera->fovy * DEG2RAD, aspect, camera->near, camera->far);
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
	static Skeleton gdbot_skeleton = { 0 };
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
	RES_Font *font = res_font(&state->cache, RES_FONT_IBM_PLEX_MONO, 24);

	draw2d_textf(font, float2(24.0f), WHITE, "Hello world!\n\n%.2f", 42.332f);

	if (1) {
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

			float age = clamp(1.0f - particle->t / particle->lifetime, 0.0f, 1.0f);

			float fade_in = smoothstep(0.0f, 0.12f, age);
			float fade_out = 1.0f - smoothstep(0.45f, 1.0f, age);
			float scale = fade_in * fade_out;

			instances[instance_count++] =
				(MeshInstance){
					MESH_CLOUD,
					transform3(particle->position, identity4q(), float3(scale))
				};
		}
	}
	if (0) { // raycast circle
		float2 origin = rect_center(viewport);

		float2 s = origin;
		float r = 64.0f;

		static Drag2D ro = { 0 };
		drag2d_point(&ro, float2(origin.x - origin.x * 0.5f, origin.y), 4.0f, mouse_position);

		Ray2 ray = { ro.position, normalize2(float2(1.0f, 1.0f)) };

		float t = dot2(sub2(s, ray.origin), ray.direction);
		float2 p = sadd2(ray.origin, ray.direction, t);
		float y = len2(sub2(p, s));

		draw2d_rect(viewport, hex(0x101010));
		draw2d_circle(s, 4.0f, WHITE);
		draw2d_circle_outline(s, r, 4.0f, WHITE);

		draw2d_arrow(ray.origin, scale2(ray.direction, t), 4.0f, 8.0f, WHITE);
		draw2d_circle(ray.origin, 4.0f, WHITE);

		if (y < r) {
			float x = sqrtf(r * r - y * y);
			float t1 = t - x;
			float t2 = t + x;

			draw2d_circle(sadd2(ray.origin, ray.direction, t1), 4.0f, WHITE);
			draw2d_circle(sadd2(ray.origin, ray.direction, t2), 4.0f, WHITE);

			float2 hit_center = sadd2(ray.origin, ray.direction, t);
			draw2d_arrow(ray.origin, scale2(ray.direction, t), 4.0f, 8.0f, WHITE);
		}
	}
	if (0) { // raycast
		Plane near_plane = { FLOAT3_FORWARD, 1.0f };

		float3 q = { 2.0f, 4.0f, -8.0f };
		float3 r = scale3(q, -1.0f / q.z); // == q / dot(q, n);

		draw3d_quad_outline(near_plane, 1.0f, 1.0f, 4.0f, WHITE);
		draw3d_arrow(float3(0.0f), q, 4.0f, WHITE, view_from_world, clip_from_view, viewport.width);
		draw3d_arrow(float3(0.0f), r, 4.0f, WHITE, view_from_world, clip_from_view, viewport.width);

		float2 vp = rect_half_extent(viewport);
		float3 mouse_ndc = float3(div2(sub2(mouse_position, vp), vp), 0.0f);
		float halfh = tanf(radians(camera->fovy) * 0.5f);
		float halfw = aspect * halfh;
		float3 mouse_view = { mouse_ndc.x * halfw, mouse_ndc.y * -halfh, -1.0f }; // before perspective divide

		float3 mouse_world = xform3p(world_from_view, mouse_view);
		Ray3 mouse = {
			camera->position,
			normalize3(sub3(mouse_world, camera->position)),
		};

		// dot(n, q - p) = 0
		// r(t) = o + td
		// dot(n, o + td - p) = 0
		// dot(n, o) + dot(n, td) - dot(n, p) = 0
		// dot(n, td) = dot3(n, p) - dot(n, o)
		// t*dot(n, d) = dot(n, o) - dot(n, p)
		// t = (dot(n, p) - dot(n, o)) / dot(n, d)

		float t = -dot3(FLOAT3_UP, mouse.origin) / dot3(FLOAT3_UP, mouse.direction);
		raycast_plane(mouse, (Plane){ FLOAT3_UP, 0.0f });
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(sadd3(mouse.origin, mouse.direction, t), identity4q(), float3(1.0f))
		};

		float3 inspect = { -1.0f, 3.0f, -2.0f };

		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(inspect, identity4q(), float3(1.0f)),
		};

		draw2d_textf(font, float2(24.0f), WHITE, "Mouse = { %.2f, %.2f }", spread2(mouse_ndc));
		draw2d_textf(font, float2(24.0f, 48.0f), WHITE, "Mouse ray = { %.2f, %.2f, %.2f }", spread3(mouse.direction));
	}
	if (1) { // closest point plane visualization
		Triangle3 tri = { { -1.0f, 2.0f, -3.0f }, { 1.0f, 2.0f, -3.0f }, { 0.0f, 4.0f, -2.0f } };
		draw3d_triangle_outline(tri, 4.0f, WHITE);

		Plane tp = plane_from_triangle(tri);

		static Drag3D q = { { 2.0f, 2.5f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0, 0 };
		float r = 0.1f;
		drag3d_point(&q, r, camera, viewport, mouse_position);

		float3 P = scale3(tp.normal, tp.distance);
		float3 v = sub3(q.position, P);

		float3 qN_component = scale3(tp.normal, dot3(v, tp.normal));
		float3 closest_point_on_plane = sub3(q.position, qN_component);
		draw3d_arrow(q.position, neg3(qN_component), 4.0f, WHITE, view_from_world, clip_from_view, viewport.width);

		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(q.position, identity4q(), q.hovered || q.active ? float3(r / 0.5f * 1.2f) : float3(r / 0.5f))
		};
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(closest_point_on_plane, identity4q(), float3(0.2f)),
		};
	}
	if (0) { // cloest point line visualization
		float3 a = { -1.0f, 2.0f, -2.0f };
		float3 b = { 1.0f, 2.0f, -2.0f };
		static Drag3D p = { { 0.5f, 3.0f, -1.0f }, { 0.0f, 0.0f, 0.0f }, 0, 0 };

		float r = 0.1f;
		drag3d_point(&p, r, camera, viewport, mouse_position);

		float3 ab = sub3(b, a);
		float3 ap = sub3(p.position, a);

		float ab_component = dot3(ab, ap) / dot3(ab, ab);
		float3 offset_from_a = scale3(ab, saturate(ab_component));
		float3 closest_point = add3(a, offset_from_a);

		draw3d_arrow(p.position, sub3(closest_point, p.position), 4.0f, WHITE, view_from_world, clip_from_view, viewport.width);

		draw3d_line(a, b, 4.0f, WHITE);
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(p.position, identity4q(), p.hovered || p.active ? float3((r / 0.5f) * 1.2f) : float3(r / 0.5f)),
		};
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(closest_point, identity4q(), float3(0.2f)),
		};
	}
	if (0) { // cloest point ray visualization
		Ray3 ray = { { 0.0f, 3.0f, -2.0f }, normalize3(float3(1.0f, -0.5f, 1.0f)) };
		static Drag3D p = { { 1.0f, 3.0f, -4.0f }, { 0.0f, 0.0f, 0.0f }, 0, 0 };

		float r = 0.1f;
		float3 closest_point = sadd3(ray.origin, ray.direction, maxf(0.0f, dot3(ray.direction, sub3(p.position, ray.origin))));
		drag3d_point(&p, r, camera, viewport, mouse_position);

		draw3d_arrow(ray.origin, ray.direction, 4.0f, WHITE, view_from_world, clip_from_view, viewport.width);
		draw3d_arrow(p.position, sub3(closest_point, p.position), 4.0f, WHITE, view_from_world, clip_from_view, viewport.width);
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(p.position, identity4q(), p.hovered || p.active ? float3((r / 0.5f) * 1.2f) : float3((r / 0.5f))),
		};
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(closest_point, identity4q(), float3(0.1f)),
		};
	}
	if (0) { // closest point aabb visualization
		AABB3 aabb = {
			{ -0.5f, 1.0f, -3.0f },
			{ 0.5f, 2.0f, -2.0f },
		};
		static Drag3D p = { { -3.0f, 1.5f, -2.5f }, { 0.0f, 0.0f, 0.0f }, 0, 0 };

		float r = 0.15f;
		float3 closest_point = closest_point_aabb(aabb, p.position);
		drag3d_point(&p, r, camera, viewport, mouse_position);

		draw3d_aabb_outline(aabb, 4.0f, WHITE);
		draw3d_arrow(p.position, sub3(closest_point, p.position), 4.0f, WHITE, view_from_world, clip_from_view, viewport.width);
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(p.position, identity4q(), p.hovered || p.active ? float3((r / 0.5f) * 1.2f) : float3(r / 0.5f)),
		};
		instances[instance_count++] = (MeshInstance){
			MESH_SPHERE,
			transform3(closest_point, identity4q(), float3(0.1f)),
		};
	}
	if (0) {
		draw2d_rect(viewport, hex(0x101010));
		float2 origin = rect_center(viewport);

		float size = -0.5f;
		const float3 points[] = {
			{ -size, -size, -size },
			{ size, -size, -size },
			{ size, size, -size },
			{ -size, size, -size },

			{ -size, -size, size },
			{ size, -size, size },
			{ size, size, size },
			{ -size, size, size },
		};

		float angle = PI * time * 0.5f;
		float c = cosf(angle), s = sinf(angle);
		// clang-format off
        float3x3 rotX = {{
             [0] = 1.0f, [3] = 0.0f, [6] = 0.0f,
             [1] = 0.0f, [4] = c,    [7] = -s,
             [2] = 0.0f, [5] = s,    [8] = c,
        }};

        float3x3 rotY = {{
             [0] = c,    [3] = 0.0f, [6] = -s,
             [1] = 0.0f, [4] = 1.0f, [7] = 0.0f,
             [2] = s,    [5] = 0.0f, [8] = c,
        }};

        float3x3 rotZ = {{
             [0] = c,    [3] = -s,   [6] = 0.0f,
             [1] = s,    [4] = c,    [7] = 0.0f,
             [2] = 0.0f, [5] = 0.0f, [8] = 1.0f,
        }};

		// clang-format on
		float2 projected_points[8];
		for (uint32_t index = 0; index < countof(points); ++index) {
			float3 rotated = mul3x3v(rotX, points[index]);
			rotated = mul3x3v(rotY, rotated);
			rotated = mul3x3v(rotZ, rotated);

			float distance = lerp(1.0f, 4.0f, fabsf(sinf(time)));
			float z = 1.0f / (distance - rotated.z);
			float2 projected = {
				rotated.x * z,
				rotated.y * z,
			};
			float2 screen = add2(origin, scale2(projected, 200.0f));

			projected_points[index] = screen;
		}

		for (uint32_t index = 0; index < 4; ++index) {
			draw2d_line(projected_points[index], projected_points[(index + 1) % 4], 4.0f, WHITE);
			draw2d_line(projected_points[(index + 4)], projected_points[(index + 1) % 4 + 4], 4.0f, WHITE);
			draw2d_line(projected_points[index], projected_points[index + 4], 4.0f, WHITE);
		}
	}
	if (0) {
		draw2d_rect(viewport, hex(0x101010));
		float2 origin = rect_center(viewport);
		float ppu = 64.0f;

		float3x3 screen_from_world = affine3x3(diagonal2x2(float2(ppu)), origin);

		Triangle2 tri = {
			{ -2.0f, -1.0f },
			{ 0.0f, 1.0f },
			{ 2.0f, 0.0f },
		};

		draw2d_triangle_outline(
			(Triangle2){ xform2p(screen_from_world, tri.a), xform2p(screen_from_world, tri.b), xform2p(screen_from_world, tri.c) },
			4.0f, WHITE);
	}

	GPU_Swapchain *swapchain = state->swapchain;

	if (resize.x && resize.y) {
		gpu_swapchain_resize(device, swapchain, resize.x, resize.y);
		gpu_image_resize(device, state->depthbuffer, resize.x, resize.y);
	}
	res_cache_tick(frame, &state->cache);

	GPU_CommandList *cmd = gpu_frame_begin(device);
	if (cmd == 0) return false;

	GPU_Image *backbuffer = gpu_swapchain_backbuffer(device, cmd, swapchain);
	if (backbuffer) {
		gpu_pass_begin(cmd,
			(GPU_DrawPassInfo){
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
			gpu_cmd_shader_bind(device, line3d->handle, 0);

			Uniform set1[] = {
				storage_data(0, draw->line3d.instances.base, draw->line3d.instances.offset),
			};
			Uniform set0[] = { uniform_data(0, &fd, sizeof(fd)) };
			gpu_cmd_bind(device, 0, set0, countof(set0));
			gpu_cmd_bind(device, 1, set1, countof(set1));
			gpu_cmd_draw_instanced(cmd, 0, 6, 0, draw->line3d.instances.offset / sizeof(DRAW_LineInstance));
		}

		{
			gpu_cmd_label(cmd, "Unlit");

			RES_Shader *unlit = res_shader(&state->cache, RES_SHADER_UNLIT);
			gpu_cmd_shader_bind(device, unlit->handle, 0);

			VertexLayout shader_vlayout = vertex_layout(
				{ VERTEX_SEMANTIC_POSITION, DATA_FORMAT_FLOAT32x4 },
				{ VERTEX_SEMANTIC_NORMAL, DATA_FORMAT_FLOAT32x4 },
				{ VERTEX_SEMANTIC_UV0, DATA_FORMAT_FLOAT32x2 },
				{ VERTEX_SEMANTIC_UV1, DATA_FORMAT_FLOAT32x2 } //
			);

			Uniform set0[] = { uniform_data(0, &fd, sizeof(fd)) };
			gpu_cmd_bind(device, 0, set0, countof(set0));

			uint64_t index_buffer_offsets[countof(meshes)];
			uint64_t vertex_buffer_offsets[countof(meshes)];
			uint64_t vertex_buffer_sizes[countof(meshes)];

			for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
				Mesh *mesh = meshes + mesh_index;

				uint64_t vertex_buffer_size = mesh->total_vertex_count * shader_vlayout.stride;
				uint64_t vertex_buffer_offset = alignup(cmd->transient_arena->offset, 256);
				void *vertices = arena_push(cmd->transient_arena, alignup(vertex_buffer_size, 256), 256, 0);
				mesh_pack_into(mesh, &shader_vlayout, vertices, vertex_buffer_size);

				index_buffer_offsets[mesh_index] = gpu_cmd_put(cmd, mesh->total_index_count * index_format_to_size(mesh->index_format), mesh->indices);
				vertex_buffer_offsets[mesh_index] = vertex_buffer_offset;
				vertex_buffer_sizes[mesh_index] = vertex_buffer_size;
			}

			for (uint32_t instance_index = 0; instance_index < instance_count; ++instance_index) {
				MeshInstance *instance = &instances[instance_index];
				RID mesh_index = instance->mesh;
				if (mesh_index >= countof(meshes)) continue;

				Mesh mesh = meshes[mesh_index];

				Uniform set1[] = { storage_buffers(0, cmd->transient_buffer, vertex_buffer_offsets[mesh_index], vertex_buffer_sizes[mesh_index]) };
				gpu_cmd_bind(device, 1, set1, countof(set1));

				float4x4 transform = transform4x4(instance->transform);
				gpu_cmd_push_constant(cmd, sizeof(transform), transform.elements);

				if (mesh.index_format == INDEX_FORMAT32)
					gpu_cmd_bind_index_buffer32(cmd, cmd->transient_buffer, index_buffer_offsets[mesh_index]);
				else
					gpu_cmd_bind_index_buffer16(cmd, cmd->transient_buffer, index_buffer_offsets[mesh_index]);

				for (uint32_t part_index = 0; part_index < mesh_part_count(mesh); ++part_index) {
					MeshPart part = meshpart(mesh, part_index);

					if (part.index_count) {
						gpu_cmd_draw_indexed(cmd, part.index_offset, part.index_count, part.vertex_offset);
					} else {
						gpu_cmd_draw(cmd, part.vertex_offset, part.vertex_count);
					}
				}
			}

			gpu_cmd_label(cmd, 0);
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
				gpu_cmd_shader_bind(device, shader->handle, 0);

				GPU_Image *images[32] = { 0 };
				uint32_t image_count = 1;
				for (uint32_t i = 0; i < 32; ++i)
					images[i] = state->white_texture;

				DRAW_QuadInstance *base =
					(DRAW_QuadInstance *)((uint8_t *)qch->instances.base + batch->instance_offset);

				for (uint32_t qi = 0; qi < batch->instance_count; ++qi) {
					DRAW_QuadInstance *quad = base + qi;
					if (quad->imageid && quad->imageid != state->white_texture->id) {
						int32_t found_index = -1;
						for (uint32_t ii = 1; ii < image_count; ++ii)
							if (images[ii]->id == quad->imageid) {
								found_index = ii;
								break;
							}
						if (found_index == -1) {
							ASSERT(image_count < countof(images));
							found_index = image_count++;
							images[found_index] = &device->images[rid_index(quad->imageid)];
							gpu_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, device->images + quad->imageid);
						}
						quad->imageid = found_index;
					}
				}

				Uniform uniforms0[] = {
					uniform_data(0, &fd, sizeof(fd)),
					storage_data(1, base, batch->instance_count * sizeof(DRAW_QuadInstance)),
				};
				Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), state->sampler_linear) };
				gpu_cmd_bind(device, 0, uniforms0, countof(uniforms0));
				gpu_cmd_bind(device, 1, uniforms1, countof(uniforms1));

				gpu_cmd_draw_instanced(cmd, 0, 6, 0, batch->instance_count);
			}
		}

		gpu_pass_end(cmd);
	}
	gpu_frame_end(device, cmd);

	return true;
}
