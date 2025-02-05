if [ -z "$1" ]; then
    echo "Usage: $0 [pid]"
    exit
fi

pstree -p $1
