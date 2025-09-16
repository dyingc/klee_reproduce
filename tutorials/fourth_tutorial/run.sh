set -euo pipefail

VER=6.11
IMAGE_NAME="klee-coreutils:${VER}"

docker run --rm --name "klee-dev-${VER}" -it \
  -e DISPLAY=:1 \
  --ulimit stack=-1:-1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v $(pwd)/password.c:/tmp/klee_src/examples/password/password.c:ro \
  -w /tmp/klee_src/examples/password/ \
  "${IMAGE_NAME}" \
  bash -lc 'clang -I ../../include -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone password.c; \
    echo "✅ compile done"; \
    time klee --emit-all-errors --only-output-states-covering-new --solver-backend=stp password.bc; \
    echo "✅ run done"; \
    exec bash'
