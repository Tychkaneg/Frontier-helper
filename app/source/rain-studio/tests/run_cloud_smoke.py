"""End-to-end smoke test of the real app against our synthetic Rain process.

Requires make build fixtures, the existing lab's Wine prefix, Xvfb :100, and
the user's verified client.dll copied into the PRIVATE test directory.
No real game process or game server is used by this test.
"""
from pathlib import Path
import subprocess
import time
import json
import os

repo = Path(__file__).resolve().parents[2]
lab = repo.parent / "frontier-lab"
work = lab / "orbit-rain-studio-test"
setup_name = "Тест камеры " + str(time.time_ns())
base = ["docker", "exec", "-e", "WINEPREFIX=/lab/mezelounge-runtime/prefix-stable-clean",
        "-e", "DISPLAY=:100", "-e", "WINEDEBUG=-all", "-e", "WINEDLLOVERRIDES=d3dx9_43=n",
        "-e", "LD_PRELOAD=/lab/mezelounge-runtime/wine-ldt-shim-stable.so",
        "-w", "/lab/orbit-rain-studio-test", "frontier-meze-runtime",
        "/lab/mezelounge-runtime/wine-11.0-amd64-wow64/bin/wine"]

def driver(*args):
    result = subprocess.run(base + ["StudioDriver.exe", *args], capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, (args, result.stdout, result.stderr)
    return result.stdout

def launch(exe):
    command = base.copy();command.insert(2,"-d")
    subprocess.run(command + [exe], check=True, capture_output=True)

def summary(name):
    driver("summary", name + ".txt")
    s = (work / (name + ".txt")).read_text()
    fields = dict(line.split(": ",1) for line in s.splitlines() if ": " in line)
    return fields

def fixture():
    driver("fixture-state")
    return json.loads((work / "fixture-state.json").read_text())

def command(s):
    # Wine's Linux argv conversion depends on the container locale. Feed exact
    # UTF-8 through a file, then send Unicode text to the real Win32 EDIT control.
    (work / "command-input.txt").write_text(s, encoding="utf-8")
    driver("command-file", "command-input.txt")

verified = work / "client.dll.verified-fixture"
original = work / "client.dll"
driver("cleanup") # recover only our test windows after an interrupted test
assert original.exists() and not verified.exists()
os.replace(original, verified)
original.write_bytes(b"Intentional wrong hash, synthetic fixture only.")
try:
    launch("client.exe");time.sleep(.3);launch("FrontierOrbitStudio.exe")
    command("start");time.sleep(.5)
    rejected = summary("wrong-hash")
    assert rejected["Connected"] == "0" and not fixture()["core_loaded"]
finally:
    original.unlink(missing_ok=True);os.replace(verified, original)

command("start");time.sleep(1.1)
started = summary("connected")
assert started["Connected"] == "1" and started["Engine status"] == "2", started
assert started["Controller"] == "0" and int(started["Frames"]) > 0
assert not fixture()["registered_engine"]
log_windows = started["Attach log"]
assert log_windows.lower().startswith("c:\\")
attach_log = lab / "mezelounge-runtime/prefix-stable-clean/drive_c" / log_windows[3:].replace("\\", "/")
log_text = attach_log.read_text(encoding="utf-8")
assert "[version] Frontier Orbit Studio 0.8.1" in log_text
assert "[attach 3/5] Loader handle closed" in log_text
assert "[attach 5/5] Engine status 2" in log_text
command("camera");command("start")
driver("screenshot", "rain-preview.bmp") # focus stays on the real external app
time.sleep(.2)
focused = summary("app-focus")
assert focused["State"] == "ACTIVE" and int(focused["Frames"]) > int(started["Frames"])

command("set distance 1.2");command("set height 30");command("set shoulder 40");time.sleep(.2)
changed = summary("live-settings")
requested,actual,reference = map(float,changed["Distance requested/actual/reference"].split("/"))
assert abs(requested-720)<.1 and reference==600 and actual>600, changed
command("save " + setup_name)
profiles = list((lab / "mezelounge-runtime/prefix-stable-clean/drive_c/users").glob(
    "*/AppData/Local/FrontierOrbitRain/setups/*.json"))
saved = [p for p in profiles if json.loads(p.read_text())["name"] == setup_name]
assert len(saved)==1 and json.loads(saved[0].read_text())["camera"]["gamepad_index"] == -1
command("set height 31");command("save " + setup_name)
assert json.loads(saved[0].read_text())["camera"]["height_offset"] == 31
assert len([p for p in saved[0].parent.glob("*.json") if json.loads(p.read_text())["name"] == setup_name])==1
command("set height 30");command("save " + setup_name)
command("set distance 0.8");command("load " + setup_name);time.sleep(.2)
assert summary("reloaded")["Distance requested/actual/reference"].startswith("720.00/")

command("camera");time.sleep(.15);driver("drag","297","265","365","265");time.sleep(.15)
dragged = summary("slider")
assert dragged["Profile accepted/applied"].split("/")[0] == dragged["Profile accepted/applied"].split("/")[1]
command("load " + setup_name)
driver("fixture-map");time.sleep(.85)
transition = summary("transition")
assert transition["State"] == "ACTIVE" and transition["Distance requested/actual/reference"] == "720.00/720.00/600.00", transition
command("reset");time.sleep(.2)
reset = summary("reset");accepted,applied = map(int,reset["Reset accepted/applied"].split("/"))
assert accepted==applied and accepted>0
command("stop");time.sleep(.15)
stopped = summary("stopped")
assert stopped["State"] == "STOPPED"
before = fixture();assert before["eye"] == [1000,100,915] and before["target"] == [1000,100,1000]
command("start");time.sleep(.8);driver("close");time.sleep(.1)
after_close = fixture();assert after_close["eye"] == [1000,100,915]
launch("FrontierOrbitStudio.exe");time.sleep(.3)
inactive = summary("reopened-inactive")
assert inactive["Connected"] == "0" and fixture()["eye"] == [1000,100,915]
command("load " + setup_name);command("start");time.sleep(.85)
reconnect = summary("reconnect")
assert reconnect["Connected"] == "1" and reconnect["Distance requested/actual/reference"] == "720.00/720.00/600.00", reconnect
assert attach_log.read_text(encoding="utf-8").count("[attach 1/5]")==1
log_windows = reconnect["Attach log"]
attach_log = lab / "mezelounge-runtime/prefix-stable-clean/drive_c" / log_windows[3:].replace("\\", "/")
assert "Reusing the existing Rain 0.8 module" in attach_log.read_text(encoding="utf-8")
assert attach_log.read_text(encoding="utf-8").count("[attach 1/5]")==0
command("camera");command("start");driver("screenshot","final-preview.bmp")
command("setups");driver("screenshot","setups-preview.bmp")
driver("fixture-close");time.sleep(.3)
assert summary("game-exit")["Connected"] == "0"
driver("close")
assert "[link] Connection lost" in attach_log.read_text(encoding="utf-8")

report = {"environment":"Wine 11, actual app/loader/IPC/core; synthetic manually mapped Rain engine",
          "real_rain_gameplay_test":False,"client_dll_hash_verified":True,
          "wrong_hash_blocks_injection":True,"manual_start":True,"external_app_focus_no_pad":True,
          "live_parameters":True,"mouse_slider":True,"unicode_json_setup_save_load":True,
          "save_same_name_overwrites":True,"game_exit_disconnects":True,
          "persistent_attach_log":True,"loader_handle_closed_before_ipc":True,
          "reconnect_does_not_open_loader_handle":True,
          "transition_keeps_reference":True,"reset":True,"stop_returns_native":True,
          "close_returns_native":True,"reopen_no_autoconnect":True,"reconnect_reuses_core":True,
          "started":started,"transition":transition,"reconnect":reconnect}
(repo / "rain-studio/smoke-verification.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
print("PASS: Rain mapped adapter, hash rejection, manual injection, external-focus preview, live/mouse settings, Unicode profiles, transition, reset, stop/close/native and reconnect.", flush=True)
