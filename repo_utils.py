'''
Commands used in the repo-sync

@author Waldo Jordaan
'''
import sqlite3
from pathlib import Path
from ndn.encoding.name import Name, Component

REPO_DB_PATH = Path.home() / ".ndn/ndn-python-repo/sqlite3.db"

def parse_components(blob):
    offset = 0
    components = []

    while offset < len(blob):
        t = blob[offset]
        l = blob[offset + 1]
        v = blob[offset + 2 : offset + 2 + l]
        components.append((t, l, v))
        offset += 2 + l

    return components

def getLatestVersion(prefix: str):
    '''
    Gets the latest versioned/timestamped of prefix
    a.t.o.w only compares and gets latest timestamp
    '''
    try:
        conn = sqlite3.connect(REPO_DB_PATH)
        cursor = conn.cursor()
        cursor.execute("SELECT key FROM data")
        rows = cursor.fetchall()
        conn.close()
    except Exception as e:
        print(f"[DB Error] Could not read keys from repo: {e}")
        return None

    timestamped = []
    for (key_blob,) in rows:
        try:
            components = parse_components(key_blob)
            if not components:
                continue

            name_parts = []
            timestamp_value = None

            for t, l, v in components:
                if t == Component.TYPE_GENERIC:  # Generic NameComponent
                    name_parts.append(v.decode('utf-8', errors='ignore'))
                elif t == Component.TYPE_VERSION:  # Version
                    name_parts.append(f"v={int.from_bytes(v, 'big')}")
                elif t == Component.TYPE_TIMESTAMP:  # Timestamp
                    ts = int.from_bytes(v, 'big')
                    name_parts.append(f"t={ts}")
                    timestamp_value = ts
                #elif t == Component.TYPE_SEGMENT:  # Segment
                #    name_parts.append(f"seg={int.from_bytes(v, 'big')}")
                #else:
                    # Optional: handle or skip unknown types
                    #name_parts.append(f"type{t}={v.hex()}")

            if timestamp_value is not None:
                full_name_str = "/" + "/".join(name_parts)
                if full_name_str.startswith(prefix):
                    timestamped.append((timestamp_value, full_name_str))

        except Exception as e:
            print("Skip: could not parse components:", e)

    if not timestamped:
        if __name__ == "__main__":
            print(f"[Info] No timestamped names found under prefix: {prefix}")
        return None

    # Compare only by timestamp value
    _, latest_name = max(timestamped, key=lambda x: x[0])
    if __name__ == "__main__":
        print(f"[Latest Timestamped Name] Found: {latest_name}")
    return latest_name