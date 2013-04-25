#!/bin/bash

echo "Building scanner";
gcc -Wall -lm -std=c99 -l json -lzmq -pthread -I/usr/include/json/ -o scanner scanner.c
echo "Building client";
gcc -Wall -lzmq -o test-client test-client.c

exit 0;
