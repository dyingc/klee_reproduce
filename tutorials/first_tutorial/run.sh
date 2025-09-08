set -euo pipefail

VER=6.11
IMAGE_NAME="klee-coreutils:${VER}"

docker run --rm --name "klee-dev-${VER}" -it \
  -e DISPLAY=:1 \
  --ulimit stack=-1:-1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -w /tmp/klee_src/examples/get_sign/ \
  "${IMAGE_NAME}" \
  bash -lc 'clang -I ../../include -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone get_sign.c; \
    echo "✅ compile done"; \
    klee get_sign.bc; \
    echo "✅ run done"; \
    exec bash'