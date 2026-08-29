#include "core/debug.h"
#include "core/strings.h"
#include "os.h"
#include "core/arena.h"
#include "core/logger.h"
#include <stdlib.h>

#include <spirv/unified1/spirv.h>
#include <vulkan/vulkan_core.h>

static inline const char *SpvExecutionModelToString(SpvExecutionModel value) {
	switch (value) {
		case SpvExecutionModelVertex:
			return "Vertex";
		case SpvExecutionModelTessellationControl:
			return "TessellationControl";
		case SpvExecutionModelTessellationEvaluation:
			return "TessellationEvaluation";
		case SpvExecutionModelGeometry:
			return "Geometry";
		case SpvExecutionModelFragment:
			return "Fragment";
		case SpvExecutionModelGLCompute:
			return "GLCompute";
		case SpvExecutionModelKernel:
			return "Kernel";
		case SpvExecutionModelTaskNV:
			return "TaskNV";
		case SpvExecutionModelMeshNV:
			return "MeshNV";
		case SpvExecutionModelRayGenerationKHR:
			return "RayGenerationKHR";
		case SpvExecutionModelIntersectionKHR:
			return "IntersectionKHR";
		case SpvExecutionModelAnyHitKHR:
			return "AnyHitKHR";
		case SpvExecutionModelClosestHitKHR:
			return "ClosestHitKHR";
		case SpvExecutionModelMissKHR:
			return "MissKHR";
		case SpvExecutionModelCallableKHR:
			return "CallableKHR";
		case SpvExecutionModelTaskEXT:
			return "TaskEXT";
		case SpvExecutionModelMeshEXT:
			return "MeshEXT";
		default:
			return "Unknown";
	}
}

VkShaderStageFlagBits model_to_shader_stage[7] = {
	[SpvExecutionModelVertex] = VK_SHADER_STAGE_VERTEX_BIT,
	[SpvExecutionModelFragment] = VK_SHADER_STAGE_FRAGMENT_BIT,
	[SpvExecutionModelGLCompute] = VK_SHADER_STAGE_COMPUTE_BIT,
};

