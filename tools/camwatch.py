# gdb camera-writer logger for dw5e (batch mode, never stops the game for long).
# Launch with tools/run_camwatch.sh. Watchpoints arm when the guest view-array init (0x822A9700) runs.
# Every distinct writer stack is logged once per phase to notes/camwatch.log; the phase tag is
# read from notes/camwatch.phase (e.g. "menu", "battle"), so a log can be split by game state.
# Hit counts are rewritten to notes/camwatch.counts every 2 s.
# Guest addr G is host 0x100000000 + G.
import gdb, time

NOTES = "/home/slack/Projects/dw5e-port/notes/"
WATCH = {0x82847000: "view0.eye.x", 0x82846FF4: "view0.yaw"}
seen = {}
log = open(NOTES + "camwatch.log", "a")
last_dump = [0.0]


def phase():
    try:
        return open(NOTES + "camwatch.phase").read().strip() or "-"
    except OSError:
        return "-"


def stack_key(depth=8):
    f, names = gdb.newest_frame(), []
    while f is not None and len(names) < depth:
        names.append(f.name() or hex(f.pc()))
        f = f.older()
    return tuple(names)


class CamWatch(gdb.Breakpoint):
    def __init__(self, guest, label):
        super().__init__(f"*(unsigned int*){0x100000000 + guest:#x}", gdb.BP_WATCHPOINT, gdb.WP_WRITE)
        self.label = label

    def stop(self):
        k = (phase(), self.label) + stack_key()
        seen[k] = seen.get(k, 0) + 1
        if seen[k] == 1:
            log.write(f"[{k[0]}][{k[1]}] " + " <- ".join(k[2:]) + "\n")
            log.flush()
        now = time.monotonic()
        if now - last_dump[0] > 2:
            last_dump[0] = now
            with open(NOTES + "camwatch.counts", "w") as out:
                for kk, n in sorted(seen.items(), key=lambda x: -x[1]):
                    out.write(f"{n:8d} [{kk[0]}][{kk[1]}] " + " <- ".join(kk[2:]) + "\n")
        return False


class Arm(gdb.Command):
    def __init__(self):
        super().__init__("camwatch", gdb.COMMAND_USER)

    def invoke(self, arg, tty):
        for g, l in WATCH.items():
            CamWatch(g, l)
        log.write("---- armed " + time.strftime("%H:%M:%S") + "\n")
        log.flush()


Arm()
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("handle SIGSEGV SIGBUS SIGUSR1 SIGUSR2 SIGPIPE SIGILL nostop noprint pass")
