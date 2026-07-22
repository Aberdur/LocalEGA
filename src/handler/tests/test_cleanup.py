import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from code import cleanup


class FakePool:
    def __init__(self, rows):
        self.rows = rows
        self.fetch_args = None
        self.executed = []

    async def fetch(self, query, *args):
        self.fetch_args = args
        return self.rows

    async def execute(self, query, *args):
        self.executed.append((query, args))


class FakeDB:
    def __init__(self, rows):
        self.connection = FakePool(rows)


class FakeConfig:
    def __init__(self, inbox_root, vault_root, rows):
        self.db = FakeDB(rows)
        self.inbox_root = inbox_root
        self.vault_root = vault_root

    def get(self, section, option, raw=False):
        if section == 'inbox':
            return str(self.inbox_root / '%s')
        if section == 'vault':
            return str(self.vault_root)
        raise KeyError((section, option))


class CleanupValidationTests(unittest.TestCase):
    def test_env_bool_rejects_invalid_value(self):
        with mock.patch.dict(os.environ, {'CLEANUP_BOOL': 'treu'}):
            with self.assertRaisesRegex(ValueError, 'CLEANUP_BOOL'):
                cleanup.env_bool('CLEANUP_BOOL', True)

    def test_safe_relative_path_rejects_root_and_traversal(self):
        for value in ('', '/', '.', '../file', 'dir/../../file'):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    cleanup.safe_relative_path(value)


class CleanupRunTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        root = Path(self.temporary_directory.name)
        self.inbox_root = root / 'inbox'
        self.vault_root = root / 'vault'
        self.user_root = self.inbox_root / 'user@example.org'
        self.user_root.mkdir(parents=True)
        self.vault_root.mkdir()

    def tearDown(self):
        self.temporary_directory.cleanup()

    def make_row(self, source_path, vault_path):
        source_stat = source_path.stat()
        vault_stat = vault_path.stat()
        return {
            'id': 7,
            'username': 'user@example.org',
            'filepath': '/' + source_path.name,
            'accession_id': 'EGAF000000001',
            'vault_relative_path': vault_path.name,
            'inbox_filesize': source_stat.st_size,
            'inbox_mtime_ns': source_stat.st_mtime_ns,
            'completed_at': None,
            'registered_vault_relative_path': vault_path.name,
            'registered_vault_filesize': vault_stat.st_size,
        }

    async def test_deletes_only_matching_regular_file(self):
        source_path = self.user_root / 'sample.c4gh'
        vault_path = self.vault_root / 'EGAF000000001'
        source_path.write_bytes(b'inbox payload')
        vault_path.write_bytes(b'vault payload')
        config = FakeConfig(
            self.inbox_root,
            self.vault_root,
            [self.make_row(source_path, vault_path)],
        )

        summary = await cleanup.run_once(config, 90, False, batch_size=25)

        self.assertFalse(source_path.exists())
        self.assertEqual(summary['deleted'], 1)
        self.assertEqual(config.db.connection.fetch_args, (90, 25))
        self.assertEqual(config.db.connection.executed[0][1], (7,))

    async def test_dry_run_keeps_file(self):
        source_path = self.user_root / 'sample.c4gh'
        vault_path = self.vault_root / 'EGAF000000001'
        source_path.write_bytes(b'inbox payload')
        vault_path.write_bytes(b'vault payload')
        config = FakeConfig(
            self.inbox_root,
            self.vault_root,
            [self.make_row(source_path, vault_path)],
        )

        summary = await cleanup.run_once(config, 90, True)

        self.assertTrue(source_path.exists())
        self.assertEqual(summary['skipped'], 1)
        self.assertEqual(config.db.connection.executed, [])

    async def test_rejects_symlink_without_deleting_target(self):
        target_path = self.user_root / 'other.c4gh'
        source_path = self.user_root / 'sample.c4gh'
        vault_path = self.vault_root / 'EGAF000000001'
        target_path.write_bytes(b'inbox payload')
        source_path.symlink_to(target_path)
        vault_path.write_bytes(b'vault payload')
        config = FakeConfig(
            self.inbox_root,
            self.vault_root,
            [self.make_row(source_path, vault_path)],
        )

        summary = await cleanup.run_once(config, 90, False)

        self.assertTrue(source_path.is_symlink())
        self.assertTrue(target_path.exists())
        self.assertEqual(summary['errors'], 1)
        self.assertIn('symbolic links', config.db.connection.executed[0][1][1])

    async def test_rejects_symlinked_user_directory(self):
        external_root = self.inbox_root.parent / 'external-user-directory'
        external_root.mkdir()
        source_path = external_root / 'sample.c4gh'
        vault_path = self.vault_root / 'EGAF000000001'
        source_path.write_bytes(b'inbox payload')
        vault_path.write_bytes(b'vault payload')
        self.user_root.rmdir()
        self.user_root.symlink_to(external_root, target_is_directory=True)
        config = FakeConfig(
            self.inbox_root,
            self.vault_root,
            [self.make_row(source_path, vault_path)],
        )
        config.db.connection.rows[0]['filepath'] = '/sample.c4gh'

        summary = await cleanup.run_once(config, 90, False)

        self.assertTrue(source_path.exists())
        self.assertEqual(summary['errors'], 1)
        self.assertIn('symbolic links', config.db.connection.executed[0][1][1])

    async def test_rejects_vault_size_mismatch(self):
        source_path = self.user_root / 'sample.c4gh'
        vault_path = self.vault_root / 'EGAF000000001'
        source_path.write_bytes(b'inbox payload')
        vault_path.write_bytes(b'vault payload')
        row = self.make_row(source_path, vault_path)
        row['registered_vault_filesize'] += 1
        config = FakeConfig(self.inbox_root, self.vault_root, [row])

        summary = await cleanup.run_once(config, 90, False)

        self.assertTrue(source_path.exists())
        self.assertEqual(summary['errors'], 1)
        self.assertIn('Vault file size', config.db.connection.executed[0][1][1])


if __name__ == '__main__':
    unittest.main()
