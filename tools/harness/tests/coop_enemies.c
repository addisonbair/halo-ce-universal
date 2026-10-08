/*
COOP_ENEMIES.C (test)

coop_enemies.c's extra enemies a squad gets (coop_enemies_extra_count) over a
fake game of any number of players: PER PLAYER grows a squad by a percentage
of itself for each player past the first, to COOP_ENEMIES_MAXIMUM_GROWTH
times its size; STATIC MULTIPLIER makes it that many times as large; neither
outgrows the actors left. test_coop_enemies.py takes the code and constants.
*/

#include "harness.h"
#include "config.inc"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, low, high) MIN(MAX(x, low), high)

enum { _coop_enemies_none, _coop_enemies_per_player, _coop_enemies_multiplier };
enum { _game_team_player = 1, _game_team_covenant = 3 };
static struct { short mode, percent, multiplier; } coop_enemies;
static short player_total;
static struct { long actual_count; } actors, *actor_data = &actors;
struct encounter_datum { short team_index; };
static struct encounter_datum the_encounter;
#define coop_enemies_on() (coop_enemies.mode != _coop_enemies_none)
#define coop_enemies_player_count() player_total
#define encounter_get(encounter_index) (&the_encounter)
#define game_team_is_enemy(team, other) ((team) != (other))

#include "under_test.inc"

static void game(short mode, short amount, short player_count, long actors_in_use)
{
	coop_enemies.mode = mode;
	coop_enemies.percent = mode == _coop_enemies_per_player ? amount : 0;
	coop_enemies.multiplier = mode == _coop_enemies_multiplier ? amount : 0;
	player_total = player_count;
	actors.actual_count = actors_in_use;
	the_encounter.team_index = _game_team_covenant;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	long count, percent, player_count, extra, uncapped;

	/* as much more as the percentage asks, up to the cap whatever the players */
	CASE("per-player")
	{
		for (percent = 25; percent <= 200; percent += 25)
			for (count = 1; count <= 12; count++)
				for (player_count = 1; player_count <= 128; player_count++)
				{
					game(_coop_enemies_per_player, (short)percent, (short)player_count, 0);
					uncapped = (count * percent * (player_count - 1) + 50) / 100;
					extra = coop_enemies_extra_count(0, (short)count);
					CHECK(extra == MIN(uncapped, count * (COOP_ENEMIES_MAXIMUM_GROWTH - 1)),
						"%ld%%, %ld players, a squad of %ld: %ld more", percent, player_count, count, extra);
				}
		return 0;
	}
	/* the multiplier as it is, past the cap */
	CASE("multiplier")
	{
		for (percent = 2; percent <= COOP_ENEMIES_MAXIMUM_MULTIPLIER; percent++)
		{
			game(_coop_enemies_multiplier, (short)percent, 2, 0);
			extra = coop_enemies_extra_count(0, 6);
			CHECK(extra == 6 * (percent - 1), "%ld times: %ld more for a squad of 6", percent, extra);
		}
		return 0;
	}
	/* none alone, for the players' allies, for an empty squad, or with extra enemies off */
	CASE("none")
	{
		game(_coop_enemies_per_player, 100, 1, 0);
		CHECK(!coop_enemies_extra_count(0, 6), "one player got extra enemies");
		game(_coop_enemies_per_player, 100, 4, 0);
		the_encounter.team_index = _game_team_player;
		CHECK(!coop_enemies_extra_count(0, 6), "the players' allies grew");
		game(_coop_enemies_per_player, 100, 4, 0);
		CHECK(!coop_enemies_extra_count(0, 0), "an empty squad grew");
		game(_coop_enemies_none, 0, 4, 0);
		CHECK(!coop_enemies_extra_count(0, 6), "extra enemies off, and some came");
		return 0;
	}
	/* no more than the actors left above the level's own */
	CASE("actor-room")
	{
		long room = MAXIMUM_ACTORS - COOP_ENEMIES_LEVEL_ACTORS;

		game(_coop_enemies_multiplier, COOP_ENEMIES_MAXIMUM_MULTIPLIER, 2, room - 10);
		CHECK(coop_enemies_extra_count(0, 12) == 10, "%d more with 10 actors left", coop_enemies_extra_count(0, 12));
		game(_coop_enemies_multiplier, COOP_ENEMIES_MAXIMUM_MULTIPLIER, 2, room + 10);
		CHECK(coop_enemies_extra_count(0, 12) == 0, "%d more with none left", coop_enemies_extra_count(0, 12));
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
