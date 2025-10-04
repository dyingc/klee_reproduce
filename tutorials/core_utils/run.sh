VER=6.11
PROD="echo"
MODIFIED_PROD="${PROD}_challenge3"

docker run --rm -it -e DISPLAY=:1 \
  --ulimit='stack=-1:-1' \
  -e LLVM_COMPILER=clang \
  -w /home/klee/coreutils-${VER} \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v "$(pwd)/${MODIFIED_PROD}.c:/home/klee/coreutils-${VER}/src/${MODIFIED_PROD}.c:ro" \
  -v "$(pwd)/${MODIFIED_PROD}_harness.c:/home/klee/coreutils-${VER}/src/${MODIFIED_PROD}_harness.c:ro" \
  -v "$(pwd)/Makefile_${MODIFIED_PROD}:/home/klee/coreutils-${VER}/Makefile_${MODIFIED_PROD}:ro" \
  klee-coreutils:${VER} \
  bash -lc '
    # make -f Makefile_${MODIFIED_PROD} clean
    # make -f Makefile_${MODIFIED_PROD} all
    # echo "🎉 All builds finished."
    exec bash
  '