if [ -f ser ]; then
    rm ser
fi

echo "Start compiling server.c"
gcc -Wall -o ser server.c
echo "Finish compiling"

if [ -f cli ]; then
    rm cli
fi

echo "Start compiling client.c"
gcc -Wall -o cli client.c
echo "Finish compiling"

echo "Remove *~"
rm *~
