#!/bin/bash

gcc -std=c99 -l json -I/usr/include/json/ -o scanner scanner.c

exit 0;
