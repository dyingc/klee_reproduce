set -euo pipefail

VER=6.11
IMAGE_NAME="klee-coreutils:${VER}"

docker run --rm --name "klee-dev-${VER}" -it \
  -e DISPLAY=:1 \
  --ulimit stack=-1:-1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v $(pwd)/sort.c:/tmp/klee_src/examples/sort/sort.c:ro \
  -w /tmp/klee_src/examples/sort/ \
  "${IMAGE_NAME}" \
  bash -lc 'clang -I ../../include -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone sort.c; \
    echo "✅ compile done"; \
    time klee --only-output-states-covering-new --solver-backend=stp sort.bc; \
    echo "✅ run done"; \
    exec bash'