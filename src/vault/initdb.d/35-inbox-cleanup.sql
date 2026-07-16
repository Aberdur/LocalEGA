-- Inbox retention records. These rows are created only after a file has
-- been written, verified and registered in the vault.
CREATE TABLE IF NOT EXISTS private.inbox_cleanup_table (
    id                  BIGSERIAL PRIMARY KEY,
    username            text NOT NULL,
    filepath            text NOT NULL,
    accession_id        text NOT NULL REFERENCES public.file_table(stable_id),
    vault_relative_path text NOT NULL,
    inbox_filesize      bigint NOT NULL,
    inbox_mtime_ns      bigint NOT NULL,
    completed_at        timestamp(6) with time zone NOT NULL DEFAULT now(),
    deleted_at          timestamp(6) with time zone,
    delete_error        text,

    UNIQUE (username, filepath, accession_id, inbox_mtime_ns)
);

CREATE INDEX IF NOT EXISTS inbox_cleanup_pending_idx
    ON private.inbox_cleanup_table (completed_at)
    WHERE deleted_at IS NULL;

CREATE OR REPLACE FUNCTION private.record_inbox_cleanup(
    _username text,
    _filepath text,
    _accession_id text,
    _vault_relative_path text,
    _inbox_filesize bigint,
    _inbox_mtime_ns bigint
)
RETURNS void
LANGUAGE sql
AS $_$
    INSERT INTO private.inbox_cleanup_table (
        username,
        filepath,
        accession_id,
        vault_relative_path,
        inbox_filesize,
        inbox_mtime_ns
    ) VALUES (
        _username,
        _filepath,
        _accession_id,
        _vault_relative_path,
        _inbox_filesize,
        _inbox_mtime_ns
    )
    ON CONFLICT (username, filepath, accession_id, inbox_mtime_ns) DO NOTHING;
$_$;

-- Keep migration self-contained for already initialised Vault databases.
GRANT USAGE ON SCHEMA private TO lega;
GRANT USAGE ON SEQUENCE private.inbox_cleanup_table_id_seq TO lega;
GRANT SELECT,INSERT,UPDATE ON TABLE private.inbox_cleanup_table TO lega;
GRANT EXECUTE ON FUNCTION private.record_inbox_cleanup(text, text, text, text, bigint, bigint) TO lega;
