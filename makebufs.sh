#!/bin/sh

# Example file to generate the buffers

dd if=/dev/zero of=64M_file bs=1048576 count=64
dd if=/dev/zero of=192M_file bs=1048576 count=192
dd if=/dev/zero of=384M_file bs=1048576 count=384
touch outputfile
