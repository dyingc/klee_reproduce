# KLEE 符号执行实验完整复现指南

- [OSDI'08 Coreutils Experiments](https://klee-se.org/releases/docs/v2.0/docs/coreutils-experiments/)
- [Tutorial on How to Use KLEE to Test GNU Coreutils](https://klee-se.org/releases/docs/v2.0/tutorials/testing-coreutils/)
- [Using KLEE with Docker](https://klee-se.org/docker/)

本文档综合分析了KLEE官方文档的三个关键资源，提供了从环境配置到Coreutils测试的完整实验复现步骤。**优先推荐使用Docker方式，这是当前最简单、最可靠的KLEE使用方法。**

## 环境配置和安装方法

### 方法一：Docker安装（推荐）

**Docker是当前KLEE最简单的使用方式，KLEE 3.0版本已完全支持Docker容器运行。**

#### 系统要求
- 支持Docker的操作系统（Ubuntu、macOS、Windows）
- 至少4GB可用内存
- 10GB以上磁盘空间

#### 快速启动
```bash
# 拉取最新KLEE Docker镜像
$ docker pull klee/klee:latest

# 启动交互式容器
$ docker run --rm -ti --ulimit='stack=-1:-1' klee/klee:latest
```

**重要参数说明：**
- `--rm`: 退出时自动删除容器
- `-ti`: 提供交互式终端
- `--ulimit='stack=-1:-1'`: 设置无限制栈大小，避免KLEE运行时栈溢出

#### 持久化容器使用
```bash
# 创建命名容器保存工作内容
$ docker run -ti --name=klee_experiments --ulimit='stack=-1:-1' klee/klee:3.0

# 重启容器
$ docker start -ai klee_experiments

# 删除容器（完成实验后）
$ docker rm klee_experiments
```

#### 文件系统挂载
```bash
# 挂载主机目录到容器
$ docker run --rm -ti --ulimit='stack=-1:-1' \
    --volume=$(pwd):/work \
    klee/klee:3.0
```

### 方法二：本地编译安装

#### 系统依赖安装
```bash
# Ubuntu/Debian系统
$ sudo apt-get install -y build-essential curl libcap-dev git cmake \
    libncurses5-dev python3-minimal python3-pip unzip libtcmalloc-minimal4 \
    libgoogle-perftools-dev libsqlite3-dev doxygen

# 安装LLVM 13（推荐版本）
$ sudo apt-get install -y clang-13 llvm-13-dev llvm-13-tools
```

#### WLLVM安装
```bash
$ pip install --upgrade wllvm
$ export LLVM_COMPILER=clang
# 添加到~/.bashrc使配置持久化
$ echo 'export LLVM_COMPILER=clang' >> ~/.bashrc
```

## KLEE环境验证

### Docker环境验证
```bash
# 在容器内验证KLEE版本
klee@container:~$ klee --version
# 预期输出：KLEE 3.0 (https://klee.github.io)

# 验证编译器
klee@container:~$ clang --version
# 预期输出：clang version 13.0.1
```

### 基础功能测试
```bash
# 创建简单测试程序
klee@container:~$ echo "int main(int argc, char** argv) { return 0; }" > test.c

# 编译为LLVM字节码
klee@container:~$ clang -emit-llvm -g -c test.c -o test.bc

# 使用KLEE执行
klee@container:~$ klee --libc=uclibc --posix-runtime test.bc
```

## GNU Coreutils实验复现

### 步骤1：获取和构建Coreutils

#### 编译包含 Coreutils 的 Klee 镜像
```Dockerfile
# 请参照当前目录下的Dockerfile
```

#### 构建gcov版本（用于覆盖率验证）
```bash
$ mkdir obj-gcov
$ cd obj-gcov
$ ../configure --disable-nls CFLAGS="-g -fprofile-arcs -ftest-coverage"
$ make check && make
```

**配置参数解释：**
- `--disable-nls`: 禁用国际化支持，减少C库初始化复杂度
- `-fprofile-arcs -ftest-coverage`: 启用gcov覆盖率统计

#### 构建LLVM字节码版本
```bash
$ cd ../  # 回到coreutils-6.11目录
$ mkdir obj-llvm
$ cd obj-llvm

# 使用WLLVM编译器配置
$ CC=wllvm ../configure --disable-nls \
    CFLAGS="-g -O1 -Xclang -disable-llvm-passes -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__"
$ make
$ make -C src arch hostname
```

**关键编译标志说明：**
- `-O1 -Xclang -disable-llvm-passes`: 优化编译同时保持KLEE兼容性
- `-D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__`: 防止clang生成KLEE不支持的安全库函数

#### 提取LLVM字节码
```bash
$ cd src
$ find . -executable -type f | xargs -I '{}' extract-bc '{}'
$ ls -l *.bc  # 验证字节码文件生成
```

### 步骤2：sort工具特殊修改

**重要：sort工具需要特殊配置以兼容KLEE**

#### 修改filesort.c
```c
// 在源码中查找并修改：
#define INPUT_FILE_SIZE_GUESS (1024 * 1024)
// 改为：
#define INPUT_FILE_SIZE_GUESS 1024
```

#### 添加线程限制
```bash
# 在运行sort时添加--parallel=1参数，因为KLEE不支持多线程
```

### 步骤3：KLEE符号执行实验

#### 基础符号执行命令
```bash
# 进入字节码目录
$ cd obj-llvm/src

# 基本KLEE执行（以echo为例）
$ klee --libc=uclibc --posix-runtime ./echo.bc --sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout
```

#### 优化的KLEE执行命令
```bash
# 使用优化选项获得更好的覆盖率和性能
$ klee --simplify-sym-indices --write-cvcs --write-cov --output-module \
    --max-memory=1000 --disable-inlining --optimize --use-forked-solver \
    --use-cex-cache --libc=uclibc --posix-runtime \
    --external-calls=all --only-output-states-covering-new \
    --max-sym-array-size=4096 --max-solver-time=30s --max-time=60min \
    --watchdog --max-memory-inhibit=false --max-static-fork-pct=1 \
    --max-static-solve-pct=1 --max-static-cpfork-pct=1 --switch-type=internal \
    --search=random-path --search=nurs:covnew \
    --use-batching-search --batch-instructions=10000 \
    ./echo.bc --sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout
```

#### 标准符号参数配置

**大多数工具的标准配置：**
```bash
--sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout
```

**特殊工具需要调整的符号参数：**
- **dd**: `--sym-args 0 3 10 --sym-files 1 8 --sym-stdin 8 --sym-stdout`
- **dircolors**: `--sym-args 0 3 10 --sym-files 2 12 --sym-stdin 12 --sym-stdout`
- **echo**: `--sym-args 0 4 300 --sym-files 2 30 --sym-stdin 30 --sym-stdout`
- **expr**: `--sym-args 0 1 10 --sym-args 0 3 2 --sym-stdout`
- **printf**: `--sym-args 0 3 10 --sym-files 2 12 --sym-stdin 12 --sym-stdout`

### 步骤4：结果分析和验证

#### 统计信息查看
```bash
# 查看KLEE执行统计
$ klee-stats klee-last

# 使用KCachegrind可视化（如果安装）
$ kcachegrind klee-last/run.istats
```

#### 测试用例分析
```bash
# 查看生成的测试用例
$ ktest-tool klee-last/test000001.ktest

# 重放测试用例到原生程序
$ cd ../../obj-gcov/src
$ klee-replay ./echo ../../obj-llvm/src/klee-last/*.ktest
```

#### 覆盖率验证
```bash
# 清理之前的覆盖率数据
$ rm -f *.gcda

# 运行所有测试用例
$ klee-replay ./echo ../../obj-llvm/src/klee-last/*.ktest

# 生成覆盖率报告
$ gcov echo
$ cat echo.c.gcov  # 查看详细覆盖率
```

## 批量测试89个Coreutils工具

### 创建测试环境脚本
```bash
# 创建sandbox测试环境
$ mkdir -p /tmp/sandbox
$ cd /tmp/sandbox

# 创建环境变量文件test.env
$ cat > test.env << 'EOF'
PATH=/usr/bin:/bin
HOME=/tmp/sandbox
PWD=/tmp/sandbox
EOF
```

### 批量测试脚本示例
```bash
#!/bin/bash
# 完整的89个Coreutils测试脚本

COREUTILS_DIR="/path/to/coreutils-6.11/obj-llvm/src"
OUTPUT_BASE="/tmp/klee-results"

# OSDI'08论文测试的89个工具列表
TOOLS=(
    "base64" "basename" "cat" "chcon" "chgrp" "chmod" "chown" "chroot"
    "cksum" "comm" "cp" "csplit" "cut" "date" "dd" "df" "dircolors"
    "dirname" "du" "echo" "env" "expand" "expr" "factor" "false" "fmt"
    "fold" "head" "hostid" "hostname" "id" "ginstall" "join" "kill"
    "link" "ln" "logname" "ls" "md5sum" "mkdir" "mkfifo" "mknod"
    "mktemp" "mv" "nice" "nl" "nohup" "od" "paste" "pathchk" "pinky"
    "pr" "printenv" "printf" "ptx" "pwd" "readlink" "rm" "rmdir"
    "runcon" "seq" "setuidgid" "shred" "shuf" "sleep" "sort" "split"
    "stat" "stty" "sum" "sync" "tac" "tail" "tee" "touch" "tr" "tsort"
    "tty" "uname" "unexpand" "uniq" "unlink" "uptime" "users" "wc"
    "whoami" "who" "yes"
)

for tool in "${TOOLS[@]}"; do
    echo "Testing $tool..."

    # 检查特殊参数需求
    case $tool in
        "dd")
            SYM_ARGS="--sym-args 0 3 10 --sym-files 1 8 --sym-stdin 8 --sym-stdout"
            ;;
        "sort")
            # 需要添加--parallel=1
            SYM_ARGS="--sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout -- --parallel=1"
            ;;
        *)
            SYM_ARGS="--sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout"
            ;;
    esac

    # 执行KLEE测试
    klee --only-output-states-covering-new --optimize \
        --libc=uclibc --posix-runtime \
        --env-file=test.env --run-in-dir=/tmp/sandbox \
        --output-dir="$OUTPUT_BASE/$tool" \
        --max-time=60min \
        "$COREUTILS_DIR/$tool.bc" $SYM_ARGS
done
```

## 预期结果和性能指标

### OSDI'08原始实验结果
- **测试工具数量**: 89个独立Coreutils程序
- **平均行覆盖率**: 超过90%（中位数94%+）
- **测试用例生成**: 每个工具通常生成几十到几千个测试用例
- **最大并发状态**: 95,982个（hostid工具），平均最大值51,385个

### 现代Docker环境预期性能
- **KLEE版本**: 3.0（LLVM 13.0.1）
- **执行环境**: Ubuntu 22.04容器
- **预期改进**: 更稳定的约束求解，更好的内存管理

## 故障排除和注意事项

### 常见警告信息（通常可忽略）
```
undefined reference to function: __ctype_b_loc
executable has module level assembly (ignoring)
calling __user_main with extra arguments
calling external: getpagesize()
```

### 关键问题解决

#### 1. 栈溢出问题
**解决方案**: Docker运行时必须使用`--ulimit='stack=-1:-1'`

#### 2. 64位vs32位差异
**问题**: 原始实验在32位系统，64位系统产生更复杂约束
**解决**: 使用现代约束求解器和更多内存分配

#### 3. gcov覆盖率缺失
**问题**: gcov在`_exit`调用时不记录覆盖率
**解决**: 将`_exit`替换为`exit`，或使用KLEE内部覆盖率统计

#### 4. 线程支持问题
**问题**: 新版coreutils默认启用多线程，KLEE不支持
**解决**: 对sort等工具使用`--parallel=1`参数

### Docker特定注意事项

#### 安全考虑
- 默认用户有sudo权限，密码为"klee"
- **绝不能**在生产环境使用
- 仅用于实验和学习目的

#### 性能优化
- 使用足够的主机内存（8GB+推荐）
- 考虑使用SSD存储提升I/O性能
- 合理设置`--max-memory`参数避免系统资源耗尽

## 版本兼容性和更新说明

### KLEE版本演进
- **OSDI'08原版**: KLEE 1.0，LLVM 2.2-2.3
- **教程版本**: KLEE 2.0，LLVM 5.0+
- **当前Docker版**: KLEE 3.0，LLVM 13.0.1

### 命令参数变化
| 旧参数 | 新参数 | 说明 |
|--------|---------|------|
| `--with-libc --with-file-model=release` | `--libc=uclibc --posix-runtime` | 库链接方式更新 |
| `--allow-external-sym-calls` | `--external-calls=all` | 外部调用处理 |
| `--max-instruction-time=10.` | `--max-solver-time=30s` | 求解器超时 |
| `--use-random-path --use-interleaved-covnew-NURS` | `--search=random-path --search=nurs:covnew` | 搜索策略 |

### 推荐使用策略
1. **初学者**: 使用Docker方式，简单快速
2. **研究者**: 结合Docker和本地编译，获得最佳性能
3. **生产应用**: 本地编译安装，避免Docker开销

这份指南融合了KLEE的历史实验数据、现代教程方法和最新Docker技术，提供了完整的KLEE实验复现路径。Docker方式提供了最简单的入门途径，而详细的编译和配置信息确保了实验的可重复性和深入理解。
