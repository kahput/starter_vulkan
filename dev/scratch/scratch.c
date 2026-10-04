#include "core/arena.h"
#include "os.h"

#include "core/debug.h"
#include "core/logger.h"

#include "utils/input.h"

typedef bool (*TickFn)(Arena *arena, InputState *input);

int main(int32_t argc, const char *argv[]) {
	float2 start = { 0.0f, 0.0f };
	float2 end = { 1.0f, 1.0f };
	float thickness = 3.0f;

	float2 line = sub2(end, start);
	float2 direction = norm2(line);
	float2 normal = make2(-direction.y, direction.x);

	float2 proj = scale2(unit2(RIGHT), dot2(line, unit2(RIGHT)));
	float2 s = add2(scale2(normal, thickness), line);

    return 0;
}
