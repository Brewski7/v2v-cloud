import sqlite3
from ndn.encoding import Name

REPO_DB_PATH = "/home/brewski/.ndn/ndn-python-repo/sqlite3.db"

conn = sqlite3.connect(REPO_DB_PATH)
cursor = conn.cursor()
cursor.execute("SELECT key FROM data")
rows = cursor.fetchall()
conn.close()

print("---- Dumping raw keys ----")
for (key_blob,) in rows:
    print("Raw Key : ", key_blob)
    print("Raw key (hex):", key_blob.hex())
    name = Name.from_str(key_blob.hex())
    print("Name: ", name)
    print("NDN Name:", Name.to_str(name))
    try:
        name = Name.from_bytes(key_blob)
        print("Name: ", name)
        print("NDN Name:", Name.to_str(name))
    except Exception as e:
        print("Decode error:", e)
