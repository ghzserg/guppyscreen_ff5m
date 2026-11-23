import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).resolve().parents[1] / 'ci' / 'ui-review-fork-request.py'
spec = importlib.util.spec_from_file_location('fork_request', path)
fork_request = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fork_request)

FORK = 'contributor/guppyscreen_ff5m'
SHA = 'a' * 40


def upstream_pr(body='UI-Review: file-preview', sha=SHA, fork=FORK):
    return {'state': 'open', 'number': 25, 'body': body,
            'head': {'sha': sha, 'repo': {'full_name': fork}},
            'base': {'sha': 'b' * 40, 'repo': {'full_name': fork_request.UPSTREAM}}}


class ForkReviewRequestTest(unittest.TestCase):
    def lookup(self, prs, branch='feature/swap-buttons'):
        urls = []

        def fetch(url):
            urls.append(url)
            return prs

        return fork_request.find_request(FORK, branch, SHA, fetch), urls

    def test_matching_upstream_pr(self):
        result, urls = self.lookup([upstream_pr()])
        self.assertEqual(result['number'], 25)
        self.assertIn('head=contributor%3Afeature%2Fswap-buttons', urls[0])

    def test_no_pr(self):
        self.assertIsNone(self.lookup([])[0])

    def test_missing_directive(self):
        self.assertIsNone(self.lookup([upstream_pr(body='Updated layout')])[0])

    def test_prose_mention_does_not_trigger(self):
        self.assertIsNone(self.lookup([upstream_pr(body='Please add `UI-Review:` later.')])[0])

    def test_head_must_match_current_commit(self):
        self.assertIsNone(self.lookup([upstream_pr(sha='c' * 40)])[0])

    def test_head_must_match_fork(self):
        self.assertIsNone(self.lookup([upstream_pr(fork='other/fork')])[0])

    def test_base_must_match_upstream(self):
        request = upstream_pr()
        request['base']['repo']['full_name'] = 'someone/else'
        self.assertIsNone(self.lookup([request])[0])

    def test_ignores_unrelated_pr(self):
        self.assertEqual(self.lookup([upstream_pr(sha='c' * 40), upstream_pr()])[0]['number'], 25)


if __name__ == '__main__':
    unittest.main()
