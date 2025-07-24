#!/bin/bash

set -e  # exit if any command fails

echo "[INFO] Stopping previous psync processes if any..."
pkill -f "psync-start" > /dev/null 2>&1 || true
pkill -f "update-repo-file.py" > /dev/null 2>&1 || true
pkill -f "ndn-python-repo" > /dev/null 2>&1 || true

sleep 2

echo "[INFO] Cleaning repo data..."
rm -rf ~/bmw/* > /dev/null 2>&1 || true
nfdc cs erase / > /dev/null 2>&1
rm -f /home/brewski/.ndn/ndn-python-repo/sqlite3.db > /dev/null 2>&1 || true

echo "[INFO] Starting ndn-python-repo..."
ndn-python-repo > /dev/null 2>&1 &
sleep 5

echo "[INFO] Starting update-repo-file.py..."
python3 update-repo-file.py &
sleep 5

echo "[INFO] Starting psync-start..."
./psync-start psync &
echo "[INFO] All processes started successfully."