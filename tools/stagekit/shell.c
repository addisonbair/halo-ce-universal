/*
SHELL.C

The maps the game's shell plays from, Xbox caches as the game's own (cache.c),
so that a networked scenario hosts and joins its games with no game data:

- ui: the main menu's (maps/ui.map). The port's menus (port/assets/menus) are
  its own, built onto it; it has the tags they name by path, StageKit's own:
  the fonts (whose text the port draws itself), silent sounds, and blank
  bitmaps of as many frames as they draw;
- carousel: the multiplayer map a hosted game opens before its own is set
  (the engine's default, game_engine_playlist_next), there and never played.
*/

/* ---------- headers */

#include "stagekit.h"

/* ---------- private code */

static void ui_build(
	struct stage *stage)
{
	static char const *const fonts[] = { "ui\\large_ui", "ui\\small_ui" };
	static char const *const sounds[] = { "sound\\sfx\\ui\\forward", "sound\\sfx\\ui\\back" };
	static struct { char const *name; long frames; } const bitmaps[] = {
		{ "ui\\shell\\bitmaps\\sp_levels", 1 },
		{ "ui\\shell\\bitmaps\\mp_map_grafix", 1 },
		{ "ui\\shell\\bitmaps\\colors_sm", 1 },
		{ "ui\\shell\\main_menu\\halo_logo", 1 },
		{ "ui\\shell\\main_menu\\difficulty_select\\difficulty_options", 1 },
		{ "ui\\shell\\main_menu\\difficulty_select\\difficulty_options_small", 1 },
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\mp_options", 4 },
		{ "ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\playlist_edit\\gametype_options", 5 },
		{ "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\profile_options", 4 },
		{ "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\controller_edit\\config_controller", 9 },
		{ "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\player_color_marine_large", 1 },
	};
	static unsigned long const clear[9] = { 0 };
	unsigned long index;
	void *data;

	struct scenario_cutscene_camera_point *camera;

	/* (the shell looks through the scenario's first camera point: above the
	floor, level, the engine's field of view) */
	camera = cache_block(&stage->scenario->cutscene_camera_points, 1, sizeof(*camera));
	camera->position.z = 2.f;
	camera->field_of_view = DEGREES_TO_RADIANS(70.f);
	for (index = 0; index < NUMBEROF(fonts); index++)
		cache_tag_new(FOURCC('f', 'o', 'n', 't'), NONE, NONE, fonts[index], sizeof(struct font_header), &data);
	for (index = 0; index < NUMBEROF(sounds); index++)
		cache_tag_new(FOURCC('s', 'n', 'd', '!'), NONE, NONE, sounds[index], sizeof(struct sound_definition), &data);
	for (index = 0; index < NUMBEROF(bitmaps); index++)
		prop_bitmap(bitmaps[index].name, BITMAP_TYPE_2D, bitmaps[index].frames, clear);
}

static void carousel_build(
	struct stage *stage)
{
	(void)stage;
}

/* ---------- public code */

struct stagekit_scenario const shell_ui = {
	"ui", "the main menu's map", ui_build, "levels\\ui\\ui", _scenario_type_main_menu };

struct stagekit_scenario const shell_carousel = {
	"carousel", "the map a hosted game opens before its own", carousel_build,
	"levels\\test\\carousel\\carousel", _scenario_type_multiplayer };
