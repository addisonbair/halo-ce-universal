/*
OBJECT_BOUNDS_CACHE.C (test)

Collision's real object loop (collisions.c's collision_get_features_in_sphere and object_get_features_in_sphere) over
a fake world of clusters and 600 objects, most packed into one cluster as a carrier swarm is (bipeds, scenery,
vehicles; some invisible, without collisions, dead, or carrying a child), with and without the object bounds cache
(port/linux/game/object_bounds_cache.c). Every feature gathered is recorded in order. The real code is in
under_test.inc (tools/test_object_bounds_cache.py takes it from the sources).
*/

#include "harness.h"


enum { _object_type_biped, _object_type_vehicle, _object_type_weapon, _object_type_equipment, _object_type_garbage,
	_object_type_projectile, _object_type_scenery, _object_type_machine, _object_type_control };
#define _object_mask_vehicle FLAG(_object_type_vehicle)
enum { _object_invisible_bit = 0, _object_no_collisions_bit = 1 };
enum { _object_dead_bit = 0 };
enum { _biped_movement_passes_through_bipeds_bit = 0 };

struct object_datum
{
	struct
	{
		unsigned long flags;
		unsigned long damage_flags;
		short type;
		long parent_object_index, next_object_index, first_child_object_index;
		real_point3d bounding_sphere_center;
		real bounding_sphere_radius;
	} object;
	struct { long player_index; short parent_seat_index; } unit;
	struct { unsigned long flags; } biped;
};
#define biped_datum object_datum

enum { MAXIMUM_OBJECTS_PER_MAP = 1024, WORLD_CLUSTERS = 16, MAXIMUM_CLUSTER_OBJECTS = 1024 };
static struct object_datum objects[MAXIMUM_OBJECTS_PER_MAP];
static long object_indices[MAXIMUM_OBJECTS_PER_MAP];
static long object_count;
static struct object_datum *object_get(long object_index)
{
	return &objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];
}

/* clusters: a 4x4 grid of 10-unit squares; an object is in every cluster its bounding sphere touches */
static long cluster_objects[WORLD_CLUSTERS][MAXIMUM_CLUSTER_OBJECTS];
static long cluster_object_count[WORLD_CLUSTERS];
static int cluster_of(real x, real y)
{
	int cx = (int)(x / 10.0f), cy = (int)(y / 10.0f);

	cx = cx < 0 ? 0 : cx > 3 ? 3 : cx;
	cy = cy < 0 ? 0 : cy > 3 ? 3 : cy;
	return cy * 4 + cx;
}
static long cluster_get_first_collideable_object(long *reference_index, short cluster_index)
{
	*reference_index = cluster_index * MAXIMUM_CLUSTER_OBJECTS;
	return cluster_object_count[cluster_index] ? cluster_objects[cluster_index][0] : NONE;
}
static long cluster_get_next_collideable_object(long *reference_index)
{
	long cluster = *reference_index / MAXIMUM_CLUSTER_OBJECTS, place = *reference_index % MAXIMUM_CLUSTER_OBJECTS + 1;

	*reference_index = cluster * MAXIMUM_CLUSTER_OBJECTS + place;
	return place < cluster_object_count[cluster] ? cluster_objects[cluster][place] : NONE;
}

/* the BSP: every cluster the sphere's square bounds touch is a leaf of the result */
struct structure_leaf { short cluster_index; };
static struct structure_leaf leaves[WORLD_CLUSTERS];
struct tag_block { int count; };
struct structure_bsp { struct tag_block leaves; };
struct collision_bsp { int unused; };
static struct structure_bsp the_structure_bsp;
static struct collision_bsp the_collision_bsp;
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&leaves[index])
static long collision_cluster_index_from_leaf(long leaf_index) { return leaves[leaf_index].cluster_index; }
static struct structure_bsp const *global_structure_bsp_get(void) { return &the_structure_bsp; }
static struct collision_bsp const *global_collision_bsp_get(void) { return &the_collision_bsp; }
struct collision_bsp_test_sphere_result { int leaf_count; long leaf_indices[WORLD_CLUSTERS]; };
enum { MAXIMUM_BREAKABLE_SURFACES_PER_MAP = 1 };
static void *breakable_surface_flags_get(void) { return NULL; }
static boolean collision_bsp_test_sphere(struct collision_bsp const *bsp, int maximum, void *breakable,
	real_point3d const *center, real radius, struct collision_bsp_test_sphere_result *result)
{
	int cx, cy;

	result->leaf_count = 0;
	for (cy = 0; cy < 4; cy++)
		for (cx = 0; cx < 4; cx++)
			if (center->x + radius >= cx * 10.0f && center->x - radius <= (cx + 1) * 10.0f &&
				center->y + radius >= cy * 10.0f && center->y - radius <= (cy + 1) * 10.0f)
				result->leaf_indices[result->leaf_count++] = cy * 4 + cx;
	return result->leaf_count > 0;
}

