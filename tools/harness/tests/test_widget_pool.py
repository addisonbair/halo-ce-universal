"""The real widget pool (widgets.c) over a fake world of objects that each carry a widget.

No game assets needed. The C side (widget_pool.c) holds the world and the cases; this takes the code
under test from the sources: widgets_new, widgets_delete, tag_group_to_widget_type and widget_type_definition_get,
with the pool's own size (MAXIMUM_WIDGETS_PER_MAP) and the widget types' tag groups.

Cases: the light volume group is found ("finds-the-type"); rifles past the pool get none, exactly its size do
("fills-and-refuses", the cause of invisible plasma bolts in big games: 64 slots for 128 players' rifles); deleting
frees slots for the next ("frees-and-reuses"); a widget whose making fails frees its slot
("failed-widget-frees-its-slot"). Negative controls: widgets.c with a deliberate fault must fail a case.

    python -m pytest -q tools/harness
"""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, constant, function, mutated, read, run  # noqa: E402

CASES = ["finds-the-type", "fills-and-refuses", "frees-and-reuses", "failed-widget-frees-its-slot"]

# a fault in widgets.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "keeps-a-failed-widgets-slot": (("datum_delete(widget_data, widget_index);", ";"), "widgets_new",
                                    "failed-widget-frees-its-slot"),
    "never-frees-on-delete": (("datum_delete(widget_data, widget_index);", ";"), "widgets_delete", "frees-and-reuses"),
}


def generated(fault=None, faulty_function=None):
    widgets = read("source/objects/widgets/widgets.c")
    groups = re.findall(r"^\s*'(\w{3}[\w!])',\s*$", widgets, re.M)
    assert len(groups) == 5, f"widget types not found in widgets.c: {groups}"
    config = "".join(f"#define GROUP_TAG_{index} '{group}'\n" for index, group in enumerate(groups))
    config += f"#define MAXIMUM_WIDGETS_PER_MAP {constant(widgets, 'MAXIMUM_WIDGETS_PER_MAP')}\n"
    text = ""
    for name in ("widget_type_definition_get", "tag_group_to_widget_type", "widgets_new", "widgets_delete"):
        code = function(widgets, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    passed, output = run(build("widget_pool", generated()), case)
    assert passed, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    passed, output = run(build("widget_pool", generated(fault, faulty_function)), case)
    assert not passed, f"widgets.c with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