int main(void) {
	ArenaTemp scratch = arena_scratch_begin(0);

	/* String8 shader = s("assets/shaders/vertex/bin/batch3d.vertex.spv"); */
	String8 shader = s("assets/shaders/fragment/bin/phong.fragment.spv");
	int result = system((char *)str8_concat(scratch.arena, s("spirv-dis "), shader).text);

	LOG_INFO("#%.*s", sspread(os_file_read_entire(scratch.arena, s("dis.test"))));

	String8 binary = os_file_read_entire(scratch.arena, shader);

	uint32_t spv_word_count = binary.length / 4;
	uint32_t *data = (uint32_t *)binary.text;
	uint32_t magic_number = data[0];
	ASSERT(magic_number == 0x07230203);

	uint32_t id_bound = data[3];
	typedef struct {
		uint32_t id_index;
		uint32_t offset;
	} SpvMember;

	typedef struct {
		SpvOp op;
		uint32_t set;
		uint32_t binding;

		uint8_t width;
		uint8_t sign;

		uint32_t type_index;
		uint32_t count;

		SpvStorageClass storage_class;

		uint32_t value;

		String8 name;

		SpvMember *members;
		uint32_t member_count;
	} SpvIdentifier;

	SpvIdentifier *ids = arena_push_count(scratch.arena, SpvIdentifier, id_bound);

	VkShaderStageFlagBits stage = 0;

	uint32_t word_index = 5;
	while (word_index < spv_word_count) {
		SpvOp op = data[word_index] & 0xFF;
		uint16_t word_count = (uint16_t)(data[word_index] >> 16);

		switch (op) {
			case SpvOpEntryPoint: {
				SpvExecutionModel model = data[word_index + 1];
				stage = model_to_shader_stage[model];

				ASSERT_FORMAT(stage, "Unsupported shader type %s", SpvExecutionModelToString(model));
				LOG_INFO("Shader stage %s", SpvExecutionModelToString(model));
			} break;

			case SpvOpDecorate: {
				uint32_t id_index = data[word_index + 1];
				SpvIdentifier *id = &ids[id_index];

				SpvDecoration decoration = data[word_index + 2];

				switch (decoration) {
					case SpvDecorationBinding: {
						id->binding = data[word_index + 3];
					} break;
					case SpvDecorationDescriptorSet: {
						id->set = data[word_index + 3];
					} break;
					default:
						break;
				}
			} break;

			case SpvOpMemberDecorate: {
				ASSERT(word_count >= 4);

				uint32_t id_index = data[word_index + 1];
				SpvIdentifier *id = &ids[id_index];

				uint32_t member_index = data[word_index + 2];

				if (id->members == 0)
					id->members = arena_push_count(scratch.arena, SpvMember, 64);

			} break;

			case SpvOpName: {
				ASSERT(word_count >= 3);

				uint32_t id_index = data[word_index + 1];
				ASSERT(id_index < id_bound);

				SpvIdentifier *id = &ids[id_index];
				id->name = str8_wrap((char *)(data + word_index + 2));

				break;
			}
			case SpvOpTypeVector: {
				uint32_t id_index = data[word_index + 1];
				SpvIdentifier *id = &ids[id_index];

				id->op = op;
				id->type_index = data[word_index + 2];
				id->count = data[word_index + 3];
			} break;
			case SpvOpTypeStruct: {
				ASSERT(word_count >= 2);

				uint32_t id_index = data[word_index + 1];
				ASSERT(id_index < id_bound);

				SpvIdentifier *id = &ids[id_index];
				id->op = op;

				if (word_count > 2) {
					for (uint32_t member_index = 0; member_index < id->member_count; ++member_index)
						id->members[member_index].id_index = data[word_index + member_index + 2];
				}

				break;
			}

			case SpvOpTypePointer: {
				ASSERT(word_count >= 4);

				uint32_t id_index = data[word_index + 1];
				SpvIdentifier *id = &ids[id_index];

				id->op = op;
				id->type_index = data[word_index + 3];
			} break;

			case SpvOpVariable: {
				ASSERT(word_count >= 4);

				uint32_t id_index = data[word_index + 2];
				SpvIdentifier *id = &ids[id_index];

				id->op = op;
				id->type_index = data[word_index + 1];
				id->storage_class = data[word_index + 3];

			} break;

			default:
				break;
		}

		word_index += word_count;
	}

	typedef struct {
		VkDescriptorSetLayoutBinding bindings[32];
		uint32_t binding_count;
	} Set;

	Set sets[3];

	for (uint32_t id_index = 0; id_index < id_bound; ++id_index) {
		SpvIdentifier *id = &ids[id_index];

		if (id->op == SpvOpVariable) {
			switch (id->storage_class) {
				case SpvStorageClassUniformConstant:
				case SpvStorageClassUniform: {
					SpvIdentifier uniform_type = ids[ids[id->type_index].type_index];

					LOG_INFO("set = %u, binding = %u", id->set, id->binding);

					VkDescriptorSetLayoutBinding *binding = &sets[id->set].bindings[sets[id->set].binding_count++];
					binding->binding = id->binding;
					binding->descriptorCount = 1;

					switch (uniform_type.op) {
						case SpvOpTypeStruct: {
							binding->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
							LOG_INFO("%s", uniform_type.name.text);
							/* for (uint32_t member_index = 0; member_index < uniform_type.member_count; ++member_index) { */
							/* LOG_INFO("    %s", ids[uniform_type.members[); */
							/* } */
						} break;
						case SpvOpTypeSampledImage: {
							binding->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
							LOG_INFO("%s", uniform_type.name.text);
						} break;
						case SpvOpNop:
							break;
						default:
							ASSERT(false);
							break;
					}

				} break;

				default:
					break;
			}
		}
	}

	arena_scratch_end(scratch);
	return 0;
}
