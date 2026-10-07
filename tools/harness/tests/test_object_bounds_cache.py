"""Collision's real object loop over a fake crowded world, with and without the object bounds cache.

No game assets needed. The C side (object_bounds_cache.c) holds the world and the cases; this takes the
code under test from the sources: collision_get_features_in_sphere and object_get_features_in_sphere (collisions.c),
the point_in_sphere they test with (real_math.h) and the cache (object_bounds_cache.c).

Cases: the cache gathers exactly the features the objects themselves give, in order ("identical"); still so after
objects move with the cache told ("moving"); the comparison sees a stale cache ("detects-stale"); an invalidated cache
reads the objects again ("invalidated"). Negative controls: the cache with a deliberate fault must fail a case.

    python -m pytest -q tools/harness                          # every harness test
    python tools/harness/tests/test_object_bounds_cache.py     # this one, then how fast the cache is
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, inline, mutated, read, run  # noqa: E402

CASES = ["identical", "moving", "detects-stale", "invalidated"]

# a fault in the cache, and the case that must catch it
NEGATIVE_CONTROLS = {
    "ignores-invalidation": (("bounds->epoch == object_bounds_epoch &&", ""), "invalidated"),
    "drops-the-query-radius": (("bounds->radius + radius", "bounds->radius"), "identical"),
}


def under_test(fault=None):
    real_math = read("source/math/real_math.h")
    collisions = read("source/physics/collisions.c")
    cache = read("port/linux/game/object_bounds_cache.c")
    cache = cache[cache.rindex("#include"):].split("\n", 1)[1]
    if fault:
        cache = mutated(cache, *fault)
    text = enum_with(read("source/physics/collisions.h"), "_collision_test_structure_bit") + "\n"
    for helper in ("vector_from_points3d", "magnitude_squared3d", "distance_squared3d", "point_in_sphere"):
        text += inline(real_math, helper) + "\n"
    text += cache + "\n"
    text += function(collisions, "object_get_features_in_sphere") + "\n"
    text += function(collisions, "collision_get_features_in_sphere") + "\n"
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    passed, output = run(build("object_bounds_cache", under_test()), case)
    assert passed, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    passed, output = run(build("object_bounds_cache", under_test(fault)), case)
    assert not passed, f"the cache with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    status = pytest.main(["-q", __file__])
    print(run(build("object_bounds_cache", under_test()), "benchmark")[1])
    sys.exit(status)
