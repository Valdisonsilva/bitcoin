#!/bin/sh
mkdir -p $(dirname "$2")
{ \
  echo "namespace json_tests{" && \
  echo "static unsigned const char $(basename "$1" .json)[] = {" && \
  hexdump -v -e '8/1 "0x%02x, "' -e '"\n"' "$1" | sed -e 's/0x  ,//g' && \
  echo "};};"; \
} > "$2"

