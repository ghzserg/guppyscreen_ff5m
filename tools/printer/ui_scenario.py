#!/usr/bin/env python3
"""Scripted UI scenario on the printer, sampling guppyscreen's memory.

    tools/printer/ui_scenario.py LABEL OUT_DIR [SCENARIO]

Restarts guppyscreen, waits 90 s, then taps through SCENARIO:
- files (default): Print (file list), 5 file rows, Back, Tune,
  Input Shaper, Back, Home;
- macros: Macros tab, Home tab, twice.
After every tap it waits,
takes a framebuffer screenshot (OUT_DIR/NN-step.png) and samples Anonymous
and VmHWM. At the end it prints a table and the PNG decoder's log lines.

Safety:
- Only the named taps in SAFE_TAPS exist. They open panels, select file
  rows and go back; nothing moves, heats, prints, saves or sends G-code.
  Never add the Print button of the file list (starts a print), Stop,
  Calibrate, Save, temperature fields, anything on the console tab, or
  anything inside the macro list (run and hide buttons): only the tab bar.
- Before every tap: the printer must be idle and the expected screen
  must be showing (pixel checks from verified screenshots). After every
  tap the resulting screen is checked. Any mismatch aborts the run.

Needs /tmp/guppy-tap on the printer (guppy-tap.c, linked statically).
"""
import os
import subprocess
import sys
import time

HOST = 'root@ff'
W, H = 800, 480

# name: (x, y, screen it is tapped on, screen expected afterwards)
SAFE_TAPS = {
    'print':        (520, 395, 'main',   'files'),   # main screen "Print": opens the file list
    'file_row_1':   (150, 123, 'files',  'files'),   # file rows: select a file, show its thumbnail
    'file_row_2':   (150, 172, 'files',  'files'),
    'file_row_3':   (150, 221, 'files',  'files'),
    'file_row_4':   (150, 270, 'files',  'files'),
    'file_row_5':   (150, 319, 'files',  'files'),
    'files_back':   (765, 425, 'files',  'main'),    # far right edge of Back; Print is at x~605
    'tune_tab':     (30, 140,  'main',   'tune'),
    'input_shaper': (160, 330, 'tune',   'shaper'),
    'shaper_back':  (740, 425, 'shaper', 'tune'),    # Back; Calibrate/Save/Stop are at x<100
    'home_tab':     (30, 48,   'tune',   'main'),
    # Tab bar: 5 tabs of 96 px; Macros is the 4th (y 288-384), Console the 5th.
    'macros_tab':   (30, 336,  'main',   'macros'),
    'macros_home':  (30, 48,   'macros', 'main'),
}

SCENARIOS = {
    'files':  ['print', 'file_row_1', 'file_row_2', 'file_row_3', 'file_row_4', 'file_row_5',
               'files_back', 'tune_tab', 'input_shaper', 'shaper_back', 'home_tab'],
    'macros': ['macros_tab', 'macros_home', 'macros_tab', 'macros_home'],
}

TAB_ON, BG = (63, 66, 70), (40, 43, 48)
SCREENS = {
    'main':   {(5, 48): TAB_ON, (5, 430): BG, (65, 150): BG},
    'files':  {(40, 20): (33, 150, 243), (5, 430): BG},
    'tune':   {(5, 140): TAB_ON, (5, 430): BG, (65, 150): BG},
    'shaper': {(65, 150): (255, 255, 255), (5, 430): BG},
    'macros': {(5, 336): TAB_ON, (5, 48): BG, (5, 430): BG},
}


def ssh(cmd):
    return subprocess.run(['ssh', '-q', HOST, cmd], capture_output=True, check=True).stdout


def screenshot():
    raw = ssh(f'dd if=/dev/fb0 bs={W * 4} count={H} 2>/dev/null')
    if len(raw) < W * H * 4:
        sys.exit(f'short screenshot: {len(raw)} bytes')
    return raw


def save_png(raw, path):
    subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'fb2png.py'), '/dev/stdin', path],
                   input=raw, check=True)


def matches(raw, screen):
    for (x, y), want in SCREENS[screen].items():
        i = (y * W + x) * 4
        got = (raw[i + 2], raw[i + 1], raw[i])
        if any(abs(a - b) > 12 for a, b in zip(got, want)):
            return False
    return True


def printer_idle():
    out = ssh('wget -qO- "http://127.0.0.1:7125/printer/objects/query?print_stats=state"').decode()
    return any(f'"state": "{s}"' in out or f'"state":"{s}"' in out
               for s in ('standby', 'complete', 'cancelled', 'error'))


def sample():
    out = ssh('P=$(pidof guppyscreen); awk \'/^Anonymous:/{print $2}\' /proc/$P/smaps_rollup; '
              'awk \'/VmHWM/{print $2}\' /proc/$P/status; echo $P').decode().split()
    return int(out[0]), int(out[1]), out[2]


def main():
    label, out_dir = sys.argv[1], sys.argv[2]
    steps = SCENARIOS[sys.argv[3] if len(sys.argv) > 3 else 'files']
    os.makedirs(out_dir, exist_ok=True)
    if b'guppy-tap' not in ssh('ls /tmp/guppy-tap 2>/dev/null; true'):
        sys.exit('no /tmp/guppy-tap on the printer')
    if not printer_idle():
        sys.exit('printer is not idle')

    ssh('chroot /data/.mod/.zmod /etc/init.d/S80guppyscreen restart >/dev/null 2>&1')
    t0 = time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime())
    time.sleep(90)

    rows = []
    anon, hwm, pid = sample()
    rows.append(('idle 90 s', anon, hwm))
    screen = 'main'
    raw = screenshot()
    save_png(raw, f'{out_dir}/00-idle.png')
    if not matches(raw, screen):
        sys.exit('ABORT: main screen not showing after restart (see 00-idle.png)')

    for n, name in enumerate(steps, 1):
        x, y, on, after = SAFE_TAPS[name]
        if not printer_idle():
            sys.exit(f'ABORT before {name}: printer is not idle')
        raw = screenshot()
        if not matches(raw, on):
            save_png(raw, f'{out_dir}/{n:02d}-before-{name}-UNEXPECTED.png')
            sys.exit(f'ABORT before {name}: expected screen "{on}"')
        ssh(f'/tmp/guppy-tap {x} {y}')
        time.sleep(5 if name == 'input_shaper' else 3)
        raw = screenshot()
        save_png(raw, f'{out_dir}/{n:02d}-{name}.png')
        if not matches(raw, after):
            sys.exit(f'ABORT after {name}: expected screen "{after}" (see {n:02d}-{name}.png)')
        anon, hwm, pid2 = sample()
        if pid2 != pid:
            sys.exit(f'ABORT: guppyscreen restarted (pid {pid} -> {pid2})')
        rows.append((name, anon, hwm))
        screen = after

    time.sleep(10)
    anon, hwm, _ = sample()
    rows.append(('10 s later', anon, hwm))

    print(f'{label}: step, Anonymous kB, VmHWM kB')
    for r in rows:
        print(f'{label}  {r[0]:<14} {r[1]:>6} {r[2]:>6}')
    log = ssh('grep -E "png:|macros:" /opt/config/mod_data/log/guppyscreen.log; true').decode().strip()
    print(log if log else '(no png decoder or macro list log lines)')


if __name__ == '__main__':
    main()
