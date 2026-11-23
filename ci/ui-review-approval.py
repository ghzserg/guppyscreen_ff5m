#!/usr/bin/env python3
"""Разрешает UI-ревью для PR автора репозитория или по его метке."""
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

OWNER_ID = 119299827  # ghzserg
APPROVAL_LABEL = 'Готов к ревью'


def approved(pr, repository, fetch):
    if (pr.get('state') != 'open' or
            (pr.get('head', {}).get('repo') or {}).get('full_name') != repository):
        return False
    if not any(re.fullmatch(r'\s*UI-Review:\s*(.+?)\s*', line, re.I)
               for line in (pr.get('body') or '').splitlines()):
        return False
    if (pr.get('user') or {}).get('id') == OWNER_ID:
        return True

    url = f'https://api.github.com/repos/{repository}/issues/{pr["number"]}'
    issue = fetch(url)
    if not any(label['name'] == APPROVAL_LABEL for label in issue['labels']):
        return False

    latest = None
    page = 1
    while True:
        events = fetch(f'{url}/events?per_page=100&page={page}')
        for event in events:
            if (event.get('event') in ('labeled', 'unlabeled') and
                    (event.get('label') or {}).get('name') == APPROVAL_LABEL and
                    (latest is None or event['id'] > latest['id'])):
                latest = event
        if len(events) < 100:
            break
        page += 1
    return bool(latest and latest['event'] == 'labeled' and
                (latest.get('actor') or {}).get('id') == OWNER_ID)


def main():
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    headers = {'Accept': 'application/vnd.github+json',
               'Authorization': f'Bearer {os.environ["GITHUB_TOKEN"]}'}

    def fetch(url):
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
            return json.load(response)

    allowed = approved(event['pull_request'], os.environ['GITHUB_REPOSITORY'], fetch)
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write(f'approved={str(allowed).lower()}\n')
    print('UI review approved' if allowed else 'UI review skipped: owner approval required')


if __name__ == '__main__':
    try:
        main()
    except (KeyError, OSError, ValueError) as exc:
        sys.exit(str(exc))
