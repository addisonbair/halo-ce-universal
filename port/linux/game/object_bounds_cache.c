/*
OBJECT_BOUNDS_CACHE.C

The objects' bounding spheres, packed by absolute index, for collision's object
loop (collisions.c's collision_get_features_in_sphere). A collision query walks
every collideable object of every cluster its sphere touches and reads each
object's bounding sphere to pass over the far ones. In a crowded cluster
(network co-op's extra enemies: hundreds of Flood in one room, and a carrier
swarm's infection forms on top) that is hundreds of scattered object reads a
query, a query for every biped every tick. This array holds the same values,
written where the object's are (object_compute_node_matrices), so passing over
a far object reads one cache line.

An entry is used only for the object it was written for (its datum index) and
only since the game state was last replaced (a revert, a loaded core or game,
a new map put the objects back behind it); otherwise the query reads the
object as before. The test is the same point_in_sphere on the same values, so
the objects passed over, and the features gathered, are exactly the same.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"

struct object_bounds
{
	long object_index;
	long epoch;
	real_point3d center;
	real radius;
};

static struct object_bounds object_bounds[MAXIMUM_OBJECTS_PER_MAP];
static long object_bounds_epoch = 1;

void object_bounds_cache_update(
	long object_index,
	real_point3d const *center,
	real radius)
{
	struct object_bounds *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	bounds->object_index = object_index;
	bounds->epoch = object_bounds_epoch;
	bounds->center = *center;
	bounds->radius = radius;
}

void object_bounds_cache_invalidate(
	void)
{
	object_bounds_epoch++;
}

/* whether the object's bounding sphere is known to be out of the reach of
the point (collision's own test on the copy); FALSE when it is not known */
boolean object_bounds_cache_out_of_reach(
	long object_index,
	real_point3d const *point,
	real radius)
{
	struct object_bounds const *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	return bounds->object_index == object_index && bounds->epoch == object_bounds_epoch &&
		!point_in_sphere(point, &bounds->center, bounds->radius + radius);
}
