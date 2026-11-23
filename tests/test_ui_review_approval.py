import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).resolve().parents[1] / 'ci' / 'ui-review-approval.py'
spec = importlib.util.spec_from_file_location('approval', path)
approval = importlib.util.module_from_spec(spec)
spec.loader.exec_module(approval)


def pr(owner=False, fork=False):
    return {'state': 'open', 'number': 12, 'body': 'UI-Review: file-preview',
            'head': {'repo': {'full_name': 'other/fork' if fork else 'ghzserg/guppyscreen_ff5m'}},
            'user': {'id': approval.OWNER_ID if owner else 12345}}


def label_event(kind, actor, number):
    return {'event': kind, 'actor': {'id': actor}, 'id': number,
            'label': {'name': approval.APPROVAL_LABEL}}


class UIReviewApprovalTest(unittest.TestCase):
    def check(self, request, labels, events):
        calls = []

        def fetch(url):
            calls.append(url)
            if '/events?' in url:
                return events
            return {'labels': [{'name': name} for name in labels]}

        return approval.approved(request, 'ghzserg/guppyscreen_ff5m', fetch), calls

    def test_owner_does_not_need_label(self):
        allowed, calls = self.check(pr(owner=True), [], [])
        self.assertTrue(allowed)
        self.assertEqual(calls, [])

    def test_mention_without_directive(self):
        request = pr(owner=True)
        request['body'] = 'Mentioning `UI-Review:` is not a request.'
        self.assertFalse(self.check(request, [], [])[0])

    def test_other_author_without_label(self):
        self.assertFalse(self.check(pr(), [], [])[0])

    def test_owner_applied_label(self):
        self.assertTrue(self.check(pr(), [approval.APPROVAL_LABEL],
                                   [label_event('labeled', approval.OWNER_ID, 10)])[0])

    def test_other_author_applied_label(self):
        self.assertFalse(self.check(pr(), [approval.APPROVAL_LABEL],
                                    [label_event('labeled', 12345, 10)])[0])

    def test_readded_by_other_author(self):
        history = [label_event('labeled', 12345, 30),
                   label_event('unlabeled', 12345, 20),
                   label_event('labeled', approval.OWNER_ID, 10)]
        self.assertFalse(self.check(pr(), [approval.APPROVAL_LABEL], history)[0])

    def test_removed_label(self):
        self.assertFalse(self.check(pr(), [], [label_event('labeled', approval.OWNER_ID, 10)])[0])

    def test_fork_is_unsupported(self):
        self.assertFalse(self.check(pr(owner=True, fork=True), [], [])[0])


if __name__ == '__main__':
    unittest.main()
