#!/bin/sh
mkdir -p $(dirname "$2")
{ \
  echo "static unsigned const char $(basename "$1" .raw)_raw[] = {" && \
  hexdump -v -e '8/1 "0x%02x, "' -e '"\n"' "$1" | sed -e 's/0x  ,//g' && \
  echo "};"; \
} > "$2"

