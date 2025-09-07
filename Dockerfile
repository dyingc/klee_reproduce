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
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# 用 ARG 控制 coreutils 版本与安装前缀
ARG CU_VER=6.11
ARG PREFIX=/opt/coreutils-${CU_VER}

# 拉源码并解压
RUN wget -O coreutils-${CU_VER}.tar.gz \
        http://ftp.gnu.org/gnu/coreutils/coreutils-${CU_VER}.tar.gz \
    && tar -zxf coreutils-${CU_VER}.tar.gz

# 配置、编译并安装（覆盖率开启，用 NLS）
WORKDIR /build/coreutils-${CU_VER}
RUN mkdir -p obj-gcov
WORKDIR /build/coreutils-${CU_VER}/obj-gcov
RUN ../configure --disable-nls \
      CFLAGS="-g -fprofile-arcs -ftest-coverage" \
      --prefix=${PREFIX} \
    && make -j"$(nproc)" \
    && make install

# ======== 第二阶段：拷入 klee/klee:3.0 运行环境 ========
FROM klee/klee:3.0

# 在最终阶段也声明一次 ARG，保证路径一致
ARG CU_VER=6.11
ARG PREFIX=/opt/coreutils-${CU_VER}

# 拷贝构建产物
COPY --from=coreutils_builder ${PREFIX} ${PREFIX}

# 让该版本在 PATH 中优先（不覆盖系统文件，优先使用你编译的 coreutils）
ENV PATH="${PREFIX}/bin:${PATH}"

# 自检（可选）
RUN ${PREFIX}/bin/ls --version

# WORKDIR
WORKDIR /home/klee
RUN ln -s ${PREFIX} coreutils-${CU_VER}

