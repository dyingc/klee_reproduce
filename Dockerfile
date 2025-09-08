# ======== 第一阶段：在旧 glibc (Debian 9/stretch) 中构建 coreutils ========
FROM debian/eol:stretch AS coreutils_builder
ENV DEBIAN_FRONTEND=noninteractive

# 保险起见，关闭 Valid-Until 检查（EOL 仓库常见做法）
RUN printf 'Acquire::Check-Valid-Until "false";\nAcquire::AllowInsecureRepositories "true";\n' \
      > /etc/apt/apt.conf.d/99no-check-valid && \
    apt-get -o Acquire::Check-Valid-Until=false update

# 安装构建依赖
RUN apt-get install -y --no-install-recommends \
        build-essential wget xz-utils ca-certificates \
        autoconf automake gettext autopoint libtool pkg-config make \
        python3 python3-pip clang llvm gawk libc6-dev \
    && apt-get install -y --no-install-recommends kcachegrind valgrind xvfb x11vnc xfce4 x11-apps dbus-x11 \
    && update-alternatives --install /usr/bin/python python /usr/bin/python3 1 \
    && update-alternatives --install /usr/bin/pip pip /usr/bin/pip3 1 \
    && rm -rf /var/lib/apt/lists/*

# 安装 wllvm 和 tabulate（用于显示`klee-stats klee-last`结果）
RUN pip install --upgrade wllvm tabulate
ARG LLVM_COMPILER=clang
# 用 ARG 控制 coreutils 版本与安装前缀
ARG CU_VER=6.11
ARG homedir=/home/klee

WORKDIR ${homedir}

# 拉源码并解压
RUN wget -O coreutils-${CU_VER}.tar.gz \
        http://ftp.gnu.org/gnu/coreutils/coreutils-${CU_VER}.tar.gz \
    && tar -zxf coreutils-${CU_VER}.tar.gz \
    && rm -f coreutils-${CU_VER}.tar.gz

# 配置、编译并安装（覆盖率开启，不用 NLS）
WORKDIR ${homedir}/coreutils-${CU_VER}
RUN mkdir -p obj-gcov
WORKDIR ${homedir}/coreutils-${CU_VER}/obj-gcov
RUN ../configure --disable-nls \
      CFLAGS="-g -fprofile-arcs -ftest-coverage" \
      --prefix=${PREFIX} \
    && make -j"$(nproc)" \
    && make -C src arch hostname

# 使用 LLVM 编译 coreutils
WORKDIR ${homedir}/coreutils-${CU_VER}
RUN mkdir obj-llvm
WORKDIR ${homedir}/coreutils-${CU_VER}/obj-llvm
RUN export LLVM_COMPILER=clang && \
    CC=wllvm ../configure --disable-nls CFLAGS="-g -O0" \
  && make -j"$(nproc)" \
  && make -C src arch hostname

# 生成 LLVM bitcode 文件
WORKDIR ${homedir}/coreutils-${CU_VER}/obj-llvm/src
RUN find . -executable -type f | xargs -I '{}' extract-bc '{}'

# ======== 第二阶段：拷入 klee/klee:latest 运行环境 ========
FROM klee/klee:latest

# 安装必要工具
USER root

# 暂时移走 kitware 的源
RUN set -eux; \
    mkdir -p /tmp/kitware-src-backup; \
    mv /etc/apt/sources.list.d/*kitware*.list /tmp/kitware-src-backup/ || true; \
    apt-get update; \
    apt-get install -y --no-install-recommends kcachegrind valgrind xvfb x11vnc xfce4 x11-apps dbus-x11; \
    rm -rf /var/lib/apt/lists/*; \
    # 安装完再移回来
    mv /tmp/kitware-src-backup/* /etc/apt/sources.list.d/ 2>/dev/null || true; \
    rmdir /tmp/kitware-src-backup || true

# 在最终阶段也声明一次 ARG，保证路径一致
ARG CU_VER=6.11
ARG homedir=/home/klee

# 拷贝构建产物
COPY --from=coreutils_builder --chown=klee:klee ${homedir}/coreutils-${CU_VER} ${homedir}/coreutils-${CU_VER}

# 设置工作目录
WORKDIR ${homedir}/coreutils-${CU_VER}/obj-llvm/src

# 准备显示 source code
RUN ln -s /tmp/klee-uclibc-130/libc; ln -s /tmp/klee_src

# 准备测试文件
COPY klee-out-0.tar.gz .
RUN tar -zxf klee-out-0.tar.gz && ln -s klee-out-0 klee-last && rm -f klee-out-0.tar.gz

USER klee

