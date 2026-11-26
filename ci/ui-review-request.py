#!/usr/bin/env python3
"""Формирует сценарий LVGL по строке UI-Review в описании PR."""
import json
import os
import re
import sys
from pathlib import Path

# Общие окна C5 Pro, AD5X и AD5M: открытие без реальных действий над принтером.
SCREENS = {
    'home': 'tab home',
    'print': 'tab print',
    'tune': 'tab tune',
    'settings': 'tab settings',
    'homing': 'tab home\ntap Homing',
    'extrude': 'tab home\ntap Extrude',
    'fans': 'tab home\ntap Fans',
    'led': 'tab home\ntap LED',
    'fine-tune': 'tab home\ntap Fine Tune',
    'bed-mesh': 'tab home\ntap Bed Mesh',
    'screw-adjustment': 'tab home\ntap Bed Mesh\ntap Screw adjustment',
    'limits': 'tab tune\ntap Limits',
    'retract': 'tab tune\ntap Retract',
    'input-shaper': 'tab tune\ntap Input Shaper',
    'belts': 'tab tune\ntap Belts/Shake',
    'pid': 'tab tune\ntap PID',
    'system': 'tab settings\ntap System',
    'macros': 'tab settings\ntap Macros',
    'console': 'tab settings\ntap Console',
    'file-preview': 'tab print\ntable_row cube.gcode',
    'print-status': 'status {"print_stats":{"state":"printing","filename":"cube.gcode"}}\ntab print',
    'exclude-map': (
        'status {"print_stats":{"state":"printing","filename":"cube.gcode"},'
        '"exclude_object":{"objects":[{"name":"CENTER_CUBE","polygon":'
        '[[88,88],[128,88],[128,128],[88,128]]},'
        '{"name":"CORNER_CUBE","polygon":[[20,20],[40,20],[40,40],[20,40]]},'
        '{"name":"EXCLUDED_CUBE","polygon":[[88,20],[128,20],[128,60],[88,60]]}],'
        '"excluded_objects":["EXCLUDED_CUBE"],"current_object":"CENTER_CUBE"}}\n'
        'tab print\n'
        'tap_parent cube.gcode'
    ),
    'print-paused': 'status {"print_stats":{"state":"paused","filename":"cube.gcode"}}\ntab print',
    'home-printing': 'status {"print_stats":{"state":"printing","filename":"cube.gcode"}}\ntab home',
    'home-paused': 'status {"print_stats":{"state":"paused","filename":"cube.gcode"}}\ntab home',
}

def scenario(command):
    if not command.startswith('/ui-review '):
        raise ValueError('Expected /ui-review home, pid, system')
    screens = [s.strip() for s in command[len('/ui-review '):].split(',')]
    if not 1 <= len(screens) <= 8 or any(not s for s in screens):
        raise ValueError('Choose 1–8 screens, comma-separated')
    lines, changed_state = [], False
    for index, spec in enumerate(screens, 1):
        match = re.fullmatch(r'(.+?)(?:@(\d{1,5}))?', spec)
        if not match:
            raise ValueError(f'Invalid screen: {spec}')
        name = match[1].strip()
        delay = int(match[2] or 0)
        if delay > 10000:
            raise ValueError(f'Delay exceeds 10000 ms: {spec}')
        route = SCREENS.get(name.lower())
        if route is None:
            parts = [p.strip() for p in name.split('/')]
            if not 1 <= len(parts) <= 5 or parts[0].lower() not in ('home', 'print', 'tune', 'settings'):
                raise ValueError(f'Unknown screen: {name}. Available: {", ".join(SCREENS)}')
            if any(not re.fullmatch(r'[\w .()+:-]{1,64}', p) for p in parts):
                raise ValueError(f'Invalid screen path: {name}')
            route = '\n'.join([f'tab {parts[0].lower()}'] + [f'tap {p}' for p in parts[1:]])
        if changed_state and not route.startswith('status '):
            lines.append('status {"print_stats":{"state":"standby","filename":""}}')
            changed_state = False
        if route.startswith('status '):
            changed_state = True
        lines.extend(route.splitlines())
        if delay:
            lines.append(f'sleep {delay}')
        filename = re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-') or 'screen'
        lines.extend((f'save {filename}-{index}', f'memory {filename}-{index}'))
        if name.lower() == 'screw-adjustment':
            lines.append('tap_optional Close')
    return '\n'.join(lines) + '\n'


def main():
    event = json.loads(Path(sys.argv[1]).read_text())
    pr = event['pull_request']
    if pr['state'] != 'open' or pr['head']['repo']['full_name'] != os.environ['GITHUB_REPOSITORY']:
        raise ValueError('Only open PRs from this repository are supported')

    screens = next(
        (match.group(1) for line in (pr.get('body') or '').splitlines()
         if (match := re.fullmatch(r'\s*UI-Review:\s*(.+?)\s*', line, re.I))),
        None)
    if not screens:
        raise ValueError('No valid UI-Review: screens line found in PR description')

    script = scenario('/ui-review ' + screens)
    Path(sys.argv[2]).write_text(script)
    with open(os.environ['GITHUB_OUTPUT'], 'a') as out:
        out.write(f"pr_number={pr['number']}\nbase_sha={pr['base']['sha']}\n"
                  f"head_sha={pr['head']['sha']}\n")
    print(f"PR #{pr['number']}: {pr['base']['sha']} -> {pr['head']['sha']}\n{script}")


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError) as exc:
        sys.exit(str(exc))
