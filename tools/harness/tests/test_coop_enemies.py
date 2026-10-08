"""Network co-op's extra enemies: PER PLAYER up to its cap, STATIC MULTIPLIER as set, within the actors left."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["per-player", "multiplier", "none", "actor-room"]

# a fault in coop_enemies_extra_count, and the case that must catch it
NEGATIVE_CONTROLS = {
    "uncapped": (("extra = MIN(extra, (long)count * (COOP_ENEMIES_MAXIMUM_GROWTH - 1));", ";"), "per-player"),
    "ignores-the-actors-in-use": ((" - actor_data->actual_count;", ";"), "actor-room"),
}


def generated(fault=None):
    source = read("port/linux/game/coop_enemies.c")
    config = "".join(f"#define {name} {constant(source, name)}\n"
                     for name in ("COOP_ENEMIES_MAXIMUM_GROWTH", "COOP_ENEMIES_LEVEL_ACTORS", "COOP_ENEMIES_MAXIMUM_MULTIPLIER"))
    config += f"#define MAXIMUM_ACTORS {constant(read('source/ai/actors.h'), 'MAXIMUM_ACTORS')}\n"
    code = function(source, "coop_enemies_extra_count")
    return (("config.inc", config), ("under_test.inc", mutated(code, *fault) if fault else code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("coop_enemies", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("coop_enemies", generated(fault)), case)
    assert status == CHECK_FAILED, f"coop_enemies.c with a fault ({control}) passed '{case}': the test cannot see it"
