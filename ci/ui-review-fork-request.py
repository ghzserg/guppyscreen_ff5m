#!/usr/bin/env python3
"""Находит открытый PR в основной репозиторий для текущей ветки форка."""
import json
import os
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

UPSTREAM = 'ghzserg/guppyscreen_ff5m'
DIRECTIVE = re.compile(r'^\s*UI-Review:\s*(.+?)\s*$', re.I | re.M)


def find_request(repository, branch, sha, fetch):
    owner = repository.split('/', 1)[0]
    query = urllib.parse.urlencode({'state': 'open', 'head': f'{owner}:{branch}',
                                    'per_page': 100})
    pulls = fetch(f'https://api.github.com/repos/{UPSTREAM}/pulls?{query}')
    for pr in pulls:
        if (pr.get('state') == 'open' and
                (pr.get('head', {}).get('repo') or {}).get('full_name') == repository and
                pr['head'].get('sha') == sha and
                (pr.get('base', {}).get('repo') or {}).get('full_name') == UPSTREAM and
                DIRECTIVE.search(pr.get('body') or '')):
            return pr
    return None


def main():
    repository = os.environ['GITHUB_REPOSITORY']
    headers = {'Accept': 'application/vnd.github+json',
               'Authorization': f'Bearer {os.environ["GITHUB_TOKEN"]}'}

    def fetch(url):
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers),
                                    timeout=30) as response:
            return json.load(response)

    pr = find_request(repository, os.environ['GITHUB_REF_NAME'], os.environ['GITHUB_SHA'],
                      fetch)
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write(f'enabled={str(pr is not None).lower()}\n')
        if pr:
            output.write(f'pr_number={pr["number"]}\n')
            output.write(f'base_sha={pr["base"]["sha"]}\n')
            output.write(f'head_sha={pr["head"]["sha"]}\n')
    if pr:
        Path(sys.argv[1]).write_text(json.dumps({'pull_request': pr}))
        print(f'UI review requested by upstream PR #{pr["number"]}')
    else:
        print('No matching upstream PR with a UI-Review request; skipping')


if __name__ == '__main__':
    try:
        main()
    except (KeyError, OSError, ValueError) as exc:
        sys.exit(str(exc))
