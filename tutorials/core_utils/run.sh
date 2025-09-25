VER=6.11
PROD="echo"
MODIFIED_PROD="${PROD}_challenge2"

docker run --rm -it -e DISPLAY=:1 \
    --ulimit='stack=-1:-1' \
    -e LLVM_COMPILER=clang \
    -w /home/klee/coreutils-${VER}/obj-llvm/src \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v $(pwd)/${MODIFIED_PROD}.c:/home/klee/coreutils-${VER}/src/${MODIFIED_PROD}.c:ro \
    -v $(pwd)/rerun_asan.py:/home/klee/coreutils-${VER}/obj-llvm/src/rerun_asan.py:ro \
    klee-coreutils:${VER} \
    bash -lc \
    "\
    cd ..; make -j$(nproc) -C src ${MODIFIED_PROD}; cd -; \
    echo '✅ compile for Ordinary ${MODIFIED_PROD}: done'; \
    cd /home/klee/coreutils-${VER}/; clang -fsanitize=address -g -O0 src/${MODIFIED_PROD}.c -o obj-llvm/src/${MODIFIED_PROD}_asan; cd -; \
    echo '✅ compile for ASan ${MODIFIED_PROD}: done'; \
    extract-bc ${MODIFIED_PROD}; \
    echo '✅ generate ${MODIFIED_PROD}.bc done'; \
    exec bash"
