#!/bin/bash

gcc -std=c99 -l json -lzmq -I/usr/include/json/ -o scanner scanner.c

exit 0;
