VER=6.11
docker run --rm -it -e DISPLAY=:1 \
--ulimit='stack=-1:-1' \
-w /home/klee/coreutils-6.11/obj-llvm/src \
-v /tmp/.X11-unix:/tmp/.X11-unix:rw \
klee-coreutils:${VER} \
bash
