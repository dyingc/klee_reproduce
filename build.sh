VER=6.11
docker build --build-arg CU_VER=${VER} -t klee-coreutils:${VER} .
