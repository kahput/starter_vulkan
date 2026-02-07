#include "input.h"
#include "mesh_source.h"

#include <game_interface.h>

#include <common.h>
#include <core/cmath.h> // Using your new library
#include <core/arena.h>
#include <core/debug.h>
#include <core/logger.h>
#include <core/astring.h>

#include <renderer.h>
#include <renderer/r_internal.h>
#include <renderer/backend/vulkan_api.h>

#include <assets.h>
#include <assets/asset_types.h>

#include <math.h>
#include <string.h>

static GameInterface interface;

typedef struct {
    RhiBuffer vb;
    uint32_t vertex_count;
} Terrain;

typedef struct {
    Arena arena;
    // Sprite shader
    RhiShader sprite_shader;
    RhiGroupResource sprite_material;

    RhiShader terrain_shader;
    RhiGroupResource terrain_material;

    // Quad mesh
    RhiBuffer quad_vb;
    uint32_t quad_vertex_count;

    Terrain terrain[2];

    uint32_t current_frame;

    float3 player_position; // Changed from vec3
    Camera camera;

    // Texture
    RhiTexture sprite_texture;
    RhiTexture checkered_texture;

    uint32_t variant_index;

    bool is_initialized;
} GameState;

static GameState *state = NULL;

RhiShader create_shader(GameContext *context, String filename);
RhiTexture create_texture(GameContext *context, String filename);

// Changed signature: passing pointer to float3 to allow modification
void player_update(float3 *player_position, float dt, Camera *camera);

#define CAMERA_SENSITIVITY .001f
#define MOVE_SPEED 5.f
#define SPRING_LENGTH 16.f

bool game_on_load(GameContext *context) {
    LOG_INFO("Game loading...");
    LOG_INFO("Game loaded successfully");
    return true;
}