/* each query visits a cluster and an object once */
static int cluster_marks[WORLD_CLUSTERS], object_marks[MAXIMUM_OBJECTS_PER_MAP], marker;
static void structure_cluster_marker_begin(void) { marker++; }
static void structure_cluster_marker_end(void) {}
static boolean structure_cluster_mark(short cluster)
{
	if (cluster_marks[cluster] == marker) return FALSE;
	cluster_marks[cluster] = marker;
	return TRUE;
}
static void object_marker_begin(void) {}
static void object_marker_end(void) {}
static boolean object_mark_function(long object_index)
{
	int *mark = &object_marks[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	if (*mark == marker) return FALSE;
	*mark = marker;
	return TRUE;
}

/* the features gathered, in order: what each object gave, and to which query */
enum { _collision_feature_sphere, _collision_feature_cylinder, _collision_feature_prism, NUMBER_OF_COLLISION_FEATURE_TYPES };
struct collision_feature_list { short count[NUMBER_OF_COLLISION_FEATURE_TYPES]; };
static unsigned long feature_hash, feature_total;
static void feature(long object_index, int kind, real a, real b)
{
	unsigned long values[4];
	int index;

	values[0] = (unsigned long)object_index;
	values[1] = (unsigned long)kind;
	memcpy(&values[2], &a, sizeof(real));
	memcpy(&values[3], &b, sizeof(real));
	for (index = 0; index < 4; index++)
		feature_hash = (feature_hash ^ values[index]) * 16777619UL;
	feature_total++;
}
static void collision_features_new(struct collision_feature_list *features) { memset(features, 0, sizeof(*features)); }
static void collision_features_from_point(real_point3d const *point, real height, real width, long object_index,
	long b, int c, byte d, long e, struct collision_feature_list *features)
{
	feature(object_index, 0, point->x + point->y + point->z, height + width);
	features->count[_collision_feature_sphere]++;
}
static void biped_get_physics_pill(long object_index, real_point3d *base, real *height, real *width)
{
	*base = object_get(object_index)->object.bounding_sphere_center;
	*height = 0.4f;
	*width = 0.2f;
}
struct collision_model_instance { long object_index; };
struct physics_instance { long object_index; };
static boolean collision_model_instance_new(struct collision_model_instance *instance, long object_index)
{
	instance->object_index = object_index;
	return TRUE;
}
static void collision_model_get_features_in_sphere(struct collision_model_instance const *instance,
	real_point3d const *center, real radius, real height, real width, struct collision_feature_list *features)
{
	feature(instance->object_index, 1, radius, height + width);
	features->count[_collision_feature_cylinder]++;
}
static boolean physics_instance_new(struct physics_instance *instance, long object_index)
{
	instance->object_index = object_index;
	return TRUE;
}
static void physics_get_features_in_sphere(struct physics_instance const *instance,
	real_point3d const *center, real radius, real height, real width, struct collision_feature_list *features)
{
	feature(instance->object_index, 2, radius, height + width);
	features->count[_collision_feature_prism]++;
}
static void collision_bsp_get_features_in_sphere(struct collision_bsp const *bsp, void const *result, void *x,
	real height, real width, long none, struct collision_feature_list *features) {}

/* collision's statistics */
enum { _collision_function_vector_bounds_object };
static struct { struct { long long QuadPart; } features; } collision_usage_times;
static void collision_log_usage(int function) {}
static void collision_log_start_time(void *time) {}
static void collision_log_end_time(int function, long long start) {}
static boolean debug_collision_skip_objects = FALSE;

#include "under_test.inc"

static unsigned long seed = 12345;
static real random_real(real low, real high)
{
	seed = seed * 1103515245UL + 12345UL;
	return low + (high - low) * (real)((seed >> 8) & 0xFFFF) / 65535.0f;
}

static void place(long object_index, real_point3d const *center)
{
	struct object_datum *object = object_get(object_index);
	int cx, cy;

	object->object.bounding_sphere_center = *center;
	if (object->object.parent_object_index != NONE)
		return;
	for (cy = 0; cy < 4; cy++)
		for (cx = 0; cx < 4; cx++)
		{
			int cluster = cy * 4 + cx;
			real r = object->object.bounding_sphere_radius;

			if (center->x + r >= cx * 10.0f && center->x - r <= (cx + 1) * 10.0f &&
				center->y + r >= cy * 10.0f && center->y - r <= (cy + 1) * 10.0f)
				cluster_objects[cluster][cluster_object_count[cluster]++] = object_index;
		}
}

static void rebuild_clusters(void)
{
	long index;

	memset(cluster_object_count, 0, sizeof(cluster_object_count));
	for (index = 0; index < object_count; index++)
	{
		long object_index = object_indices[index];

		place(object_index, &object_get(object_index)->object.bounding_sphere_center);
	}
}

/* (as object_compute_node_matrices does) */
static void tell_cache(long object_index)
{
	struct object_datum *object = object_get(object_index);

	object_bounds_cache_update(object_index, &object->object.bounding_sphere_center, object->object.bounding_sphere_radius);
}

static void build_world(void)
{
	long index;

	for (index = 0; index < 16; index++)
		leaves[index].cluster_index = (short)index;
	/* a crowd: most objects packed into one cluster, as a carrier swarm is */
	for (index = 0; index < 600; index++)
	{
		struct object_datum *object = &objects[index];
		long object_index = (long)((index * 7 + 1) & 0x7FFF) << 16 | index;
		real_point3d center;
		int kind = index % 10;

		memset(object, 0, sizeof(*object));
		object->object.type = kind < 7 ? _object_type_biped : kind < 9 ? _object_type_scenery : _object_type_vehicle;
		object->object.parent_object_index = NONE;
		object->object.next_object_index = NONE;
		object->object.first_child_object_index = NONE;
		object->object.bounding_sphere_radius = random_real(0.2f, 1.5f);
		object->unit.player_index = index % 50 == 0 ? 1 : NONE;
		object->unit.parent_seat_index = NONE;
		if (index % 31 == 0) object->object.flags |= FLAG(_object_invisible_bit);
		if (index % 37 == 0) object->object.flags |= FLAG(_object_no_collisions_bit);
		if (index % 41 == 0) object->object.damage_flags |= FLAG(_object_dead_bit);
		if (index % 23 == 0) object->biped.flags |= FLAG(_biped_movement_passes_through_bipeds_bit);
		if (index < 450)
		{
			center.x = random_real(12.0f, 18.0f);
			center.y = random_real(12.0f, 18.0f);
		}
		else
		{
			center.x = random_real(0.0f, 40.0f);
			center.y = random_real(0.0f, 40.0f);
		}
		center.z = random_real(0.0f, 2.0f);
		object->object.bounding_sphere_center = center;
		object_indices[object_count++] = object_index;
	}
	/* children: some objects carry one (a weapon), reached only through their parent */
	for (index = 0; index < 600; index += 13)
	{
		long parent = object_indices[index], child = object_indices[(index + 300) % 600];
		struct object_datum *c = object_get(child);

		if (object_get(parent)->object.first_child_object_index != NONE || c->object.parent_object_index != NONE ||
			c->object.first_child_object_index != NONE || parent == child)
			continue;
		c->object.parent_object_index = parent;
		object_get(parent)->object.first_child_object_index = child;
	}
	rebuild_clusters();
}

enum { QUERIES = 4000 };
static real_point3d query_points[QUERIES];
static real query_radii[QUERIES];
static unsigned long query_flags[QUERIES];

static void build_queries(void)
{
	int index;

	for (index = 0; index < QUERIES; index++)
	{
		query_points[index].x = index % 3 ? random_real(11.0f, 19.0f) : random_real(0.0f, 40.0f);
		query_points[index].y = index % 3 ? random_real(11.0f, 19.0f) : random_real(0.0f, 40.0f);
		query_points[index].z = random_real(0.0f, 2.0f);
		query_radii[index] = random_real(0.3f, 2.5f);
		query_flags[index] = FLAG(_collision_test_objects_bit) | (index % 2 ? _collision_test_objects_all_types_flags : 0) |
			(index % 5 == 0 ? FLAG(_collision_test_skip_passthrough_bipeds_bit) : 0) |
			(index % 7 == 0 ? FLAG(_collision_test_skip_player_bipeds_bit) : 0) |
			(index % 11 == 0 ? FLAG(_collision_test_use_vehicle_physics_bit) : 0);
	}
}

static unsigned long run_queries(double *seconds)
{
	struct collision_feature_list features;
	struct timespec start, end;
	int index, round;

	feature_hash = 2166136261UL;
	feature_total = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (round = 0; round < 10; round++)
		for (index = 0; index < QUERIES; index++)
			collision_get_features_in_sphere(query_flags[index], &query_points[index], query_radii[index], 0.4f, 0.2f,
				object_indices[index % object_count], &features);
	clock_gettime(CLOCK_MONOTONIC, &end);
	*seconds = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
	return feature_hash;
}

static void move_some(int with_cache)
{
	long index;

	for (index = 0; index < object_count; index += 3)
	{
		struct object_datum *object = object_get(object_indices[index]);

		object->object.bounding_sphere_center.x += random_real(-1.5f, 1.5f);
		object->object.bounding_sphere_center.y += random_real(-1.5f, 1.5f);
		if (with_cache)
			tell_cache(object_indices[index]);
	}
	rebuild_clusters();
}


static void tell_cache_all(void)
{
	long index;

	for (index = 0; index < object_count; index++)
		tell_cache(object_indices[index]);
}

/* the features every query gathers from the objects themselves: the cache emptied directly (not through its own
code, which may be the fault), so no entry is used, as before the cache */
static unsigned long features_from_objects(double *seconds)
{
	long index;

	for (index = 0; index < MAXIMUM_OBJECTS_PER_MAP; index++)
		object_bounds[index].object_index = NONE;
	return run_queries(seconds);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	unsigned long without, with;
	double without_seconds, with_seconds;

	build_world();
	build_queries();

	/* the cache gathers exactly the features the objects themselves give, in the same order */
	CASE("identical")
	{
		without = features_from_objects(&without_seconds);
		CHECK(feature_total > 1000, "the queries gathered %lu features; the fake world is too sparse to compare", feature_total);
		tell_cache_all();
		with = run_queries(&with_seconds);
		CHECK(with == without, "features differ with the cache (%08lx, without %08lx)", with, without);
		printf("%lu features identical\n", feature_total);
		return 0;
	}
	/* objects move, the cache told as they do (object_compute_node_matrices): still identical */
	CASE("moving")
	{
		tell_cache_all();
		move_some(TRUE);
		move_some(TRUE);
		with = run_queries(&with_seconds);
		without = features_from_objects(&without_seconds);
		CHECK(with == without, "features differ after objects moved (%08lx, without %08lx)", with, without);
		return 0;
	}
	/* the comparison is sensitive: objects moved behind the cache's back must change the features */
	CASE("detects-stale")
	{
		tell_cache_all();
		move_some(FALSE);
		with = run_queries(&with_seconds);
		without = features_from_objects(&without_seconds);
		CHECK(with != without, "features did not change although the cache was stale: the comparison sees nothing");
		return 0;
	}
	/* a replaced game state invalidates the cache: the objects are read again, and the features are right */
	CASE("invalidated")
	{
		tell_cache_all();
		move_some(FALSE);
		object_bounds_cache_invalidate();
		with = run_queries(&with_seconds);
		without = features_from_objects(&without_seconds);
		CHECK(with == without, "features differ after the cache was invalidated (%08lx, without %08lx)", with, without);
		return 0;
	}
	/* not a check: how long the queries take without and with the cache */
	CASE("benchmark")
	{
		without = features_from_objects(&without_seconds);
		tell_cache_all();
		with = run_queries(&with_seconds);
		printf("%.1f ms without, %.1f ms with (%.2fx)\n", 1000.0 * without_seconds, 1000.0 * with_seconds, without_seconds / with_seconds);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
