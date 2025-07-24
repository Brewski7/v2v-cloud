#!/bin/bash

echo "[INFO] Stopping previous psync processes..."
pkill -f "psync-start" || true
pkill -f "update-repo-file.py" || true
pkill -f "ndn-python-repo" || true
