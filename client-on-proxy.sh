#!/bin/bash

nc -v -x 127.0.0.1:9999 127.0.0.1 "$@" >/dev/null


