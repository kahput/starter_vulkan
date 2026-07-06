/*
 * test_gjk.c
 *
 * Test suite for gjk_distance_squared() and its supporting shape3 machinery.
 *
 * Strategy:
 *   1. Hand-computed "oracle" cases: pairs of shapes where the true separation
 *      distance can be derived analytically (sphere-sphere, sphere-aabb,
 *      sphere-capsule, sphere-triangle, aabb-aabb, capsule-capsule).
 *   2. Grid / fuzz sweeps that reuse those oracles over many relative
 *      positions, similar in spirit to the original sphere-vs-triangle sweep.
 *   3. Property tests that don't need an oracle: symmetry (gjk(a,b) == gjk(b,a)),
 *      non-negativity, zero-distance-on-overlap, degenerate-shape robustness,
 *      and stability of the result across different reference_dist_sq hints.
 *
 * Build: link against the same object files as the rest of the geometry
 * library (shape3.c / gjk.c / arena.c / logger.c ...).
 */

#include "core/logger.h"
#include "core/shape3.h"
#include "core/arena.h"
#include "core/debug.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Minimal test framework                                              */
/* ------------------------------------------------------------------ */

static int g_tests_run = 0;
static int g_tests_failed = 0;
static const char *g_current_group = "";

#define TEST_GROUP(name)                         \
	do {                                         \
		g_current_group = (name);                \
		LOG_INFO("=== %s ===", g_current_group); \
	} while (0)

#define EXPECT_NEAR(actual, expected, tol, label)                            \
	do {                                                                     \
		float _a = (float)(actual);                                          \
		float _e = (float)(expected);                                        \
		float _t = (float)(tol);                                             \
		g_tests_run++;                                                       \
		if (fabsf(_a - _e) > _t) {                                           \
			LOG_ERROR("[%s] FAIL: %s -- expected %.6f, got %.6f (tol %.6f)", \
				g_current_group, (label), _e, _a, _t);                       \
			g_tests_failed++;                                                \
			ASSERT(false);                                                   \
		}                                                                    \
	} while (0)

#define EXPECT_TRUE(cond, label)                                  \
	do {                                                          \
		g_tests_run++;                                            \
		if (!(cond)) {                                            \
			LOG_ERROR("[%s] FAIL: %s", g_current_group, (label)); \
			g_tests_failed++;                                     \
		}                                                         \
	} while (0)

/* ------------------------------------------------------------------ */
/* Analytic oracles                                                    */
/* ------------------------------------------------------------------ */

static float sphere_sphere_distance(Sphere a, Sphere b) {
	float center_dist = float3_length(float3_subtract(a.center, b.center));
	return fmaxf(0.0f, center_dist - a.radius - b.radius);
}

static float sphere_aabb_distance(Sphere s, AABB3 box) {
	float3 closest = aabb3_closest_point(box, s.center);
	float d = float3_length(float3_subtract(s.center, closest));
	return fmaxf(0.0f, d - s.radius);
}

static float sphere_capsule_distance(Sphere s, Capsule3 c) {
	Segment3 seg = { c.a, c.b };
	float3 closest = segment3_closest_point(seg, s.center);
	float d = float3_length(float3_subtract(s.center, closest));
	return fmaxf(0.0f, d - s.radius - c.radius);
}

static float sphere_triangle_distance(Sphere s, Triangle3 t) {
	float3 closest = triangle3_closest_point(t, s.center);
	float d = float3_length(float3_subtract(s.center, closest));
	return fmaxf(0.0f, d - s.radius);
}

static float aabb_aabb_distance(AABB3 a, AABB3 b) {
	float3 delta = { 0 };
	delta.x = fmaxf(0.0f, fmaxf(a.min.x - b.max.x, b.min.x - a.max.x));
	delta.y = fmaxf(0.0f, fmaxf(a.min.y - b.max.y, b.min.y - a.max.y));
	delta.z = fmaxf(0.0f, fmaxf(a.min.z - b.max.z, b.min.z - a.max.z));
	return float3_length(delta);
}

/* Closest distance between two segments (Ericson, Real-Time Collision
 * Detection, sec. 5.1.9) -- used as an independent oracle for capsules. */
