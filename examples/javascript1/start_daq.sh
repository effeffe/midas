#!/bin/sh

odbedit -c cleanup
mhttpd -D
mlogger -D

echo Please connect to the midas status page at https://localhost:8443
echo If mhttpd failed to start, please run it without "-D" to see any errors.

#end
