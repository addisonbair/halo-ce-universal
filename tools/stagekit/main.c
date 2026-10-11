/*
MAIN.C

StageKit (README.md): writes a scenario's map, in code.

usage: stagekit --list
       stagekit <scenario> <map file>
       stagekit ui|carousel <map file> (shell.c: the maps a networked scenario hosts through)
*/

/* ---------- headers */

#include "stagekit.h"

/* ---------- globals */

extern struct stagekit_scenario const scenario_walk;
extern struct stagekit_scenario const scenario_obstacle;
extern struct stagekit_scenario const scenario_elevator;
extern struct stagekit_scenario const shell_ui;
extern struct stagekit_scenario const shell_carousel;

static struct stagekit_scenario const *const scenarios[] = { &scenario_walk, &scenario_obstacle, &scenario_elevator };
/* (not scenarios: the maps the game's shell plays from, shell.c) */
static struct stagekit_scenario const *const shell_maps[] = { &shell_ui, &shell_carousel };

/* ---------- public code */

int main(
	int argc,
	char **argv)
{
	struct stage stage = { 0 };
	struct stagekit_scenario const *scenario = NULL;
	long tag_count;
	unsigned long index;

	if (argc == 2 && !strcmp(argv[1], "--list"))
	{
		for (index = 0; index < NUMBEROF(scenarios); index++)
			printf("%-12s %s\n", scenarios[index]->name, scenarios[index]->description);
		return 0;
	}
	for (index = 0; argc == 3 && index < NUMBEROF(scenarios) + NUMBEROF(shell_maps); index++)
	{
		struct stagekit_scenario const *candidate = index < NUMBEROF(scenarios) ? scenarios[index] :
			shell_maps[index - NUMBEROF(scenarios)];
		if (!strcmp(candidate->name, argv[1]))
			scenario = candidate;
	}
	if (!scenario)
	{
		fprintf(stderr, "usage: stagekit --list\n       stagekit <scenario> <map file>\n"
			"       stagekit ui|carousel <map file>\n");
		return 2;
	}
	stage_build(&stage, scenario);
	{
		/* (an Xbox cache is named as its scenario's last part) */
		char const *name = "void";
		char const *at;

		for (at = scenario->xbox_scenario; at && *at; at++)
		{
			if (*at == '\\')
				name = at + 1;
		}
		cache_write(argv[2], name, scenario->type, &tag_count);
	}
	printf("stagekit: %s: %s, %ld tags\n", scenario->name, argv[2], tag_count);
	return 0;
}
