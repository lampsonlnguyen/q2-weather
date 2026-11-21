#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Weather's pages as Q2 Pod asks for them: the host build serves Q2SPOT/1 on its socket under a
scratch WEATHER_ROOT holding a cached forecast, and each level's reply is checked row by row.

    python3 tests/test_pages.py build/host/q2-weather
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

STATE = """Q2W2
0|1|Westminster|51.500000|-0.140000|1|22|45|1|1795000000
""" + "".join(f"0|1|2026-11-{21 + d}|{24 - d}|{14 - d}|{10 * d}|{c}\n"
              for d, c in enumerate((1, 3, 61, 0, 80, 71, 95))) + """1|1|Ho Chi Minh City|10.820000|106.630000|1|31|78|3|1795000000
""" + "".join(f"1|1|2026-11-{21 + d}|32|25|{60 + d}|{c}\n"
              for d, c in enumerate((3, 80, 80, 95, 2, 61, 1)))


def ask(sock, *args, req=[0]):
    req[0] += 1
    esc = lambda v: v.replace("%", "%25").replace("\t", "%09").replace("\n", "%0A")
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.sendall(("Q2SPOT/1\n" + "\t".join([str(req[0])] + [esc(a) for a in args]) + "\n").encode())
    s.shutdown(socket.SHUT_WR)
    data = b""
    while chunk := s.recv(65536):
        data += chunk
    s.close()
    lines = data.decode().split("\n")
    assert lines[0] == "Q2SPOT/1", lines[0]
    head = lines[1].split("\t")
    assert head[0] == str(req[0]) and len(head) == 4, head
    unesc = lambda v: v.replace("%09", "\t").replace("%0A", "\n").replace("%0D", "\r").replace(
        "%25", "%")
    rows = [[unesc(f) for f in l.split("\t")] for l in lines[2:] if l]
    assert all(len(r) == 6 for r in rows), rows
    assert len(rows) <= 50
    return head[1], head[2], rows


def main(binary):
    root = tempfile.mkdtemp(prefix="q2-weather-")
    os.makedirs(f"{root}/mnt/mmc/.weather")
    os.makedirs(f"{root}/mnt/data")
    os.makedirs(f"{root}/tmp")
    open(f"{root}/mnt/mmc/.weather/state", "w").write(STATE)
    open(f"{root}/mnt/data/config.ini", "w").write("[SYSSET]\nWIFI=1\n")
    open(f"{root}/tmp/q2-addon.caps", "w").write("playlist progress pages art field fieldtext\n")
    sock = f"{root}/tmp/q2-weather.browse.sock"
    env = dict(os.environ, WEATHER_ROOT=root, WEATHER_NO_NET="1", TZ="UTC")
    app = subprocess.Popen([binary], env=env)
    try:
        for _ in range(100):
            if os.path.exists(sock):
                break
            time.sleep(0.05)
        time.sleep(0.3)  # the first update fails at once with no network

        st, msg, rows = ask(sock, "menu")
        assert st == "ok"
        assert [r[0] for r in rows] == ["search", "list", "list", "list"], rows
        assert rows[1][1:3] == ["location", "Westminster"], rows[1]
        assert rows[1][3] == "My Location · 22° Mostly clear", rows[1][3]
        assert rows[2][2] == "Ho Chi Minh City" and rows[2][3].startswith("Saved City"), rows[2]
        assert rows[3][1:3] == ["settings", "Settings"]

        st, msg, rows = ask(sock, "location", rows[1][4])
        assert st == "ok"
        assert rows[0][0] == "info" and rows[0][2] == "22° Mostly clear", rows[0]
        assert rows[0][3] == "H 24°  L 14° · Humidity 45%", rows[0][3]
        days = rows[1:8]
        assert [d[2] for d in days[:2]] == ["Today", "Tomorrow"], days
        assert days[2][2] == "Monday 23", days[2][2]
        assert days[2][3] == "Rain · 22° / 12° · Rain 20%", days[2][3]
        assert rows[8][:2] == ["action", "refresh"], rows[8]

        st, msg, rows = ask(sock, "settings")
        assert msg == "refresh"
        assert rows[0][1:4] == ["units", "Temperature", "Celsius"], rows[0]
        assert ask(sock, "units") == ("ok", "", [])
        st, msg, rows = ask(sock, "settings")
        assert rows[0][3] == "Fahrenheit"
        assert open(f"{root}/mnt/mmc/.weather/units").read() == "1"
        st, msg, rows = ask(sock, "menu")
        assert "72°" in rows[1][3], rows[1][3]

        st, msg, rows = ask(sock, "cities", "1")
        assert rows[0][:2] == ["field", "cities"] and rows[0][4] == "1", rows[0]

        assert ask(sock, "nonsense")[0] == "unsupported"
        print("Weather card app pages passed")
    finally:
        app.terminate()
        app.wait(timeout=5)


if __name__ == "__main__":
    main(sys.argv[1])
