#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: $0 [pid]"
    exit
fi

kill -s USR2 $1
