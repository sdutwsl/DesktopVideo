"""Run with MURALOP_CONTROL_TEST pointing to the C++ test host; no desktop surfaces.

On Linux run inside dbus-run-session to test the real session-bus transport.
"""
import os
from pathlib import Path
import subprocess
import sys
import time

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gui"))
from backend import Backend


@pytest.mark.skipif(not os.environ.get("MURALOP_CONTROL_TEST"), reason="needs native control test host")
def test_real_named_pipe_unicode_commands_and_clean_shutdown():
    executable = os.environ["MURALOP_CONTROL_TEST"]
    process = subprocess.Popen([executable, "--serve"], creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
    backend = Backend(executable)
    try:
        for _ in range(40):
            try:
                state = backend.request("status")
                break
            except OSError:
                time.sleep(0.05)
        else:
            pytest.fail("pipe host failed to start")
        assert len(state["monitors"]) == 2
        state = backend.request("set", 'C:\\视频\\a,b "quoted".mp4', "screen-b")
        assert state["monitors"][1]["video"] == 'C:\\视频\\a,b "quoted".mp4'
        assert backend.request("volume", "29")["volume"] == 29
        assert backend.request("pause")["paused"] is True
        with pytest.raises(RuntimeError, match="Volume"):
            backend.request("volume", "101")
        backend.request("quit")
        assert process.wait(timeout=10) == 0
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
