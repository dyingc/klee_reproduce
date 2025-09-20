# KLEE 符号执行实验完整复现指南

- [OSDI'08 Coreutils Experiments](https://klee-se.org/docs/coreutils-experiments/)：说明 KLEE 在 [OSDI’08 论文](https://llvm.org/pubs/2008-12-OSDI-KLEE.pdf) 中用于实验 GNU Coreutils 的具体构建环境、软件版本、测试工具与参数配置等细节。
- [Using KLEE with Docker](https://klee-se.org/docker/)：介绍如何通过 Docker 容器快速获取、运行与使用 KLEE，包括拉取镜像、创建容器和持久化使用等实用操作说明。

[官方教程](https://klee-se.org/tutorials/testing-coreutils/)

## 1. 工作流程概览
本教程采用**双版本验证**策略：
1. **gcov 版本** - 用于覆盖率测量和验证
2. **LLVM 版本** - 用于 KLEE 符号执行分析

```mermaid
graph LR
    A[gcov版本编译] --> B[LLVM版本编译]
    B --> C[KLEE生成测试用例]
    C --> D[测试用例验证gcov版本]
    D --> E[覆盖率对比分析]
```

### 1.1 双版本验证的具体比较机制

验证策略的核心是**使用 gcov 作为独立的"真相来源"**，验证KLEE符号执行结果在真实程序中的有效性：

#### 步骤1：KLEE在LLVM版本上的分析结果
```bash
$ cd ~/coreutils-*/obj-llvm/src
$ pwd
/home/klee/coreutils-6.11/obj-llvm/src
$ klee --optimize --libc=uclibc --posix-runtime ./echo.bc --sym-arg 3
KLEE: done: completed paths = 25
KLEE: done: generated tests = 25

$ klee-stats klee-last
---------------------
|  ICov(%)|  BCov(%)|
|    33.46|    22.66|  # KLEE的覆盖率声明
---------------------
```

#### 步骤2：用测试用例验证 gcov 版本
```bash
$ cd ../../obj-gcov/src
$ rm -f *.gcda  # 清空覆盖率数据
$ klee-replay ./echo ../../obj-llvm/src/klee-last/*.ktest  # 运行25个测试用例

$ gcov echo
File '../../src/echo.c'
Lines executed:52.43% of 103  # gcov的实际测量结果
```

#### 步骤3：关键对比分析

| 指标 | KLEE报告 | gcov报告 | 说明 |
|------|----------|----------|------|
| **覆盖率范围** | 33.46% (所有代码) | 52.43% (仅echo.c) | KLEE计算包含库代码，gcov可专注源文件 |
| **测试用例数** | 25个路径 | 25个文件执行 | 一对一验证KLEE生成用例的有效性 |
| **验证意义** | 符号执行探索 | 真实程序执行 | 确保符号执行结果在实际环境中可重现 |

这种对比验证了：
- ✅ KLEE生成的测试用例在真实程序中确实有效
- ✅ 符号执行发现的路径能够在实际运行中重现
- ✅ 为进一步优化提供基准（如需提高覆盖率，可考虑`--sym-args 0 2 4`等策略）

```bash
$ klee --optimize --libc=uclibc --posix-runtime ./echo.bc --sym-args 0 2 4
KLEE: done: completed paths = 9963 # 远多于之前的测试用例数量
KLEE: done: generated tests = 9963

$ klee-stats klee-last
---------------------
|  ICov(%)|  BCov(%)|
|    34.95|    24.22|  # 新的 KLEE 覆盖率声明
---------------------
$ cd ../../obj-gcov/src
$ rm -f *.gcda
$ klee-replay ./echo ../../obj-llvm/src/klee-last/test000[01]*.ktest 2>&1 | grep 'KLEE-REPLAY: NOTE: Test file'  # 测试用例太多，只运行一部分
$ gcov echo
File '../../src/echo.c'
Lines executed:66.99% of 103  # gcov 的实际测量结果出现显著提高
# 注意，经过测试，如果在上面的`klee-replay`阶段依次把所有的测试用例都跑一遍，该覆盖率甚至可以提高到：98.06%
```

## 2. 编译包含 Coreutils 的 Klee 镜像
```Dockerfile
# 请参照上级目录下的Dockerfile
```

### 2.1 构建gcov版本（用于覆盖率验证）
```bash
$ mkdir obj-gcov
$ cd obj-gcov
$ ../configure --disable-nls CFLAGS="-g -fprofile-arcs -ftest-coverage"
$ make check && make
```

**gcov的作用：**
- 提供独立的覆盖率验证机制
- 验证KLEE生成的测试用例在真实程序上的实际覆盖率
- 生成`.gcda`文件记录执行路径，用于后续`klee-replay`验证

**配置参数解释：**
- `--disable-nls`: 禁用国际化支持，减少C库初始化复杂度
- `-fprofile-arcs -ftest-coverage`: 生成的可执行文件会记录执行路径，运行后产生 `.gcda` 文件（覆盖率数据）

### 2.2 构建LLVM字节码版本
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

**提取LLVM字节码：**
```bash
$ cd src
$ find . -executable -type f | xargs -I '{}' extract-bc '{}'
# 生成 .bc 文件供KLEE分析
```

### 2.3 sort工具特殊修改

**必要性：** sort的默认内存配置会导致KLEE内存溢出和求解器超时。

**修改方法：**
```bash
# 在coreutils源码目录执行
$ pwd
/home/klee/coreutils-6.11
$ sed 's/#define INPUT_FILE_SIZE_GUESS.*/#define INPUT_FILE_SIZE_GUESS 1024/g' -i src/sort.c
```

**修改对比：**
```c
// 修改前（会导致KLEE失败）
#define INPUT_FILE_SIZE_GUESS (1024 * 1024)  // 1MB

// 修改后（KLEE可处理）
#define INPUT_FILE_SIZE_GUESS 1024           // 1KB
```

**影响：**
- ✅ 解决KLEE内存限制问题，避免`memory allocation failed`错误
- ✅ 显著提升符号执行性能（1000倍复杂度降低）
- ✅ 保持sort核心功能和代码覆盖率
- ⚠️ 仅影响初始缓冲区大小，大文件时程序会自动扩展

**验证：** 修改后KLEE能成功分析sort并生成测试用例，而非因内存问题终止。

## 3. 单工具 KLEE 符号执行实验

### 3.1 进入 KLEE 容器环境
```bash
# 启动容器
$ docker run --rm -it -e DISPLAY=:1 --ulimit='stack=-1:-1' -v /tmp/.X11-unix:/tmp/.X11-unix:rw klee-coreutils:${VER} bash
```

### 3.2 准备测试环境
```bash
# 进入字节码目录
$ cd /home/klee/coreutils-6.11/obj-llvm/src

# 创建沙盒测试环境
$ mkdir -p /tmp/sandbox
$ cd /tmp/sandbox

# 创建环境变量文件
$ cat > test.env << 'EOF'
PATH=/usr/bin:/bin
HOME=/tmp/sandbox
PWD=/tmp/sandbox
EOF
```

### 3.3 基础符号执行实验

**最简单的符号执行示例（echo工具）：**
```bash
$ cd /home/klee/coreutils-6.11/obj-llvm/src
$ klee --libc=uclibc --posix-runtime ./echo.bc --sym-args 0 1 10
```

**标准符号参数配置：**
```bash
# OSDI'08论文中使用的标准配置
$ klee --libc=uclibc --posix-runtime \
    --env-file=/tmp/sandbox/test.env \
    --run-in-dir=/tmp/sandbox \
    ./echo.bc \
    --sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout
```

### 3.4 高级符号执行配置

**推荐的优化执行命令：**
```bash
$ klee --simplify-sym-indices --write-cvcs --write-cov --output-module \
    --max-memory=1000 --disable-inlining --optimize --use-forked-solver \
    --use-cex-cache --libc=uclibc --posix-runtime \
    --external-calls=all --only-output-states-covering-new \
    --max-sym-array-size=4096 --max-solver-time=30s --max-time=60min \
    --watchdog --max-memory-inhibit=false --max-static-fork-pct=1 \
    --max-static-solve-pct=1 --max-static-cpfork-pct=1 --switch-type=internal \
    --search=random-path --search=nurs:covnew \
    --use-batching-search --batch-instructions=10000 \
    --env-file=/tmp/sandbox/test.env --run-in-dir=/tmp/sandbox \
    ./echo.bc --sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout
```

#### 3.4.1 通用参数

- `--libc=uclibc`: 使用uClibc库，提供POSIX兼容性
- `--posix-runtime`: 启用POSIX运行时环境支持
- `--optimize`: 启用死代码消除等优化
- `--only-output-states-covering-new`: 仅输出覆盖新代码的状态
- `--max-memory=1000`: 限制内存使用（MB）
- `--max-time=60min`: 设置最大执行时间
- `--search=random-path`: 使用随机路径搜索策略
- `--use-batching-search`: 启用批量搜索优化

#### 3.4.2 符号参数策略详解

**符号参数语法：**
```bash
--sym-args MIN_ARGC MAX_ARGC MAX_ARG_LEN  # 符号命令行参数
--sym-files N MAX_SIZE                    # N个符号文件，每个最大MAX_SIZE字节
--sym-stdin MAX_SIZE                      # 符号标准输入，最大MAX_SIZE字节
--sym-stdout                              # 符号标准输出
```

**89个工具的标准配置：**
```bash
# 大多数工具使用的标准配置
--sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout
```

**特殊工具的扩展配置：**
- **dd**: `--sym-args 0 3 10 --sym-files 1 8 --sym-stdin 8 --sym-stdout`
- **dircolors**: `--sym-args 0 3 10 --sym-files 2 12 --sym-stdin 12 --sym-stdout`
- **echo**: `--sym-args 0 4 300 --sym-files 2 30 --sym-stdin 30 --sym-stdout`
- **expr**: `--sym-args 0 1 10 --sym-args 0 3 2 --sym-stdout`
- **printf**: `--sym-args 0 3 10 --sym-files 2 12 --sym-stdin 12 --sym-stdout`
- **sort**: 需要添加 `--parallel=1` 参数禁用多线程
- **ptx**: `--sym-args 0 2 10 --sym-files 2 8 --sym-stdin 8 --sym-stdout`
- **md5sum**: `--sym-args 0 1 10 --sym-files 2 8`

### 3.5 实际测试示例

**测试echo工具：**
```bash
$ klee --libc=uclibc --posix-runtime \
    --env-file=/tmp/sandbox/test.env --run-in-dir=/tmp/sandbox \
    --max-time=5min --optimize --only-output-states-covering-new \
    ./echo.bc --sym-args 0 4 300 --sym-files 2 30 --sym-stdin 30 --sym-stdout
```

**测试sort工具（需要特殊处理）：**
```bash
$ klee --libc=uclibc --posix-runtime \
    --env-file=/tmp/sandbox/test.env --run-in-dir=/tmp/sandbox \
    --max-time=60min --optimize --only-output-states-covering-new \
    ./sort.bc --sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8 --sym-stdout -- --parallel=1
```

### 3.6 测试监控与结果分析

#### 3.6.1 监控和统计

**实时查看统计信息：**
```bash
# 查看当前执行统计
$ klee-stats klee-last

# 持续监控（每5秒更新）
$ watch -n 5 'klee-stats klee-last'
```

**典型统计信息含义：**
```
| Path     | Instrs  | Time(s) | ICov(%) | BCov(%) | ICount | TSolver(%) |
|----------|---------|---------|---------|---------|--------|------------|
|klee-last | 52417   | 121.3   | 84.25   | 65.1    | 204    | 48.2       |
```
- **Instrs**: 执行的LLVM指令数
- **Time(s)**: 总执行时间
- **ICov(%)**: 指令覆盖率
- **BCov(%)**: 分支覆盖率
- **ICount**: 生成的测试用例数
- **TSolver(%)**: 约束求解器时间占比

#### 3.6.2 高级可视化分析

**使用KCachegrind进行指令级分析：**
```bash
# 生成可视化统计文件
$ klee --libc=uclibc --posix-runtime ./echo.bc --sym-args 0 1 10

# 启动KCachegrind查看详细性能分析
$ kcachegrind klee-last/run.istats
```

**KCachegrind中的关键指标：**
- **Incl.（Inclusive）**：包含该函数自身以及它调用的函数的占比。
- **Self**：仅该函数自身的占比（不含被调函数）。
- **Called**：函数被调用次数。
- **Function**：函数名称。


- **CoveredInstructions (lcov)**：被执行的指令百分比。
- **Forks**：路径分叉次数。
- **Instructions (I)**：总指令数占比。
- **Queries / QueriesValid / QueriesInvalid**：KLEE 与 SMT 求解器交互的次数。
- **Queries (Q)**：总查询次数。
- **QueriesValid (Qv)**：有效查询。
- **QueriesInvalid (Qiv)**：无效查询。
- **QueryTime (Qtime)**：查询求解花费的时间百分比。

#### 3.6.3 测试用例深度分析

**测试用例文件结构：**
```bash
# 查看生成的测试用例文件
$ ls -la klee-last/
# 输出示例：
# test000001.ktest  - 第一个测试用例
# test000002.ktest  - 第二个测试用例
# ...
# messages.txt      - 错误信息和警告
# info              - 执行信息文件
# warnings.txt      - 警告信息
```

**分析单个测试用例：**
```bash
# 查看测试用例详细内容
$ ktest-tool klee-last/test000001.ktest
```

**典型输出解析：**
```
ktest file : 'test000001.ktest'
args       : ['echo.bc', '--sym-args', '0', '1', '10']
num objects: 3
object    0: name: 'arg0'
object    0: size: 2
object    0: data: '\x00\x00'
object    1: name: 'stdin'
object    1: size: 8
object    1: data: '\x00\x00\x00\x00\x00\x00\x00\x00'
object    2: name: 'stdout-stat'
object    2: size: 144
object    2: data: ...
```

#### 3.6.4 测试用例重放和验证

**重放到gcov版本程序：**
```bash
# 切换到gcov编译版本目录
$ cd /home/klee/coreutils-6.11/obj-gcov/src

# 清理之前的覆盖率数据
$ rm -f *.gcda *.gcov

# 重放所有KLEE生成的测试用例
$ klee-replay ./echo ../../obj-llvm/src/klee-last/*.ktest
```

**重放输出示例：**
```
KTEST_FILE=../../obj-llvm/src/klee-last/test000001.ktest
KTEST_FILE=../../obj-llvm/src/klee-last/test000002.ktest
...
```

#### 3.6.5 覆盖率测量和分析

**生成gcov覆盖率报告：**
```bash
# 生成覆盖率统计文件
$ gcov echo
# 输出：File 'echo.c' - Lines executed:85.23% of 123

# 查看详细的行级覆盖率
$ cat echo.c.gcov
```

**gcov输出格式解读：**
```
       -:    1:/* echo.c */
       1:    2:#include <stdio.h>
       5:    3:int main(int argc, char **argv) {
       5:    4:  if (argc > 1) {
       3:    5:    printf("%s", argv[1]);
   #####:    6:    if (error_condition)  // 未覆盖的代码
       2:    7:      return 1;
       5:    8:  }
       5:    9:  return 0;
      -:   10:}
```
- **数字**: 该行被执行的次数
- **#####**: 该行从未被执行（未覆盖）
- **-**: 空行或注释行

#### 3.6.6 高级覆盖率分析

**使用zcov生成HTML覆盖率报告：**
```bash
# 如果系统安装了zcov
$ zcov-genhtml *.gcda
$ firefox zcov-output/index.html  # 在浏览器中查看
```

**使用lcov生成详细报告：**
```bash
# 收集覆盖率数据
$ lcov --capture --directory . --output-file coverage.info

# 生成HTML报告
$ genhtml coverage.info --output-directory coverage_html

# 查看报告
$ firefox coverage_html/index.html
```

#### 3.6.7 错误和异常分析

**分析KLEE错误报告：**
```bash
# 查看错误信息
$ cat klee-last/messages.txt

# 查看警告信息
$ cat klee-last/warnings.txt

# 分析断言失败（如果有）
$ ls klee-last/*.assert.err
```

**常见错误类型：**
- **Assertion failures**: 程序断言失败
- **Division by zero**: 除零错误
- **Out of bound access**: 数组越界访问
- **Use after free**: 释放后使用错误
- **Memory leaks**: 内存泄漏

#### 3.6.8 符号执行状态分析

#### 3.13.1 单次（最新）结果比较和分析

**查看状态遍历统计：**
```bash
# 查看状态遍历信息（如果使用了--write-states）
$ find klee-last/ -name "*.kquery" | wc -l  # 查询文件数量
$ find klee-last/ -name "*.smt2" | head -5   # 查看约束文件
```

**约束求解器性能分析：**
```bash
# 查看求解器统计（在messages.txt中）
$ grep -E "(solver|query|time)" klee-last/messages.txt
```

#### 3.13.2 批量结果比较和分析

**比较多个工具的覆盖率：**
```bash
# 创建批量分析脚本
$ cat > analyze_results.sh << 'EOF'
#!/bin/bash
echo "Tool,ICov(%),BCov(%),Tests,Time(s)"
for result in klee-out-*; do
    if [ -d "$result" ]; then
        tool=$(basename $result | sed 's/klee-out-//')
        stats=$(klee-stats $result | tail -1)
        echo "$tool,$stats" | cut -d'|' -f4,5,6,3 | tr '|' ','
    fi
done
EOF

$ chmod +x analyze_results.sh
$ ./analyze_results.sh > coverage_summary.csv
```

#### 3.13.3 性能基准对比

**OSDI'08原始实验基准：**
- **平均行覆盖率**: 90%+ (中位数94%+)
- **平均分支覆盖率**: 85%+
- **平均测试用例**: 50-500个每工具
- **平均执行时间**: 1-60分钟每工具

**现代环境预期改进：**
- **更好的约束求解**: 使用Z3求解器，求解速度提升
- **内存管理优化**: 支持更大的符号数组和更深的路径
- **并行化支持**: 可以使用多核进行符号执行加速

#### 3.13.4 结果质量评估

**评估标准：**
```bash
# 1. 覆盖率达标检查
coverage_threshold=85
actual_coverage=$(klee-stats klee-last | tail -1 | cut -d'|' -f4 | tr -d ' ')
if (( $(echo "$actual_coverage > $coverage_threshold" | bc -l) )); then
    echo "✓ 覆盖率达标: $actual_coverage%"
else
    echo "✗ 覆盖率不足: $actual_coverage%"
fi

# 2. 测试用例多样性检查
test_count=$(ls klee-last/*.ktest | wc -l)
if [ $test_count -gt 10 ]; then
    echo "✓ 测试用例充足: $test_count 个"
else
    echo "✗ 测试用例不足: $test_count 个"
fi
```

## 4. 批量测试89个Coreutils工具

### 4.1 创建测试环境脚本
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

**批量测试脚本示例**
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

### 4.2 预期结果和性能指标

#### 4.2.1 OSDI'08原始实验结果
- **测试工具数量**: 89个独立Coreutils程序
- **平均行覆盖率**: 超过90%（中位数94%+）
- **测试用例生成**: 每个工具通常生成几十到几千个测试用例
- **最大并发状态**: 95,982个（hostid工具），平均最大值51,385个

#### 4.2.2 现代Docker环境预期性能
- **KLEE版本**: 3.0（LLVM 13.0.1）
- **执行环境**: Ubuntu 22.04容器
- **预期改进**: 更稳定的约束求解，更好的内存管理

### 4.3 故障排除和注意事项

#### 4.3.1 常见警告信息（通常可忽略）

```bash
undefined reference to function: __ctype_b_loc
executable has module level assembly (ignoring)
calling __user_main with extra arguments
calling external: getpagesize()
```

#### 4.3.2 关键问题解决

1. 栈溢出问题
**解决方案**: Docker运行时必须使用`--ulimit='stack=-1:-1'`

2. 64位vs32位差异
**问题**: 原始实验在32位系统，64位系统产生更复杂约束
**解决**: 使用现代约束求解器和更多内存分配

3. gcov覆盖率缺失
**问题**: gcov在`_exit`调用时不记录覆盖率
**解决**: 将`_exit`替换为`exit`，或使用KLEE内部覆盖率统计

4. 线程支持问题
**问题**: 新版coreutils默认启用多线程，KLEE不支持
**解决**: 对sort等工具使用`--parallel=1`参数

#### 4.3.3 Docker特定注意事项

1. 安全考虑
- 默认用户有sudo权限，密码为"klee"
- **绝不能**在生产环境使用
- 仅用于实验和学习目的

2. 性能优化
- 使用足够的主机内存（8GB+推荐）
- 考虑使用SSD存储提升I/O性能
- 合理设置`--max-memory`参数避免系统资源耗尽

#### 4.3.3 版本兼容性和更新说明

## 5. KLEE版本演进
1. **KLEE版本**
    - **OSDI'08原版**: KLEE 1.0，LLVM 2.2-2.3
    - **教程版本**: KLEE 2.0，LLVM 5.0+
    - **当前Docker版**: KLEE 3.0，LLVM 13.0.1

2. **命令参数变化**

| 旧参数 | 新参数 | 说明 |
|--------|---------|------|
| `--with-libc --with-file-model=release` | `--libc=uclibc --posix-runtime` | 库链接方式更新 |
| `--allow-external-sym-calls` | `--external-calls=all` | 外部调用处理 |
| `--max-instruction-time=10.` | `--max-solver-time=30s` | 求解器超时 |
| `--use-random-path --use-interleaved-covnew-NURS` | `--search=random-path --search=nurs:covnew` | 搜索策略 |

3. **推荐使用策略**
    - **初学者**: 使用Docker方式，简单快速
    - **研究者**: 结合Docker和本地编译，获得最佳性能
    - **生产应用**: 本地编译安装，避免Docker开销

