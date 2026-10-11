/*
ELEVATOR.C

A platform 6 by 6 and 0.25 high on the floor, with two players' starts on it,
that rises 5 units in 2 seconds when it is set to, and comes down when set
back (tests/test_elevator.py: whoever stands on it rides it, on one machine
and on a client of another's game, #238).

Going up 2.5 units a second, it rises 0.5 in the 6 ticks of a 200 ms round
trip, twice its thickness: where a host put a client's player as the client
last said it was, as it did before #238, that is under the floor.
*/

#include "../stagekit.h"

static void elevator_build(
	struct stage *stage)
{
	real_rectangle3d platform = { { -3.f, 3.f, -3.f, 3.f, 0.f, 0.25f } };
	real_point3d position = { { 0.f, 0.f, 0.f } };
	real_point3d starts[2] = { { { -1.f, 0.f, 0.25f } }, { { 1.f, 0.f, 0.25f } } };

	prop_elevator(stage, "elevator", &platform, &position, 5.f, 2.f);
	player_start(stage, &starts[0]);
	player_start(stage, &starts[1]);
}

struct stagekit_scenario const scenario_elevator = {
	"elevator", "a platform the players ride up and down", elevator_build };
