"""main.c's death and respawn timers, co-op respawn and the dead camera keep game time at any frame rate."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["frame-rates", "clock-and-lifecycle", "co-op-blocked", "dead-camera"]

# a fault in main.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "ignores-a-clock-reset": (("*timer = 0;", ";"), "main_death_timer_expired", "clock-and-lifecycle"),
    "counts-paused-time": (("!game_time_get_paused()))", "TRUE))"), "main_lost_map_private", "clock-and-lifecycle"),
    "respawns-in-cinematics": ((" && !cinematic_in_progress()", ""), "main_respawn_private", "clock-and-lifecycle"),
}


def structure(source, name):
    start = source.index("struct " + name + "\n{")
    return source[start:source.index("\n};", start) + 4] + "\n"


def under_test(fault=None, faulty_function=None):
    main = read("source/main/main.c")
    camera = read("source/camera/dead_camera.c")
    text = structure(camera, "camera_control") + structure(camera, "dead_camera_command")
    text += structure(read("source/camera/dead_camera.h"), "dead_camera")
    # (each co-op respawn counted as an attempt)
    text += "#define players_respawn_coop players_respawn_coop_real\n"
    text += function(read("source/game/players.c"), "players_respawn_coop")
    text += "\n#undef players_respawn_coop\n"
    text += "static boolean players_respawn_coop(void) { attempts++; return players_respawn_coop_real(); }\n"
    for name in ("main_lost_map", "main_respawn", "main_reset_map", "main_revert_map", "main_death_timer_expired",
                 "main_lost_map_private", "main_respawn_private"):
        code = function(main, name)
        text += (mutated(code, *fault) if fault and name == faulty_function else code) + "\n"
    for name in ("player_has_allies", "player_get_next_player_with_a_unit", "dead_camera_update"):
        text += function(camera, name) + "\n"
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("death_timing", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("death_timing", under_test(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"main.c with a fault ({control}) passed '{case}': the test cannot see it"
