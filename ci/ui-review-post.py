#!/usr/bin/env python3
"""Обновляет только автоматически созданный раздел ревью в описании PR."""
import json
import os
import sys
import urllib.request
from pathlib import Path

START = '<!-- ui-review:start -->'
END = '<!-- ui-review:end -->'


def main():
    report = Path(sys.argv[1]).read_text()
    number, expected_sha = sys.argv[2:4]
    url = f"https://api.github.com/repos/{os.environ['GITHUB_REPOSITORY']}/pulls/{number}"
    headers = {'Accept': 'application/vnd.github+json',
               'Content-Type': 'application/json',
               'Authorization': f"Bearer {os.environ['GITHUB_TOKEN']}"}
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
        pr = json.load(response)
    if pr['state'] != 'open' or pr['head']['sha'] != expected_sha:
        raise ValueError('PR changed during capture; request /ui-review again')
    original = pr.get('body') or ''
    section = (f'{START}\n## Автоматическое UI-ревью\n\n{report}\n\n'
               'Измерения сделаны на x86, а не на физическом принтере.\n' + END)
    if START in original and END in original:
        original = original[:original.index(START)] + original[original.index(END) + len(END):]
    body = original.rstrip() + '\n\n' + section
    request = urllib.request.Request(url, data=json.dumps({'body': body}).encode(),
                                     headers=headers, method='PATCH')
    with urllib.request.urlopen(request, timeout=30) as response:
        print(f"Updated PR #{number}: {json.load(response)['html_url']}")


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError) as exc:
        sys.exit(str(exc))
