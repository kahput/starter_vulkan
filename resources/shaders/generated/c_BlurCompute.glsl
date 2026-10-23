#version 450 core
#pragma shader_stage(compute)
// --- shared_start ---


// --- shared_end ---

// --- source_start ---

layout(rgba8, set = 0, binding = 0) uniform image2D image;

layout(local_size_x = 3, local_size_y = 3) in;
void main(void) {
}

// --- source_end ---