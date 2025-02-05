if [ -z "$1" ]; then
    echo "Usage: $0 [pid]"
    exit
fi

kill -s USR1 $1
