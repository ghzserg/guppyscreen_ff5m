#!/usr/bin/env python3
"""Формирует сценарий LVGL из списка экранов PR или команды /ui-review."""
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

# Постоянно доступные окна AD5M: открытие без реальных действий над принтером.
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
    'print-paused': 'status {"print_stats":{"state":"paused","filename":"cube.gcode"}}\ntab print',
    'home-printing': 'status {"print_stats":{"state":"printing","filename":"cube.gcode"}}\ntab home',
    'home-paused': 'status {"print_stats":{"state":"paused","filename":"cube.gcode"}}\ntab home',
}
DEFAULT = 'home, print, tune, settings'


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
    return '\n'.join(lines) + '\n'


def api(path, method='GET', body=None):
    request = urllib.request.Request(
        f"https://api.github.com/repos/{os.environ['GITHUB_REPOSITORY']}/{path}",
        data=json.dumps(body).encode() if body is not None else None,
        headers={'Accept': 'application/vnd.github+json',
                 'Authorization': f"Bearer {os.environ['GITHUB_TOKEN']}",
                 'Content-Type': 'application/json'},
        method=method)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def hint(event):
    pr = event['pull_request']
    if pr['head']['repo']['full_name'] != os.environ['GITHUB_REPOSITORY']:
        return
    try:
        api('labels/ui-review')
    except urllib.error.HTTPError as exc:
        if exc.code != 404:
            raise
        try:
            api('labels', 'POST', {'name': 'ui-review', 'color': 'd4c5f9',
                                   'description': 'Снимки и замеры памяти для PR'})
        except urllib.error.HTTPError as create_error:
            if create_error.code != 422:
                raise
    comment = (
        '### Снимки интерфейса и память\n\n'
        'Чтобы добавить изображения до/после и замеры памяти x86 прямо в описание PR, '
        'укажите в описании строку:\n\n'
        'UI-Review: home, pid, system\n\n'
        'Затем поставьте метку ui-review. Если строка отсутствует, снимутся '
        'home, print, tune, settings.\n\n'
        'Доступные окна AD5M: ' + ', '.join(SCREENS) + '. '
        'Дополнительно можно указывать путь по кнопкам (settings/System), '
        'задержку (home@1200). Не более 8 снимков за запуск. '
        'Никаких новых сценариев или PNG в PR добавлять не нужно. '
        'Для повторного запуска снимите и верните метку.'
    )
    api(f"issues/{pr['number']}/comments", 'POST', {'body': comment})


def main():
    if sys.argv[1] == '--hint':
        hint(json.loads(Path(sys.argv[2]).read_text()))
        return
    event = json.loads(Path(sys.argv[1]).read_text())
    repo = os.environ['GITHUB_REPOSITORY']
    if 'comment' in event:
        if 'pull_request' not in event['issue'] or event['comment']['author_association'] not in ('OWNER', 'MEMBER', 'COLLABORATOR'):
            raise ValueError('Only collaborators can request PR UI reviews')
        command = event['comment']['body'].strip().splitlines()[0]
        pr = api(f"pulls/{event['issue']['number']}")
    elif os.environ.get('GITHUB_EVENT_NAME') == 'pull_request_target':
        pr = event['pull_request']
        line = next((m.group(1) for text in (pr.get('body') or '').splitlines()
                     if (m := re.fullmatch(r'\s*UI-Review:\s*(.+?)\s*', text, re.I))), DEFAULT)
        command = '/ui-review ' + line
    else:
        # Smoke-test текущего PR: одинаковый SHA, без права записи.
        pr = event['pull_request']
        command = '/ui-review home, file-preview, tune/PID, settings/System'
    if pr['state'] != 'open' or pr['head']['repo']['full_name'] != repo:
        raise ValueError('Only open same-repository PRs are supported')
    number, head_sha = pr['number'], pr['head']['sha']
    base_sha = head_sha if os.environ.get('GITHUB_EVENT_NAME') == 'pull_request' else pr['base']['sha']
    generated = scenario(command)
    Path(sys.argv[2]).write_text(generated)
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write(f'pr_number={number}\nbase_sha={base_sha}\nhead_sha={head_sha}\n')
    print(f'PR #{number}: {base_sha} -> {head_sha}\n{generated}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError) as exc:
        sys.exit(str(exc))
