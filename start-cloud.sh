#!/bin/bash

kill %1 %2 %3 %4 %4
kill -SIGINT $$
sleep 2
rm -R ~/bmw/*
sleep 2
nfdc cs erase /
sleep 2
rm /home/brewski/.ndn/ndn-python-repo/sqlite3.db
sleep 2
ndn-python-repo > /dev/null 2>&1 &
sleep 5
python3 update-repo-file.py &
sleep 5
./psync-start psync / &