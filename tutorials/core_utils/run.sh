VER=6.11
PROD="echo"
MODIFIED_PROD="${PROD}_challenge2"

docker run --rm -it -e DISPLAY=:1 \
  --ulimit='stack=-1:-1' \
  -e LLVM_COMPILER=clang \
  -w /home/klee/coreutils-${VER} \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v "$(pwd)/${MODIFIED_PROD}.c:/home/klee/coreutils-${VER}/src/${MODIFIED_PROD}.c:ro" \
  klee-coreutils:${VER} \
  bash -lc '
    set -e
    echo "[*] PWD=$(pwd)"
    mkdir -p obj-llvm/src

    echo "[*] (optional) try make target if exists: src/'"${MODIFIED_PROD}"'"
    ( make -C src '"${MODIFIED_PROD}"' -j"$(nproc)" && echo "✅ make ${MODIFIED_PROD} done" ) || echo "ℹ️ no automake target for '"${MODIFIED_PROD}"' (ok)"

    echo "[*] build ASan native for replay"
    clang -fsanitize=address -g -O0 src/'"${MODIFIED_PROD}"'.c -o obj-llvm/src/'"${MODIFIED_PROD}"'_asan
    echo "✅ ASan native: obj-llvm/src/'"${MODIFIED_PROD}"'_asan"

    echo "[*] build LLVM bitcode (.bc) directly via clang"
    clang -O1 -Xclang -disable-llvm-passes \
      -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__ \
      -emit-llvm -c src/'"${MODIFIED_PROD}"'.c -o obj-llvm/src/'"${MODIFIED_PROD}"'.bc
    echo "✅ Bitcode: obj-llvm/src/'"${MODIFIED_PROD}"'.bc"

    echo "[*] (optional) if a native ELF was built, we can also try extract-bc"
    if [[ -x obj-llvm/src/'"${MODIFIED_PROD}"' ]]; then
      extract-bc obj-llvm/src/'"${MODIFIED_PROD}"'
      echo "✅ extract-bc (ELF→.bc) also done"
    else
      echo "ℹ️ skip extract-bc (no native ELF)"
    fi

    echo "🎉 All builds finished."
    exec bash
  '