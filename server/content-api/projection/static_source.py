"""Local-only SQL oracle for static publication. No D1 import/SQL is generated."""
import argparse, json, sqlite3
from contextlib import closing
from pathlib import Path
from packed import build as pack
from prepare import quote, file_hash


def build(source_dir, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    report = pack(source_dir, output / 'oracle')  # verifies all immutable source hashes
    with closing(sqlite3.connect(output / 'oracle' / 'packed.sqlite')) as src, closing(sqlite3.connect(output / 'static-source.sqlite')) as dst:
        for spec in report['tables']:
            name = spec['view']
            cols = list(src.execute('PRAGMA table_info(' + quote(name) + ')'))
            dst.execute('CREATE TABLE ' + quote(name) + '(' + ','.join(quote(c[1]) + ' ' + (c[2] or 'BLOB') for c in cols) + ')')
            for (payload,) in src.execute('SELECT payload FROM ' + quote(spec['physical']) + ' ORDER BY id'):
                dst.executemany('INSERT INTO ' + quote(name) + ' VALUES(' + ','.join('?' for _ in cols) + ')', json.loads(payload))
            # Natural IDs accelerate local lookups; no hosted index migration.
            if cols[0][1] in ('tt', 'mal_id', 'gcd_id', 'locg_id', 'id', 'post_id'):
                dst.execute('CREATE INDEX ' + quote('key_' + name) + ' ON ' + quote(name) + '(' + quote(cols[0][1]) + ')')
        dst.commit()
        if dst.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
            raise ValueError('Local publication oracle failed integrity')
    result = {'format': 'static-source-v1', 'revision': report['revision'], 'sources': report['sources'], 'sqliteSha256': file_hash(output / 'static-source.sqlite')}
    # The shared pack builder emits this private intermediate as a side effect.
    # It is not an I01 import plan/artifact; leave only the local SQL oracle.
    (output / 'oracle' / 'packed.sql').unlink()
    (output / 'static-source-report.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-dir', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    print(json.dumps(build(a.source_dir, a.output)))