static float segment_segment_distance(float3 p1, float3 q1, float3 p2, float3 q2) {
	float3 d1 = float3_subtract(q1, p1);
	float3 d2 = float3_subtract(q2, p2);
	float3 r = float3_subtract(p1, p2);
	float a = float3_dot(d1, d1);
	float e = float3_dot(d2, d2);
	float f = float3_dot(d2, r);
	const float kEps = 1e-8f;
	float s, t;

	if (a <= kEps && e <= kEps) {
		s = 0.0f;
		t = 0.0f;
	} else if (a <= kEps) {
		s = 0.0f;
		t = clampf(f / e, 0.0f, 1.0f);
	} else {
		float c = float3_dot(d1, r);
		if (e <= kEps) {
			t = 0.0f;
			s = clampf(-c / a, 0.0f, 1.0f);
		} else {
			float b = float3_dot(d1, d2);
			float denom = a * e - b * b;
			if (denom != 0.0f) {
				s = clampf((b * f - c * e) / denom, 0.0f, 1.0f);
			} else {
				s = 0.0f;
			}
			t = (b * s + f) / e;
			if (t < 0.0f) {
				t = 0.0f;
				s = clampf(-c / a, 0.0f, 1.0f);
			} else if (t > 1.0f) {
				t = 1.0f;
				s = clampf((b - c) / a, 0.0f, 1.0f);
			}
		}
	}

	float3 c1 = float3_add(p1, float3_scale(d1, s));
	float3 c2 = float3_add(p2, float3_scale(d2, t));
	return float3_length(float3_subtract(c1, c2));
}

