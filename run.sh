VER=6.11
docker run --rm -it -e DISPLAY=:1 -v /tmp/.X11-unix:/tmp/.X11-unix:rw klee-coreutils:${VER} bash
