# 教程三：用 KLEE 交叉验证两种排序（`examples/sort/sort.c`）

> 目标：把长度为 4 的整型数组设为**符号输入**，分别用**插入排序**与**冒泡排序**处理同一输入，然后断言两者输出完全一致。KLEE 会自动探索输入空间，若冒泡排序实现有问题，就会生成**导致断言失败的反例**与可回放的测试用例。

本教程沿用官方教程一/二的操作套路（编译为 LLVM bitcode → 运行 KLEE → 查看/回放 `.ktest`），并针对 `examples/sort/sort.c` 给出一份可直接上手的步骤说明。

---

## 0. 参考资料（强烈建议先快速浏览）

- 官方教程一：**Testing a Small Function**
  [https://klee-se.org/tutorials/testing-function/](https://klee-se.org/tutorials/testing-function/)
- 官方教程二：**Testing a Simple Regular Expression Library**
  [https://klee-se.org/tutorials/testing-regex/](https://klee-se.org/tutorials/testing-regex/)
- KLEE 内建函数（`klee_make_symbolic`、`klee_assume` 等）
  [https://klee-se.org/docs/intrinsics/](https://klee-se.org/docs/intrinsics/)
- KLEE 工具（`ktest-tool`、`klee-stats` 等）
  [https://klee-se.org/docs/tools/](https://klee-se.org/docs/tools/)
- 你的两份学习记录：
  教程一 README：[https://github.com/dyingc/klee_reproduce/blob/master/tutorials/first_tutorial/README.md](https://github.com/dyingc/klee_reproduce/blob/master/tutorials/first_tutorial/README.md)
  教程二 README：[https://github.com/dyingc/klee_reproduce/blob/master/tutorials/second_tutorial/README.md](https://github.com/dyingc/klee_reproduce/blob/master/tutorials/second_tutorial/README.md)
  教程二所用 `Regexp.c`（你有修改过）：
  [https://raw.githubusercontent.com/dyingc/klee_reproduce/refs/heads/master/tutorials/second_tutorial/Regexp.c](https://raw.githubusercontent.com/dyingc/klee_reproduce/refs/heads/master/tutorials/second_tutorial/Regexp.c)
- `sort.c`（社区镜像，内容与本教程使用代码一致，可对照查看）：
  [https://trong.loang.net/\~cnx/klee/plain/examples/sort/sort.c?h=2.0.x&id=64c67386a3c5eb4baa23847d737fd812312117f9](https://trong.loang.net/~cnx/klee/plain/examples/sort/sort.c?h=2.0.x&id=64c67386a3c5eb4baa23847d737fd812312117f9)

---

## 1. 代码概览

`examples/sort/sort.c` 的核心逻辑：

- `main()`：定义 `int input[4]`，用 `klee_make_symbolic(&input, sizeof(input), "input")` 把**整个数组**设为符号输入，然后调用 `test(input, 4)`。（参见官方教程一的“Marking input as symbolic”：[link](https://klee-se.org/tutorials/testing-function/)）
- `test()`：复制两份输入，分别调用 `insertion_sort()` 与 `bubble_sort()`，随后逐元素断言 `assert(temp1[i] == temp2[i]);`。
- **埋点**：`bubble_sort()` 只做**一趟**相邻交换就 `break`，不是完整的冒泡排序；因此它与插入排序**并不等价**。KLEE 会自动生成让两者输出不同的输入，从而触发断言失败。

---

## 2. 环境与目录

假设你在官方教程环境或容器中，示例位于（示意）：

/tmp/klee_src/examples/
├── get_sign/      # 教程一
├── regexp/        # 教程二
└── sort/          # 本教程

---

## 3. 编译并执行

### 3.1 编译为 LLVM bitcode（`.bc`）

```bash
cd /tmp/klee_src/examples/sort

# 生成 sort.bc
clang -I ../../include -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone sort.c

# （可选）用 llvm-nm 验证
llvm-nm sort.bc | grep -E 'main|klee_make_symbolic|bubble_sort|insertion_sort'
```

⸻

### 3.2 用 KLEE 执行 bitcode

最小命令：

```bash
klee sort.bc
```

也可以使用教程二推荐的常用选项（减少冗余用例、限制时间/分叉等）。

```bash
# 仅输出覆盖到新代码的状态（通常能显著减少 .ktest 数量）
klee --only-output-states-covering-new sort.bc

# 或限制运行成本（可按需组合）
klee -max-time=1min sort.bc
klee -max-forks=5000 sort.bc
```

预期结果：KLEE 会探索到让插入排序与“残缺冒泡排序”结果不同的输入，导致 assert 失败；输出目录里会出现相应的错误报告与 .ktest 文件。可使用 `ktest-tool <test-case-file>` 来查看该测试用例。如：

```bash
# 查看运行统计
klee@c42fe4c925b5:/tmp/klee_src/examples/sort$ klee-stats klee-last
------------------------------------------------------------------------
|  Path   |  Instrs|  Time(s)|  ICov(%)|  BCov(%)|  ICount|  TSolver(%)|
------------------------------------------------------------------------
|klee-last|     295|     0.01|    20.57|     8.33|     389|       52.52|
------------------------------------------------------------------------
# 查看测试用例
klee@c42fe4c925b5:/tmp/klee_src/examples/sort$ ktest-tool klee-last/test000001.ktest
ktest file : 'klee-last/test000001.ktest'
args       : ['sort.bc']
num objects: 1
object 0: name: 'input'
object 0: size: 16
object 0: data: b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
object 0: hex : 0x00000000000000000000000000000000
object 0: text: ................
# 查看错误
klee@c42fe4c925b5:/tmp/klee_src/examples/sort$ cat klee-last/test000001.exec.err
Error: external call with symbolic argument: printf
File: sort.c
Line: 52
assembly.ll line: 270
State: 1
Stack:
        #000000270 in test(array=22142020222976, nelem=4) at sort.c:52
        #100000372 in main() at sort.c:78
```

⸻

### 3.3 使用 `run.sh`

为简单起见，可直接使用 `tutorials/third_tutorial/run.sh`。该脚本包含了在预先编译好的 docker image 中直接生成 `sort.bc` 并执行 `klee`。

```bash
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
```

## 5. 定位失败用例并查看输入

列目录（klee-last 指向最近一次运行的目录）：

ls -1 klee-last

查找断言失败（.assert.err）对应编号 N：

ls klee-last/*assert.err
# 例：klee-last/test000123.assert.err

用 ktest-tool 查看同编号的 .ktest，可直接读出 4 个整型元素的具体取值（ktest-tool 的用途详见 KLEE Tools 文档）：
	•	ktest-tool 文档：https://klee-se.org/docs/tools/

ktest-tool klee-last/test000123.ktest

教程一“Replaying a test case”也演示了如何解读 .ktest 并回放：link

⸻

6. 回放失败用例（原生二进制 + libkleeRuntest）

参考官方教程一“Replaying a test case”，把程序与 libkleeRuntest 链接，并用 KTEST_FILE 指定要回放的 .ktest 文件：
	•	回放步骤详见教程一对应段落与命令示例：https://klee-se.org/tutorials/testing-function/

# 按你的 KLEE 构建路径调整
export LD_LIBRARY_PATH=/path/to/klee/build/lib:$LD_LIBRARY_PATH

# 重新编译 sort.c 并链接回放库
gcc -I ../../include -L /path/to/klee/build/lib/ sort.c -lkleeRuntest

# 用失败用例回放
KTEST_FILE=klee-last/test000123.ktest ./a.out
echo $?  # 断言失败会得到非 0 退出码


⸻

7. 缺陷成因与修复

7.1 原因

bubble_sort() 只做一趟扫描后就 break，因此无法保证整体有序；这与 insertion_sort() 的结果并不等价。KLEE 能自动合成“需要多趟冒泡才能有序”的输入来触发 assert。

7.2 修复示例

把 bubble_sort() 改为“直到本趟无交换才停止”的标准写法之一：

void bubble_sort(int *array, unsigned nelem) {
  for (;;) {
    int done = 1;
    for (unsigned i = 0; i + 1 < nelem; ++i) {
      if (array[i+1] < array[i]) {
        int t = array[i + 1];
        array[i + 1] = array[i];
        array[i] = t;
        done = 0;
      }
    }
    if (done) break;  // 只有当本趟无交换才停止
  }
}

重新编译并运行 KLEE（可继续使用 --only-output-states-covering-new）：若修复正确，将不再产生 .assert.err。运行与选项说明可参考教程二：https://klee-se.org/tutorials/testing-regex/

⸻

8.（可选）给输入加约束，控制路径规模

当输入是 4 个 32 位整型时，理论路径空间很大。可像教程二那样使用 klee_assume 给每个元素加范围约束（例如限制在 [-10, 10]），以削减状态数：
	•	klee_assume 用法与注意事项见官方 Intrinsics 文档：https://klee-se.org/docs/intrinsics/

for (int i = 0; i < 4; ++i) {
  klee_assume(input[i] >= -10);
  klee_assume(input[i] <= 10);
}


⸻

9. 清理并多次试验

多次运行会生成 klee-out-0, klee-out-1, ... 与 klee-last 软链。需要重来时可清理：

rm -rf klee-out-* klee-last


⸻

10. 你将掌握的要点（对应官方教程的综合实践）
	1.	把数组整体设为符号变量（教程一的 klee_make_symbolic 思路，参考：link）。
	2.	用交叉验证（两种排序）构造可判定的断言，让 KLEE 自动找出不一致输入。
	3.	阅读 .err 与 .ktest（工具与输出说明，参考：link、link）。
	4.	按官方套路回放生成的用例（libkleeRuntest，参考：link）。
	5.	用 klee_assume 合理约束输入空间（参考：link）。

补充：若你希望和教程二一样，观察“限制时间/分叉”等对探索规模的影响，可直接复用其命令与参数说明：
https://klee-se.org/tutorials/testing-regex/

⸻

附：本教程使用的 sort.c（与社区镜像一致，可对照核验）
	•	社区镜像原始文件（可读）：
https://trong.loang.net/~cnx/klee/plain/examples/sort/sort.c?h=2.0.x&id=64c67386a3c5eb4baa23847d737fd812312117f9

⸻


