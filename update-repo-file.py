#!/usr/bin/env python3
'''
    Watches a specified repo. If update any change occurs to a file it goes through the update process:
        - Deletes the original file from the repo 
        - Clears it from content store in the NFD, otherwise it gets uploaded from cache
        - Inserts the new file (updated file) into the repo
            - ndn-python-repo only has insert and delete commands, no update command a.o.w.
            - Update command will entail updating only affected segment.
        - Does a PSync update of the file with a new version (timestamped) 

    ToDo: Add change detections, i.e. if file is deleted, the process will continue as normal and other nodes want a new file that is deleted

    @author Waldo Jordaan
'''

import time
import subprocess
from pathlib import Path
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler, FileModifiedEvent, FileCreatedEvent, FileMovedEvent

import sqlite3
from ndn.encoding import parse_data
from ndn.encoding.name import Name, Component

import socket
import threading
import atexit

from repo_utils import getLatestVersion

# Thread synchronization primitives for shared structures
LOCK = threading.Lock()
# Lock to ensure only one file update runs at a time
FILE_UPDATE_LOCK = threading.Lock()

from threading import Timer
import asyncio

# CONFIGURATION
#The following detect wether I am on my laptop or rpi node
# Define both primary and fallback paths
PRIMARY_PATH = Path("/home/brewski")
FALLBACK_PATH = Path("/home/brewski/masters")

if FALLBACK_PATH.exists():              #code run on laptop
    WATCH_DIR = FALLBACK_PATH / "bmw"
    SOCKET_PATH = FALLBACK_PATH / "tmp/ndn-fetch.sock"
else:
    WATCH_DIR = PRIMARY_PATH / "bmw"    #code run on rpi node
    SOCKET_PATH = PRIMARY_PATH / "tmp/ndn-fetch.sock"

# Automatically delete the socket file on exit (clean shutdown)
atexit.register(lambda: Path(SOCKET_PATH).unlink(missing_ok=True))

# File lock registry to ignore during writes
FETCHED_LOCKS: set[Path] = set()
FETCHED_FROM_PSYC: set[Path] = set()


#debounce the file events
DEBOUNCE_TIMERS: dict[Path, Timer] = {}
DEBOUNCE_DELAY = 2.0  # seconds

REPO_DB_PATH = PRIMARY_PATH / ".ndn/ndn-python-repo/sqlite3.db" # this is the same on rpi node and laptop

REPO_NAME = "/bmw"
PUTFILE = "./putfile.py"
DELFILE = "./delfile.py"
PSYNC_UPDATE = "./psync-update"
PSYNC_REPO_NAME = "psync"


def erase_cs(name: str):
    subprocess.run(["nfdc", "cs", "erase", name], check=False) # TODO: Needs to delete the version number or all associated with prefix


def delete_from_repo(name: str):
    print("String to delete: ", name)
    subprocess.run([
        "python3", DELFILE, 
        "-r", REPO_NAME,
        "-n", name
    ], check=True)


def insert_to_repo(filepath: Path, name: str, timestamp: int):
    subprocess.run([
        "python3", PUTFILE,
        "-r", REPO_NAME,
        "-f", str(filepath),
        "-n", name,
        "--timestamp", str(timestamp)
    ], check=True)

def wait_until_repo_ready(name: str, timestamp: int, interval=0.1):
    """
    Waits indefinitely until the repo has inserted the file version (based on timestamp).
    """
    versioned_name = name + f"/t={timestamp}"
    print(f"[Wait] Waiting for repo to commit: {versioned_name}")
    while True:
        latest = getLatestVersion(name)
        if latest and latest.endswith(f"t={timestamp}"):
            print(f"[Ready] Repo insert confirmed: {versioned_name}")
            return
        time.sleep(interval)



def notify_update(name: str):
    subprocess.run([PSYNC_UPDATE, PSYNC_REPO_NAME, name], check=False)


def start_fetch_listener():
    if Path(SOCKET_PATH).exists():
        Path(SOCKET_PATH).unlink()  # Clean stale socket

    server = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    server.bind(str(SOCKET_PATH))

    def listener():
        while True:
            try:
                data, _ = server.recvfrom(1024)
                msg = data.decode()
                if msg.startswith("LOCK:"):
                    path = Path(msg[5:]).resolve()
                    with LOCK:
                        FETCHED_LOCKS.add(path)
                    print(f"[Socket] LOCKED: {path}")
                elif msg.startswith("UNLOCK:"):
                    path = Path(msg[7:]).resolve()
                    with LOCK:
                        FETCHED_LOCKS.discard(path)
                        FETCHED_FROM_PSYC.add(path)
                    print(f"[Socket] UNLOCKED: {path}")
            except Exception as e:
                print(f"[Socket Error] {e}")

    threading.Thread(target=listener, daemon=True).start()


class ChangeHandler(FileSystemEventHandler):
    def on_created(self, event):
        self.handle_change(event)

    def on_modified(self, event):
        self.handle_change(event)

    def on_moved(self, event):
        # Use event.dest_path to get new file location
        self.handle_change(event)

    def handle_change(self, event):
        if not event.is_directory:
            # Handle both modify/create and move events
            file_path = Path(event.dest_path if isinstance(event, FileMovedEvent) else event.src_path).resolve()

            if file_path.name.startswith('.'): # do not handle .temp files
                return
            
            #if file_path in FETCHED_LOCKS:
            #    print(f"[Skip] File is locked from PSync for writing: {file_path}")
            #    return

            with LOCK:
                if file_path in DEBOUNCE_TIMERS:
                    DEBOUNCE_TIMERS[file_path].cancel()
                
            # Start a new debounce timer
            timer = Timer(DEBOUNCE_DELAY, debounce_trigger, args=[file_path])
            with LOCK:
                DEBOUNCE_TIMERS[file_path] = timer
            timer.start()


def debounce_trigger(file_path: Path):
    with LOCK:
        # Remove the timer entry now that it fired
        DEBOUNCE_TIMERS.pop(file_path, None)

        if file_path in FETCHED_LOCKS:
            print(f"[Skip] Locked file after debounce: {file_path}")
            return
        
        if file_path in FETCHED_FROM_PSYC:
            print(f"[Skip] File was fetched via PSync: {file_path}")
            FETCHED_FROM_PSYC.discard(file_path) # this is needed as for some reason after unlock is given, there is one more file event triggered!!!
            return

    print(f"[Handle] File stabilized after debounce: {file_path}")
    process_file_change(file_path)


def process_file_change(file_path: Path):
    
    # use full relative path in name and not just the file name. This keeps subdirectories intact
    name = "/" + str(file_path.relative_to(WATCH_DIR)).replace("\\", "/") 

    try:
        print(f"[Update Detected] {file_path} -> {name}")
        with FILE_UPDATE_LOCK:
            latest_name = getLatestVersion(name)
            if latest_name: delete_from_repo(latest_name) 
            else: print("No version found") 
            erase_cs(name)

            ts = int(time.time())
            insert_to_repo(file_path, name, ts)
            
            wait_until_repo_ready(name, ts)
            
            versioned_name = name + f"/t={ts}"
            notify_update(versioned_name)

    except Exception as e:
        print(f"[Error] Failed to update {name}: {e}")


if __name__ == "__main__":
    observer = Observer()
    handler = ChangeHandler()
    observer.schedule(handler, str(WATCH_DIR), recursive=True)

    start_fetch_listener()
    observer.start()

    print(f"[Watching] Folder: {WATCH_DIR.resolve()}")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
    observer.join()
