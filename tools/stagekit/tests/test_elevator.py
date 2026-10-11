"""scenarios/elevator.c: players on a platform 0.25 high; a second in, it rises 5 units in 2 seconds, and at 5
seconds comes down. One player on one machine; on two, a host's and a client's, each machine receiving 100 ms
late (#238: the host's copy of a client's player fell through it going up)."""

import pytest

TOP = 0.25
CUES = [(1, "device_set_position elevator 1"), (5, "device_set_position elevator 0")]
PLAY = dict(seconds=10, input="look:1", cues=CUES)
PLAY_NETWORKED = dict(seconds=10, cues=CUES, latency=100.0)


def rides(session, player):
    """on the platform throughout, alive: at the top in between, back at the bottom by the end, never under it"""
    heights = [z for _, _, z in session.places(player)]
    assert session.deaths(player) == 0
    assert len(heights) == len(session.seconds), heights
    assert abs(heights[0] - TOP) < 0.05, heights
    assert max(heights) > TOP + 4.95, heights
    assert abs(heights[-1] - TOP) < 0.05, heights
    assert min(heights) > TOP - 0.05, heights


def test_rides_it_up_and_down(play):
    rides(play("elevator"), 0)


@pytest.mark.parametrize("machine", [0, 1], ids=["host", "client"])
def test_rides_it_on_two_machines(play_networked, machine):
    """as the host has it, and the client's player as the client has it"""
    session = play_networked("elevator")
    assert "network: receiving 100 ms late" in session.client.log
    rides(session, session.player_on(machine))
    if machine:
        rides(session.client, session.client.player_on(machine))
