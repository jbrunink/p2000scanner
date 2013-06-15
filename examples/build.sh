#!/bin/bash

echo "Building client";
gcc -Wall -lzmq -o scanner-client scanner-client.c

exit;

