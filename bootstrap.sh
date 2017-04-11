#!/bin/sh
autoreconf -vi

long_rev=$(git rev-parse HEAD)
sed -i "s/@OSAF_LONG_GIT_REV@/$long_rev/" configure
