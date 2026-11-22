#!/usr/bin/env python3
"""Преобразует короткую команду из PR в сценарий тестового LVGL."""
import json
import os
import re
import sys
import urllib.request
from pathlib import Path


def scenario(command):
    if not command.startswith('/ui-review '):
        raise ValueError('Expected: /ui-review home, home@1200, settings/System')
    screens = [s.strip() for s in command[len('/ui-review '):].split(',')]
    if not 1 <= len(screens) <= 8 or any(not s for s in screens):
        raise ValueError('Specify 1–8 screens, comma-separated')
    lines = []
    for index, spec in enumerate(screens, 1):
        match = re.fullmatch(r'(.+?)(?:@(\d{1,5}))?', spec)
        if not match:
            raise ValueError(f'Invalid screen: {spec}')
        path = [s.strip() for s in match[1].split('/')]
        delay = int(match[2] or 0)
        if not 1 <= len(path) <= 4 or path[0] not in ('home', 'print', 'tune', 'settings') or delay > 10000:
            raise ValueError(f'Unsupported screen or delay: {spec}')
        if any(not re.fullmatch(r'[\w .()+:-]{1,64}', part) for part in path):
            raise ValueError(f'Invalid screen name: {spec}')
        name = '-'.join(re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-') for s in path) + f'-{index}'
        lines.append(f'tab {path[0]}')
        lines.extend(f'tap {label}' for label in path[1:])
        if delay:
            lines.append(f'sleep {delay}')
        lines.extend((f'save {name}', f'memory {name}'))
    return '\n'.join(lines) + '\n'


def main():
    event = json.loads(Path(sys.argv[1]).read_text())
    repo = os.environ['GITHUB_REPOSITORY']
    if 'comment' in event:
        if 'pull_request' not in event['issue'] or event['comment']['author_association'] not in ('OWNER', 'MEMBER', 'COLLABORATOR'):
            raise ValueError('Only repository collaborators can request PR UI reviews')
        command = event['comment']['body'].strip().splitlines()[0]
        number = event['issue']['number']
        url = f'https://api.github.com/repos/{repo}/pulls/{number}'
        req = urllib.request.Request(url, headers={
            'Accept': 'application/vnd.github+json',
            'Authorization': f"Bearer {os.environ['GITHUB_TOKEN']}",
        })
        with urllib.request.urlopen(req, timeout=30) as response:
            pr = json.load(response)
        if pr['state'] != 'open' or pr['head']['repo']['full_name'] != repo:
            raise ValueError('Only open PRs from this repository are supported')
        base_sha, head_sha = pr['base']['sha'], pr['head']['sha']
    else:
        # Smoke-test нового workflow в PR: один SHA для обоих снимков.
        pr = event['pull_request']
        number = pr['number']
        base_sha = head_sha = pr['head']['sha']
        command = '/ui-review home, home@1200, settings/System'
    Path(sys.argv[2]).write_text(scenario(command))
    with open(os.environ['GITHUB_OUTPUT'], 'a') as out:
        out.write(f'pr_number={number}\nbase_sha={base_sha}\nhead_sha={head_sha}\n')
    print(f'PR #{number}: {base_sha} -> {head_sha}\n{scenario(command)}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError) as exc:
        sys.exit(str(exc))
