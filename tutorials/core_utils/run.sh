VER=6.11
PROD="echo"

docker run --rm -it -e DISPLAY=:1 \
    --ulimit='stack=-1:-1' \
    -e LLVM_COMPILER=clang \
    -w /home/klee/coreutils-${VER}/obj-llvm/src \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v $(pwd)/${PROD}_challenge2.c:/home/klee/coreutils-${VER}/src/${PROD}.c:ro \
    -v $(pwd)/rerun_asan.py:/home/klee/coreutils-${VER}/obj-llvm/src/rerun_asan.py:ro \
    klee-coreutils:${VER} \
    bash -lc \
    "\
    cd ..; make -j$(nproc) -C src ${PROD}; cd -; \
    echo '✅ compile for Ordinary ${PROD}: done'; \
    clang -I ../../include -fsanitize=address -g -O0 /home/klee/coreutils-${VER}/src/${PROD}.c -o /home/klee/coreutils-${VER}/obj-llvm/src/${PROD}_asan; \
    echo '✅ compile for ASan ${PROD}: done'; \
    extract-bc ${PROD}; \
    echo '✅ generate ${PROD}.bc done'; \
    exec bash"