FrameInfo game_on_update(GameContext *context, float dt) {
    state = (GameState *)context->permanent_memory;

    ArenaTemp scratch = arena_scratch(NULL);
    if (state->is_initialized == false) {
        state->arena = (Arena){
            .memory = state + 1,
            .offset = 0,
            .capacity = context->permanent_memory_size - sizeof(GameState)
        };

        state->current_frame = 0;
        for (uint32_t index = 0; index < countof(state->terrain); ++index)
            state->terrain[index].vb.id = 0;

        state->sprite_shader = create_shader(context, str_lit("sprite.glsl"));
        state->terrain_shader = create_shader(context, str_lit("terrain.glsl"));

        MeshSource plane_src = mesh_source_cube_face_create(scratch.arena, 0, 0, 0, CUBE_FACE_FRONT);

        state->quad_vertex_count = plane_src.vertex_count;

        state->quad_vb = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_VERTEX,
            plane_src.vertex_size * plane_src.vertex_count, plane_src.vertices);

        // Load sprite texture
        state->sprite_texture = create_texture(context, str_lit("tile_0085.png"));
        state->checkered_texture = create_texture(context, str_lit("texture_09.png"));

        // Create material
        float4 sprite_tint = { 1.0f, 1.0f, 1.0f, 1.0f };

        state->sprite_material = vulkan_renderer_resource_group_create(context->vk_context,
            state->sprite_shader, 1);

        vulkan_renderer_resource_group_write(context->vk_context, state->sprite_material,
            0, 0, sizeof(float4), &sprite_tint, true);

        vulkan_renderer_resource_group_set_texture_sampler(context->vk_context,
            state->sprite_material, 0, state->sprite_texture, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_NEAREST, 0 });

        float4 terrain_tint = { 1.0f, 1.0f, 1.0f, 1.0f };

        state->terrain_material = vulkan_renderer_resource_group_create(context->vk_context,
            state->terrain_shader, 1);

        vulkan_renderer_resource_group_write(
            context->vk_context, state->terrain_material,
            0, 0, sizeof(float4), &terrain_tint, true);

        vulkan_renderer_resource_group_set_texture_sampler(context->vk_context,
            state->terrain_material, 0, state->checkered_texture, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR, 0 });

        float azimuth = C_PIf * 3 / 2.f;
        float thetha = C_PIf / 3.f;

        state->camera = (Camera){
            .position = {
              (SPRING_LENGTH * sinf(thetha) * cosf(azimuth)) + state->player_position.x,
              SPRING_LENGTH * cosf(thetha),
              (SPRING_LENGTH * sinf(thetha) * sinf(azimuth)) + state->player_position.z,
            },
            .target = { 0.0f, 0.0f, 0.0f },
            .up = { 0.0f, 1.0f, 0.0f },
            .fov = 45.f,
            .projection = CAMERA_PROJECTION_PERSPECTIVE
        };

        state->is_initialized = true;
    }

    Terrain *terrain = &state->terrain[state->current_frame];

    MeshList list = { 0 };

    uint32_t size = 32;
    for (uint32_t z = 0; z < size; ++z) {
        for (uint32_t x = 0; x < size; x++) {
            for (int32_t face_index = 0; face_index < 6; ++face_index) {
                float x_offset = (float)x - ((float)size * .5f);
                float z_offset = (float)z - ((float)size * .5f);

                MeshSource source = mesh_source_cube_face_create(
                    scratch.arena, x_offset, -1.f, z_offset, face_index);
                mesh_source_list_push(scratch.arena, &list, source);

                if (x == 0 || x + 1 == size || z == 0 || z + 1 == size) {
                    for (uint32_t y = 0; y < 3; ++y) {
                        MeshSource wall = mesh_source_cube_face_create(
                            scratch.arena, x_offset, y, z_offset, face_index);
                        mesh_source_list_push(scratch.arena, &list, wall);
                    }
                }
            }
        }
    }
    MeshSource cube_src = mesh_source_list_flatten(scratch.arena, &list);

    if (terrain->vb.id || terrain->vertex_count < cube_src.vertex_count) {
        vulkan_renderer_buffer_destroy(context->vk_context, terrain->vb);
        terrain->vb.id = 0;
        terrain->vb = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_VERTEX,
            cube_src.vertex_size * cube_src.vertex_count, cube_src.vertices);
    } else
        vulkan_renderer_buffer_write(context->vk_context, terrain->vb,
            0, cube_src.vertex_size * cube_src.vertex_count, cube_src.vertices);

    terrain->vertex_count = cube_src.vertex_count;

    Matrix4f transform = mat4f_translated(state->player_position);

    vulkan_renderer_shader_bind(
        context->vk_context, state->sprite_shader,
        RENDERER_DEFAULT_SHADER_VARIANT_STANDARD);

    vulkan_renderer_resource_group_write(
        context->vk_context, state->sprite_material,
        0, 0, sizeof(float4),
        &(float4){ 1.0f, 1.0f, 1.0f, 1.0f }, false);

    vulkan_renderer_resource_group_write(
        context->vk_context, state->terrain_material,
        0, 0, sizeof(float4),
        &(float4){ 1.0f, 1.0f, 1.0f, 1.0f }, false);

    vulkan_renderer_resource_group_bind(context->vk_context, state->sprite_material, 0);
    vulkan_renderer_resource_local_write(context->vk_context, 0, sizeof(Matrix4f), &transform);
    vulkan_renderer_buffer_bind(context->vk_context, state->quad_vb, 0);
    vulkan_renderer_draw(context->vk_context, state->quad_vertex_count);

    transform = mat4f_identity();

    vulkan_renderer_shader_bind(
        context->vk_context, state->terrain_shader,
        state->variant_index + 1);
    vulkan_renderer_resource_group_bind(context->vk_context, state->terrain_material, 0);

    vulkan_renderer_resource_local_write(context->vk_context, 0, sizeof(Matrix4f), &transform);
    vulkan_renderer_buffer_bind(context->vk_context, terrain->vb, 0);
    vulkan_renderer_draw(context->vk_context, terrain->vertex_count);

    arena_release_scratch(scratch);
    state->current_frame = (state->current_frame + 1) % 2;

    if (input_key_pressed(KEY_CODE_ENTER))
        state->variant_index = !state->variant_index;

    player_update(&state->player_position, dt, &state->camera);

    FrameInfo info = {
        .camera = state->camera,
    };

    return info;
}

bool game_on_unload(GameContext *context) {
    LOG_INFO("Game unloading...");
    return true;
}