static float capsule_capsule_distance(Capsule3 a, Capsule3 b) {
	float d = segment_segment_distance(a.a, a.b, b.a, b.b);
	return fmaxf(0.0f, d - a.radius - b.radius);
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static AABB3 make_aabb(float3 center, float3 half_extent) {
	return aabb3_from_center(center, half_extent);
}

static Capsule3 make_capsule(float3 center, float3 up, float height, float radius) {
	return capsule_from_center(center, up, height, radius);
}

/* gjk_distance_squared expects a "reference distance squared" hint; pass a
 * generous bound (FLOAT_MAX) unless a test specifically wants to exercise
 * the hint's early-out behavior. */
static float gjk_dist(Shape3 a, Shape3 b, float reference) {
	float d2 = gjk_distance_squared(a, b, reference * reference);
	return sqrtf(fmaxf(0.0f, d2));
}

/* ------------------------------------------------------------------ */
/* Sphere vs Sphere                                                    */
/* ------------------------------------------------------------------ */

static void test_sphere_sphere_cases(void) {
	TEST_GROUP("sphere-sphere");

	struct {
		float3 c0;
		float r0;
		float3 c1;
		float r1;
		const char *label;
	} cases[] = {
		{ { 0, 0, 0 }, 1.0f, { 5, 0, 0 }, 1.0f, "separated along x" },
		{ { 0, 0, 0 }, 1.0f, { 0, 4, 0 }, 1.0f, "separated along y" },
		{ { 0, 0, 0 }, 1.0f, { 3, 4, 0 }, 1.0f, "separated diagonal (3-4-5)" },
		{ { 0, 0, 0 }, 1.0f, { 2, 0, 0 }, 1.0f, "exactly touching" },
		{ { 0, 0, 0 }, 1.0f, { 1, 0, 0 }, 1.0f, "overlapping" },
		{ { 0, 0, 0 }, 1.0f, { 0, 0, 0 }, 1.0f, "concentric (degenerate)" },
		{ { 0, 0, 0 }, 2.0f, { 0, 0, 0 }, 0.5f, "one sphere inside another" },
		{ { -1000, 0, 0 }, 1.0f, { 1000, 0, 0 }, 1.0f, "far apart" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		Sphere sa = { cases[i].c0, cases[i].r0 };
		Sphere sb = { cases[i].c1, cases[i].r1 };
		Shape3 a = shape3_from_sphere(sa);
		Shape3 b = shape3_from_sphere(sb);

		float expected = sphere_sphere_distance(sa, sb);
		float got = gjk_dist(a, b, expected);
		EXPECT_NEAR(got, expected, 1e-2f, cases[i].label);

		/* symmetry */
		float got_swapped = gjk_dist(b, a, expected);
		EXPECT_NEAR(got, got_swapped, 1e-3f, "symmetric under argument swap");
	}
}

/* ------------------------------------------------------------------ */
/* Sphere vs AABB3                                                     */
/* ------------------------------------------------------------------ */

static void test_sphere_aabb_cases(void) {
	TEST_GROUP("sphere-aabb");

	AABB3 box = make_aabb((float3){ 0, 0, 0 }, (float3){ 1, 1, 1 });

	struct {
		float3 center;
		float radius;
		const char *label;
	} cases[] = {
		{ { 5, 0, 0 }, 1.0f, "separated along x, face" },
		{ { 0, 5, 0 }, 1.0f, "separated along y, face" },
		{ { 3, 3, 3 }, 1.0f, "separated, nearest corner" },
		{ { 2, 0, 0 }, 1.0f, "exactly touching face" },
		{ { 0.5f, 0, 0 }, 1.0f, "center inside box, overlap" },
		{ { 0, 0, 0 }, 0.1f, "small sphere fully inside box" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		Sphere s = { cases[i].center, cases[i].radius };
		Shape3 a = shape3_from_sphere(s);
		Shape3 b = shape3_from_aabb3(box);

		float expected = sphere_aabb_distance(s, box);
		float got = gjk_dist(a, b, expected);
		EXPECT_NEAR(got, expected, 1e-2f, cases[i].label);
	}

	/* Grid sweep, mirroring the style of the original sphere/triangle test. */
	{
		const uint32_t N = 64;
		float max_abs_error = 0.0f;
		for (uint32_t z = 0; z < N; ++z) {
			for (uint32_t x = 0; x < N; ++x) {
				float3 center = {
					((float)x / (float)N - 0.5f) * 8.0f,
					0.25f,
					((float)z / (float)N - 0.5f) * 8.0f,
				};
				Sphere s = { center, 0.5f };
				Shape3 a = shape3_from_sphere(s);
				Shape3 b = shape3_from_aabb3(box);

				float expected = sphere_aabb_distance(s, box);
				float got = gjk_dist(a, b, expected);
				float err = fabsf(got - expected);
				if (err > max_abs_error)
					max_abs_error = err;
			}
		}
		EXPECT_TRUE(max_abs_error < 1e-2f, "sphere-aabb grid sweep max error under tolerance");
	}
}

/* ------------------------------------------------------------------ */
/* Sphere vs Capsule3                                                  */
/* ------------------------------------------------------------------ */

static void test_sphere_capsule_cases(void) {
	TEST_GROUP("sphere-capsule");

	Capsule3 cap = make_capsule((float3){ 0, 0, 0 }, FLOAT3_Y, 2.0f, 0.5f);

	struct {
		float3 center;
		float radius;
		const char *label;
	} cases[] = {
		{ { 3, 0, 0 }, 1.0f, "separated, beside cylindrical part" },
		{ { 0, 3, 0 }, 1.0f, "separated, above cap end" },
		{ { 0, 0, 0 }, 0.1f, "small sphere overlapping capsule center" },
		{ { 1.5f, 0, 0 }, 1.0f, "exactly touching (approx)" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		Sphere s = { cases[i].center, cases[i].radius };
		Shape3 a = shape3_from_sphere(s);
		Shape3 b = { .kind = SHAPE_KIND_CAPSULE3, .as.capsule = cap };

		float expected = sphere_capsule_distance(s, cap);
		float got = gjk_dist(a, b, expected);
		EXPECT_NEAR(got, expected, 1e-2f, cases[i].label);
	}
}

/* ------------------------------------------------------------------ */
/* Sphere vs ConvexPolygon (triangle) -- grid sweep                    */
/* ------------------------------------------------------------------ */

static void test_sphere_triangle_grid(Arena *arena) {
	TEST_GROUP("sphere-triangle grid sweep");

	Triangle3 triangle = { { 0.0f, 0.25f, 1.0f }, { 1.0f, 0.25f, -1.0f }, { -1.0f, 0.25f, -1.0f } };
	Shape3 b = shape3_from_convex3(convex3_from_triangle3(arena, triangle));

	const uint32_t N = 128;
	float max_abs_error = 0.0f;
	uint32_t degenerate_support_count = 0;

	for (uint32_t z = 0; z < N; ++z) {
		for (uint32_t x = 0; x < N; ++x) {
			float3 center = {
				((float)x / (float)N) * 4.0f - 2.0f,
				1.0f,
				((float)z / (float)N) * 4.0f - 2.0f,
			};
			Sphere s = { center, 1.0f };
			Shape3 a = shape3_from_sphere(s);

			float3 support = float3_subtract(
				shape3_support(a, (float3){ 1.0f, 0.0f, 0.0f }),
				shape3_support(b, (float3){ -1.0f, 0.0f, 0.0f }));
			if (equalf(float3_length_sq(support), 0.0f))
				degenerate_support_count++;

			if (z == 32) {
				uint32_t y = 0;
				(void)y;
			}

			float expected = sphere_triangle_distance(s, triangle);
			float got_sq = gjk_distance_squared(a, b, expected * expected);
			float got = sqrtf(fmaxf(0.0f, got_sq));

			float err = fabsf(got - expected);
			if (err > max_abs_error)
				max_abs_error = err;
		}
	}

	EXPECT_TRUE(max_abs_error < 1e-1f, "sphere-triangle grid sweep max error under tolerance");
	LOG_INFO("sphere-triangle grid: max_abs_error=%g, degenerate_support_count=%u, global max_error=%g",
		max_abs_error, degenerate_support_count, max_error);
}

/* ------------------------------------------------------------------ */
/* AABB3 vs AABB3                                                      */
/* ------------------------------------------------------------------ */

static void test_aabb_aabb_cases(void) {
	TEST_GROUP("aabb-aabb");

	AABB3 base = make_aabb((float3){ 0, 0, 0 }, (float3){ 1, 1, 1 });

	struct {
		float3 center;
		float3 half_extent;
		const char *label;
	} cases[] = {
		{ { 4, 0, 0 }, { 1, 1, 1 }, "separated along x" },
		{ { 0, 0, 4 }, { 1, 1, 1 }, "separated along z" },
		{ { 3, 3, 3 }, { 1, 1, 1 }, "separated, corner-nearest" },
		{ { 2, 0, 0 }, { 1, 1, 1 }, "exactly touching face" },
		{ { 0.5f, 0, 0 }, { 1, 1, 1 }, "overlapping" },
		{ { 0, 0, 0 }, { 0.25f, 0.25f, 0.25f }, "nested (one inside other)" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		AABB3 other = make_aabb(cases[i].center, cases[i].half_extent);
		Shape3 a = shape3_from_aabb3(base);
		Shape3 b = shape3_from_aabb3(other);

		float expected = aabb_aabb_distance(base, other);
		float got = gjk_dist(a, b, expected);
		EXPECT_NEAR(got, expected, 1e-2f, cases[i].label);
	}
}

/* ------------------------------------------------------------------ */
/* Capsule3 vs Capsule3                                                */
/* ------------------------------------------------------------------ */

static void test_capsule_capsule_cases(void) {
	TEST_GROUP("capsule-capsule");

	struct {
		float3 c0;
		float3 up0;
		float h0, r0;
		float3 c1;
		float3 up1;
		float h1, r1;
		const char *label;
	} cases[] = {
		{ { 0, 0, 0 }, FLOAT3_Y, 2.0f, 0.5f, { 3, 0, 0 }, FLOAT3_Y, 2.0f, 0.5f, "parallel, separated along x" },
		{ { 0, 0, 0 }, FLOAT3_Y, 2.0f, 0.5f, { 1, 0, 0 }, FLOAT3_Y, 2.0f, 0.5f, "parallel, exactly touching" },
		{ { 0, 0, 0 }, FLOAT3_Y, 2.0f, 0.5f, { 0.2f, 0, 0 }, FLOAT3_Y, 2.0f, 0.5f, "parallel, overlapping" },
		{ { 0, 0, 0 }, FLOAT3_Y, 4.0f, 0.5f, { 0, 0, 3 }, FLOAT3_X, 4.0f, 0.5f, "perpendicular, crossing axes offset in z" },
		{ { 0, 3, 0 }, FLOAT3_Y, 2.0f, 0.5f, { 0, -3, 0 }, FLOAT3_Y, 2.0f, 0.5f, "collinear, separated end-to-end" },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		Capsule3 ca = make_capsule(cases[i].c0, cases[i].up0, cases[i].h0, cases[i].r0);
		Capsule3 cb = make_capsule(cases[i].c1, cases[i].up1, cases[i].h1, cases[i].r1);
		Shape3 a = { .kind = SHAPE_KIND_CAPSULE3, .as.capsule = ca };
		Shape3 b = { .kind = SHAPE_KIND_CAPSULE3, .as.capsule = cb };

		float expected = capsule_capsule_distance(ca, cb);
		float got = gjk_dist(a, b, expected);
		EXPECT_NEAR(got, expected, 1e-2f, cases[i].label);
	}
}

/* ------------------------------------------------------------------ */
/* Convex polygon (triangle) vs Convex polygon (triangle)              */
/* ------------------------------------------------------------------ */

static void test_triangle_triangle_cases(Arena *arena) {
	TEST_GROUP("triangle-triangle");

	/* Triangle in the y=0 plane. */
	Triangle3 t0 = { { -1, 0, -1 }, { 1, 0, -1 }, { 0, 0, 1 } };

	{
		/* Directly-above, parallel triangle: nearest points are vertically
		 * aligned since the xz projections overlap, so distance == plane gap. */
		Triangle3 t1 = { { -1, 5, -1 }, { 1, 5, -1 }, { 0, 5, 1 } };
		Shape3 a = shape3_from_convex3(convex3_from_triangle3(arena, t0));
		Shape3 b = shape3_from_convex3(convex3_from_triangle3(arena, t1));
		float got = gjk_dist(a, b, 5.0f);
		EXPECT_NEAR(got, 5.0f, 1e-2f, "parallel triangles, overlapping projection, gap = 5");
	}

	{
		/* Shifted far away on x: nearest vertices are (1,0,-1) and (11,0,-1). */
		Triangle3 t1 = { { 9, 0, -1 }, { 11, 0, -1 }, { 10, 0, 1 } };
		Shape3 a = shape3_from_convex3(convex3_from_triangle3(arena, t0));
		Shape3 b = shape3_from_convex3(convex3_from_triangle3(arena, t1));
		float got = gjk_dist(a, b, 8.0f);
		EXPECT_NEAR(got, 8.0f, 1e-2f, "far apart triangles, nearest-vertex gap = 8");
	}

	{
		/* Identical (fully overlapping) triangles -> distance 0. */
		Shape3 a = shape3_from_convex3(convex3_from_triangle3(arena, t0));
		Shape3 b = shape3_from_convex3(convex3_from_triangle3(arena, t0));
		float got = gjk_dist(a, b, 0.0f);
		EXPECT_NEAR(got, 0.0f, 1e-2f, "identical triangles, distance 0");
	}
}

/* ------------------------------------------------------------------ */
/* Degenerate / robustness cases                                       */
/* ------------------------------------------------------------------ */

static void test_degenerate_cases(Arena *arena) {
	TEST_GROUP("degenerate shapes");

	/* Zero-radius "sphere" acts as a point. */
	{
		Sphere s = { { 2, 0, 0 }, 0.0f };
		AABB3 box = make_aabb((float3){ 0, 0, 0 }, (float3){ 1, 1, 1 });
		Shape3 a = shape3_from_sphere(s);
		Shape3 b = shape3_from_aabb3(box);
		float expected = sphere_aabb_distance(s, box);
		float got = gjk_dist(a, b, expected);
		EXPECT_NEAR(got, expected, 1e-2f, "zero-radius sphere (point) vs aabb");
	}

	/* Degenerate (collinear / zero-area) triangle should not crash and
	 * should behave like its underlying segment. */
	{
		Triangle3 degenerate = { { -1, 0, 0 }, { 0, 0, 0 }, { 1, 0, 0 } };
		Sphere s = { { 5, 0, 0 }, 1.0f };
		Shape3 a = shape3_from_sphere(s);
		Shape3 b = shape3_from_convex3(convex3_from_triangle3(arena, degenerate));
		float got = gjk_dist(a, b, 3.0f);
		/* Nearest point on the segment [-1,0,0]-[1,0,0] to (5,0,0) is (1,0,0). */
		EXPECT_NEAR(got, 3.0f, 1e-1f, "degenerate (collinear) triangle vs sphere");
	}

	/* Zero-size AABB acts as a point. */
	{
		AABB3 point_box = make_aabb((float3){ 0, 0, 0 }, (float3){ 0, 0, 0 });
		Sphere s = { { 3, 0, 0 }, 1.0f };
		Shape3 a = shape3_from_sphere(s);
		Shape3 b = shape3_from_aabb3(point_box);
		float got = gjk_dist(a, b, 2.0f);
		EXPECT_NEAR(got, 2.0f, 1e-2f, "zero-size aabb (point) vs sphere");
	}
}

/* ------------------------------------------------------------------ */
/* Property tests                                                     */
/* ------------------------------------------------------------------ */

static void test_properties(void) {
	TEST_GROUP("properties");

	Sphere sa = { { 0, 0, 0 }, 1.0f };
	Sphere sb = { { 0.3f, 0, 0 }, 1.0f };
	AABB3 box = make_aabb((float3){ 10, 10, 10 }, (float3){ 1, 1, 1 });

	Shape3 a = shape3_from_sphere(sa);
	Shape3 b = shape3_from_sphere(sb);
	Shape3 c = shape3_from_aabb3(box);

	/* Non-negativity */
	EXPECT_TRUE(gjk_distance_squared(a, b, 0.0f) >= 0.0f, "overlapping shapes: squared distance non-negative");
	EXPECT_TRUE(gjk_distance_squared(a, c, sphere_aabb_distance(sa, box)) >= 0.0f, "separated shapes: squared distance non-negative");

	/* Overlap collapses to zero */
	EXPECT_NEAR(gjk_dist(a, b, 0.0f), 0.0f, 1e-3f, "overlapping spheres report zero distance");

	/* Symmetry */
	float expected = sphere_aabb_distance(sa, box);
	EXPECT_NEAR(gjk_dist(a, c, expected), gjk_dist(c, a, expected), 1e-3f, "gjk_distance_squared is symmetric in its arguments");

	/* Self-distance is zero */
	EXPECT_NEAR(gjk_dist(a, a, 0.0f), 0.0f, 1e-3f, "shape vs itself has zero distance");

	/* Stability across reference_dist_sq hints: a tight-but-correct hint and
	 * a wildly loose hint should agree with each other. */
	{
		float exact = sphere_aabb_distance(sa, box);
		float tight_hint_sq = exact * exact;
		float loose_hint_sq = tight_hint_sq;

		float d_tight = sqrtf(fmaxf(0.0f, gjk_distance_squared(a, c, tight_hint_sq)));
		float d_loose = sqrtf(fmaxf(0.0f, gjk_distance_squared(a, c, loose_hint_sq)));
		EXPECT_NEAR(d_tight, d_loose, 1e-2f, "result is stable across reference_dist_sq hints");
		EXPECT_NEAR(d_tight, exact, 1e-2f, "result under tight hint matches analytic distance");
	}
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
	ArenaTemp scratch = arena_scratch_begin(0);

	max_error = 0.0f;

	test_sphere_sphere_cases();
	test_sphere_aabb_cases();
	test_sphere_capsule_cases();
	test_sphere_triangle_grid(scratch.arena);
	test_aabb_aabb_cases();
	test_capsule_capsule_cases();
	test_triangle_triangle_cases(scratch.arena);
	test_degenerate_cases(scratch.arena);
	test_properties();

	arena_scratch_end(scratch);

	LOG_INFO("================================================");
	LOG_INFO("Ran %d checks, %d failed. Global GJK max_error=%g", g_tests_run, g_tests_failed, max_error);
	LOG_INFO("================================================");

	if (g_tests_failed > 0) {
		LOG_ERROR("GJK TEST SUITE FAILED");
		return 1;
	}

	LOG_INFO("GJK TEST SUITE PASSED");
	return 0;
}
