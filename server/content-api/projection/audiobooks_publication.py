"""Offline D1 import SQL from a verified CC0 audiobook bake; never connects to Cloudflare.

Follows the projection/packed.py pattern: a standalone, deterministic SQL artifact,
sized below the per-statement D1 ceiling, generated outside the publication runtime.
The library revision and ready flag remain the publisher's responsibility.
"""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3

TABLES = ("works","people","series","work_authors","recordings",
          "recording_narrators","work_series","recording_asins","recording_isbns","search_fts")
MAX_STATEMENT_BYTES = 94000
INSERT_CHUNK_ROWS = 64


def sha256(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda:f.read(4*1024*1024),b""):
            h.update(block)
    return h.hexdigest()


def escape_name(value):
    return '"' + value.replace('"','""') + '"'


def export(source, output):
    manifest_path=source.with_suffix(".manifest.json")
    report_path=output.with_suffix(".report.json")
    if output.exists() or report_path.exists():
        raise ValueError("Refusing to overwrite SQL or report")
    if not source.is_file() or not manifest_path.is_file():
        raise ValueError("Verified source SQLite and manifest required")
    manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
    src_hash=sha256(source)
    if manifest.get("format")!="colosseum-audiobooks-cc0-v1" or manifest.get("sqlite_sha256")!=src_hash:
        raise ValueError("Catalogue hash or bake version mismatch")
    if manifest.get("rights")!=["CC0-1.0"]:
        raise ValueError("Non-CC0 catalogue")
    if set(manifest.get("tables",{}))!=set(TABLES):
        raise ValueError("Unexpected table inventory")
    for item in manifest["tables"].values():
        if item.get("licenses")!=["CC0-1.0"]:
            raise ValueError("Non-CC0 table detected")
        for original in item.get("columns",{}).values():
            if not original or any(part.split(".")[0] not in manifest["cc0_sources"] for part in original):
                raise ValueError("Unknown column provenance")
    output.parent.mkdir(parents=True,exist_ok=True)
    records={}
    statements=0
    max_size=0
    try:
        with closing(sqlite3.connect(source.resolve().as_uri()+"?mode=ro&immutable=1",uri=True)) as db,output.open("w",encoding="utf-8",newline="\n") as sql:
            metadata={row[0]:(row[1],row[2]) for row in db.execute(
                "SELECT name,type,sql FROM sqlite_master WHERE sql IS NOT NULL")}
            all_table_names={name for name,(kind,_) in metadata.items() if kind=="table"}
            allowed=set(TABLES)|{name for name in all_table_names if name.startswith("search_fts_")}
            if all_table_names!=allowed:
                raise ValueError("Unexpected table or expressive sidecar in bake")
            if db.execute("PRAGMA user_version").fetchone()[0]!=1:
                raise ValueError("Unsupported projection version")
            sql.write("-- Colosseum Audiobooks CC0; source sha256: "+src_hash+"\n")
            sql.write("PRAGMA user_version=1;\n")
            for name in TABLES:
                if name not in metadata or metadata[name][0]!="table":
                    raise ValueError("Missing source table: "+name)
                ddl=metadata[name][1]
                if not ddl or "VIRTUAL TABLE" in ddl.upper() and name!="search_fts":
                    raise ValueError("Unsupported schema: "+name)
                sql.write(ddl+";\n")
            for name in TABLES:
                columns=[r[1] for r in db.execute("PRAGMA table_info("+escape_name(name)+")")]
                if not columns:
                    raise ValueError("Missing columns: "+name)
                select="SELECT "+",".join("quote("+escape_name(column)+")" for column in columns)
                select+=" FROM "+escape_name(name)+" ORDER BY rowid"
                prefix="INSERT INTO "+escape_name(name)+" ("+",".join(map(escape_name,columns))+") VALUES "
                buffer=[]
                size=len(prefix.encode("utf-8"))+2
                count=0
                def flush():
                    nonlocal buffer,size,statements,max_size
                    if not buffer: return
                    command=prefix+",".join(buffer)+";\n"
                    command_size=len(command.encode("utf-8"))
                    if command_size>MAX_STATEMENT_BYTES:
                        raise ValueError("SQL statement exceeds safe D1 bound")
                    sql.write(command)
                    statements+=1
                    max_size=max(max_size,command_size)
                    buffer=[]
                    size=len(prefix.encode("utf-8"))+2
                for row in db.execute(select):
                    line="("+",".join(row)+")"
                    encoded=len(line.encode("utf-8"))
                    if encoded+len(prefix.encode("utf-8"))+2>MAX_STATEMENT_BYTES:
                        raise ValueError("Single catalogue row exceeds D1 statement limit")
                    if buffer and (len(buffer)>=INSERT_CHUNK_ROWS or size+encoded+1>MAX_STATEMENT_BYTES):
                        flush()
                    buffer.append(line)
                    size+=encoded+1
                    count+=1
                flush()
                expected=manifest["tables"][name]["rows"]
                if count!=expected:
                    raise ValueError("Row-count mismatch: "+name)
                records[name]=count
            for name,(kind,ddl) in sorted(metadata.items()):
                if kind=="index" and ddl:
                    sql.write(ddl+";\n")
        report={"format":"audiobooks-cc0-publication-v1","source_sha256":src_hash,
                "source_snapshot_id":manifest["source_snapshot_id"],"rights":["CC0-1.0"],
                "sql_sha256":sha256(output),"sql_bytes":output.stat().st_size,
                "max_statement_bytes":max_size,"statements":statements,"rows":records,
                "import_qualified":False}
        report_path.write_text(json.dumps(report,sort_keys=True,indent=2)+"\n",encoding="utf-8",newline="\n")
        print(json.dumps(report,sort_keys=True))
    except BaseException:
        output.unlink(missing_ok=True)
        report_path.unlink(missing_ok=True)
        raise


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    args=p.parse_args()
    export(args.source,args.output)


if __name__=="__main__":
    main()