RhiShader create_shader(GameContext *context, String filename) {
    ShaderSource *shader_src = NULL;
    asset_library_request_shader(context->asset_library, filename, &shader_src);
    if (shader_src == NULL) {
        LOG_ERROR("Failed to load %.*s shader", str_expand(filename));
        ASSERT(false);
        return (RhiShader){ 0 };
    }

    ShaderConfig config = {
        .vertex_code = shader_src->vertex_shader.content,
        .vertex_code_size = shader_src->vertex_shader.size,
        .fragment_code = shader_src->fragment_shader.content,
        .fragment_code_size = shader_src->fragment_shader.size,
    };

    PipelineDesc desc = DEFAULT_PIPELINE();
    desc.cull_mode = CULL_MODE_BACK;
    desc.front_face = FRONT_FACE_COUNTER_CLOCKWISE;

    ShaderReflection reflection;

    RhiShader shader = vulkan_renderer_shader_create(&state->arena, context->vk_context,
        (RhiGlobalResource){ RENDERER_GLOBAL_RESOURCE_MAIN, 0 }, &config, &reflection);

    vulkan_renderer_shader_variant_create(context->vk_context, shader,
        (RhiPass){ RENDERER_DEFAULT_PASS_MAIN, 0 }, desc);

    desc.polygon_mode = POLYGON_MODE_LINE;
    vulkan_renderer_shader_variant_create(context->vk_context, shader,
        (RhiPass){ RENDERER_DEFAULT_PASS_MAIN, 0 }, desc);

    return shader;
}

RhiTexture create_texture(GameContext *context, String filename) {
    ImageSource *texture_src = NULL;
    UUID texture_id = asset_library_request_image(context->asset_library, filename, &texture_src);

    if (!texture_src) {
        LOG_ERROR("Failed to load texture texture");
        return (RhiTexture){ 0 };
    }

    RhiTexture texture = vulkan_renderer_texture_create(context->vk_context,
        texture_src->width, texture_src->height,
        TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
        TEXTURE_USAGE_SAMPLED, texture_src->pixels);

    return texture;
}

void player_update(float3 *player_position, float dt, Camera *camera) {
	static float azimuth = C_PIf * 3 / 2.f;
	static float theta = C_PIf / 3.f;

	float yaw_delta = input_mouse_dx() * CAMERA_SENSITIVITY;
    float pitch_delta = input_mouse_dy() * CAMERA_SENSITIVITY;

    azimuth = fmodf(azimuth + yaw_delta, C_PIf * 2.f);
    if (azimuth < 0)
        azimuth += C_PIf * 2.f;

    theta = clamp(theta - pitch_delta, C_PIf / 4.f, C_PIf / 2.f);

    float3 offset = float3_subtract(camera->position, *player_position);

    float r = float3_length(offset);
    if (r < EPSILON)
        r = EPSILON;

    float current_theta = acosf(offset.y / r);
    float current_azimuth = atan2f(offset.z, offset.x); // [-pi, pi]

    if (current_azimuth < 0)
        current_azimuth += C_PIf * 2.f;

    float da = azimuth - current_azimuth;
    if (da > C_PI)
        da -= C_PI * 2.f;
    if (da < -C_PI)
        da += C_PI * 2.f;

    float lerp = 10.0f * dt;

    current_azimuth += lerp * da;
    current_theta += lerp * (theta - current_theta);

    camera->position = (float3){
          (SPRING_LENGTH * sinf(current_theta) * cosf(current_azimuth)) + player_position->x,
          SPRING_LENGTH * cosf(current_theta),
          (SPRING_LENGTH * sinf(current_theta) * sinf(current_azimuth)) + player_position->z,
    };

    float3 camera_position = camera->position;
    camera_position.y = 0.0f;
    float3 camera_target = camera->target;
    camera_target.y = 0.0f;

    float3 forward, right;

    forward = float3_normalize(float3_subtract(camera_target, camera_position));
    
    right = float3_cross(forward, camera->up);
    right = float3_normalize(right);

    float3 direction = { 0, 0, 0 };
    
    // Logic: direction += forward * scalar
    float forward_input = (float)(input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S));
    direction = float3_add(direction, float3_scale(forward, forward_input));

    float right_input = (float)(input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A));
    direction = float3_add(direction, float3_scale(right, right_input));

    if (float3_length(direction) > 1e-6f)
        direction = float3_normalize(direction);

    // Apply movement to player_position (passed by pointer)
    *player_position = float3_add(*player_position, float3_scale(direction, MOVE_SPEED * dt));
    
    camera->target.x = player_position->x;
    camera->target.z = player_position->z;
}

GameInterface *game_hookup(void) {
    interface = (GameInterface){
        .on_load = game_on_load,
        .on_update = game_on_update,
        .on_unload = game_on_unload,
    };
    return &interface;
}
