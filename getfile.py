#!/usr/bin/env python3
"""
    NDN Repo getfile example.
    This code example code has been adapted by WJ.
    - Skip saving version component to file

    @Author jonnykong@cs.ucla.edu
"""

import argparse
import logging
from ndn.app import NDNApp
from ndn.encoding import Name, Component
from ndn_python_repo.clients import GetfileClient
from pathlib import Path
import asyncio

import socket

#The following detect wether I am on my laptop or rpi node
# Define both primary and fallback paths
PRIMARY_PATH = Path("/home/brewski/bmw")
FALLBACK_PATH = Path("/home/brewski/masters/bmw")
if PRIMARY_PATH.exists(): 
    SAVE_BASE_PATH = PRIMARY_PATH 
    SOCKET_PATH = "/home/brewski/tmp/ndn-fetch.sock"
else: 
    SAVE_BASE_PATH = FALLBACK_PATH # universal save location
    SOCKET_PATH = "/home/brewski/masters/tmp/ndn-fetch.sock"

async def run_getfile_client(app: NDNApp, **kwargs):
    """
    Async helper function to run the GetfileClient.
    This function is necessary because it's responsible for calling app.shutdown().
    """
    client = GetfileClient(app, kwargs['repo_name'])

    # Strip version component from local file name
    ndn_name = kwargs['name_at_repo']

    if Component.get_type(ndn_name[-1]) == Component.TYPE_TIMESTAMP:
        base_name = ndn_name[:-1]
    else:
        base_name = ndn_name

    relative_path = Path("/".join(Name.to_str(base_name).split('/')[1:]))  # Skip leading empty ""
    local_filepath = SAVE_BASE_PATH / relative_path
    local_filepath.parent.mkdir(parents=True, exist_ok=True)  # Ensure folders exist

    # try:
    #     sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) # notify socket of update fetch as to not invoke watcher. Do this before file write
    #     sock.connect(str(SOCKET_PATH))
    #     sock.send(str(local_filepath.resolve()).encode())
    #     sock.close()
    # except Exception as e:
    #     print(f"[Socket Notify Error] {e}")

    # await asyncio.sleep(0.1)  # 100ms is enough
    
    # await client.fetch_file(kwargs['name_at_repo'], local_filename=str(local_filepath), overwrite=True)

    # Step 1: LOCK before fetch
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        sock.connect(str(SOCKET_PATH))
        sock.send(f"LOCK:{local_filepath.resolve()}".encode())
        sock.close()
    except Exception as e:
        print(f"[Socket Notify Error] LOCK: {e}")

    # Step 2: Fetch file
    try:
        await client.fetch_file(kwargs['name_at_repo'], local_filename=str(local_filepath), overwrite=True)
    except Exception as e:
        print(f"[Fetch Error] {e}")
    finally:
        # Step 3: UNLOCK after fetch
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            sock.connect(str(SOCKET_PATH))
            sock.send(f"UNLOCK:{local_filepath.resolve()}".encode())
            sock.close()
        except Exception as e:
            print(f"[Socket Notify Error] UNLOCK: {e}")
    app.shutdown()


def main():
    parser = argparse.ArgumentParser(description='getfile')
    parser.add_argument('-r', '--repo_name',
                        required=True, help='Name of repo')
    parser.add_argument('-n', '--name_at_repo',
                        required=True, help='Name used to store file at Repo')
    args = parser.parse_args()

    logging.basicConfig(format='[%(asctime)s]%(levelname)s:%(message)s',
                        datefmt='%Y-%m-%d %H:%M:%S',
                        level=logging.INFO)

    app = NDNApp()
    try:
        app.run_forever(
            after_start=run_getfile_client(app,
                                           repo_name=Name.from_str(args.repo_name),
                                           name_at_repo=Name.from_str(args.name_at_repo)))
    except FileNotFoundError:
        print('Error: could not connect to NFD.')



if __name__ == '__main__':
    main()
