"""Periodic cleanup of Inbox files whose archival has completed."""

import argparse
import asyncio
import logging
import os
import stat
import sys
from pathlib import Path, PurePosixPath

LOG = logging.getLogger(__name__)

CANDIDATES_QUERY = """
SELECT c.id, c.username, c.filepath, c.accession_id, c.vault_relative_path,
       c.inbox_filesize, c.inbox_mtime_ns, c.completed_at,
       f.relative_path AS registered_vault_relative_path,
       f.payload_size AS registered_vault_filesize
FROM private.inbox_cleanup_table AS c
JOIN private.file_table AS f ON f.stable_id = c.accession_id
WHERE c.deleted_at IS NULL
  AND c.completed_at <= now() - ($1::bigint * interval '1 day')
-- Process never-attempted rows first so persistent errors cannot starve newer
-- valid candidates when a batch limit is in use.
ORDER BY (c.delete_error IS NOT NULL), c.completed_at, c.id
LIMIT $2::bigint
"""

MARK_DELETED_QUERY = """
UPDATE private.inbox_cleanup_table
SET deleted_at = now(), delete_error = NULL
WHERE id = $1 AND deleted_at IS NULL
"""

MARK_ERROR_QUERY = """
UPDATE private.inbox_cleanup_table
SET delete_error = $2
WHERE id = $1 AND deleted_at IS NULL
"""


def env_bool(name, default):
    value = os.getenv(name)
    if value is None:
        return default
    value = value.strip().lower()
    if value in {'1', 'true', 'yes', 'on'}:
        return True
    if value in {'0', 'false', 'no', 'off'}:
        return False
    raise ValueError(
        f'{name} must be one of true/false, yes/no, on/off or 1/0'
    )


def env_positive_int(name, default):
    value = os.getenv(name, str(default))
    try:
        parsed = int(value)
    except ValueError as error:
        raise ValueError(f'{name} must be an integer') from error
    if parsed < 1:
        raise ValueError(f'{name} must be greater than zero')
    return parsed


def setup_persistent_log():
    """Keep an audit trail even when the container runtime rotates logs."""
    path = Path(os.getenv(
        'INBOX_CLEANUP_LOG_FILE',
        '/var/log/local/localega/app/cleanup.log',
    ))
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        handler = logging.FileHandler(path)
        handler.setFormatter(logging.Formatter(
            '%(asctime)s %(levelname)s %(name)s %(message)s',
        ))
        LOG.addHandler(handler)
    except OSError as error:
        LOG.warning('Persistent cleanup log is unavailable at %s: %s', path, error)


def safe_relative_path(value):
    path = PurePosixPath(value.lstrip('/'))
    if (not value or path == PurePosixPath('.') or path.is_absolute() or
            '..' in path.parts):
        raise ValueError(f'unsafe relative path: {value!r}')
    return path


def safe_username(value):
    if not value or '/' in value or value in {'.', '..'}:
        raise ValueError(f'unsafe Inbox username: {value!r}')
    return value


def path_below(root, relative_path):
    root = root.resolve()
    path = root.joinpath(*relative_path.parts)
    try:
        path.relative_to(root)
    except ValueError as error:
        raise ValueError(f'path escapes root: {relative_path}') from error
    if path == root:
        raise ValueError(f'path escapes root: {relative_path}')
    return path


def regular_file_stat(root, path):
    """Return lstat data while rejecting symlinks in every path component."""
    root = root.resolve()
    relative_path = path.relative_to(root)
    current = root
    parts = relative_path.parts

    for index, part in enumerate(parts):
        current = current / part
        current_stat = current.lstat()
        if stat.S_ISLNK(current_stat.st_mode):
            raise ValueError(f'symbolic links are not allowed: {current}')
        if index < len(parts) - 1 and not stat.S_ISDIR(current_stat.st_mode):
            raise ValueError(f'path component is not a directory: {current}')

    if not stat.S_ISREG(current_stat.st_mode):
        raise ValueError(f'path is not a regular file: {path}')
    return current_stat


async def run_once(config, retention_days, dry_run, batch_size=1000):
    connection = config.db
    if not connection.connection:
        await connection.connect()

    rows = await connection.connection.fetch(
        CANDIDATES_QUERY,
        retention_days,
        batch_size,
    )
    inbox_root = Path(config.get('inbox', 'location', raw=True) % '')
    vault_root = Path(config.get('vault', 'location', raw=True))
    summary = {'candidates': len(rows), 'deleted': 0, 'skipped': 0, 'errors': 0}

    for row in rows:
        try:
            username = safe_username(row['username'])
            inbox_path = path_below(
                inbox_root,
                PurePosixPath(username) / safe_relative_path(row['filepath']),
            )
            vault_relative_path = safe_relative_path(row['vault_relative_path'])
            vault_path = path_below(vault_root, vault_relative_path)

            if row['vault_relative_path'] != row['registered_vault_relative_path']:
                raise ValueError('vault path no longer matches the registered accession')
            vault_stat = regular_file_stat(vault_root, vault_path)
            if vault_stat.st_size != row['registered_vault_filesize']:
                raise ValueError('Vault file size no longer matches the registered accession')

            try:
                source_stat = regular_file_stat(
                    inbox_root,
                    inbox_path,
                )
            except FileNotFoundError:
                LOG.info('Skipping already absent Inbox file for %s: %s', row['accession_id'], inbox_path)
                await connection.connection.execute(MARK_DELETED_QUERY, row['id'])
                summary['skipped'] += 1
                continue

            if (source_stat.st_size != row['inbox_filesize'] or
                    source_stat.st_mtime_ns != row['inbox_mtime_ns']):
                raise ValueError('Inbox file no longer matches the archived source')

            if dry_run:
                LOG.info('DRY RUN: would delete Inbox file for %s: %s', row['accession_id'], inbox_path)
                summary['skipped'] += 1
                continue

            inbox_path.unlink()
            await connection.connection.execute(MARK_DELETED_QUERY, row['id'])
            LOG.info('Deleted Inbox file for %s: %s', row['accession_id'], inbox_path)
            summary['deleted'] += 1
        except Exception as error:
            LOG.error('Could not clean Inbox record %s: %s', row['id'], error)
            await connection.connection.execute(MARK_ERROR_QUERY, row['id'], str(error))
            summary['errors'] += 1

    LOG.info('Inbox cleanup summary: %s', summary)
    return summary


async def main(conf_file, once):
    from .utils import conf

    config = conf.Configuration(conf_file)
    setup_persistent_log()
    retention_days = env_positive_int('INBOX_RETENTION_DAYS', 90)
    interval_hours = env_positive_int('INBOX_CLEANUP_INTERVAL_HOURS', 24)
    batch_size = env_positive_int('INBOX_CLEANUP_BATCH_SIZE', 1000)
    dry_run = env_bool('INBOX_CLEANUP_DRY_RUN', True)

    while True:
        await run_once(config, retention_days, dry_run, batch_size)
        if once:
            return
        await asyncio.sleep(interval_hours * 3600)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('conf_file', help='LocalEGA handler configuration file')
    parser.add_argument('--once', action='store_true', help='run one cleanup cycle and exit')
    args = parser.parse_args()
    try:
        asyncio.run(main(args.conf_file, args.once))
    except Exception as error:
        LOG.error('Inbox cleanup failed: %r', error, exc_info=True)
        sys.exit(2)
