# 教程三：用 KLEE 交叉验证两种排序（`examples/sort/sort.c`）

> 目标：把长度为 4 的整型数组设为**符号输入**，分别用**插入排序**与**冒泡排序**处理同一输入，然后断言两者输出完全一致。若冒泡排序实现有问题，KLEE 会自动探索并生成**导致断言失败的反例**与可回放用例。

**变更要点**：为避免 `printf` 在“符号实参”下触发外部调用策略，本教程版本已将所有 `printf` 改为 **KLEE 内建输出**（`klee_warning`/`klee_print_expr`）。这样无需放宽 `--external-calls`，也不必引入 POSIX runtime，就能专注于“交叉验证两种排序”的核心目标。

---

## 0. 参考资料（强烈建议先快速浏览）

- 官方教程一：**Testing a Small Function**
  [https://klee-se.org/tutorials/testing-function/](https://klee-se.org/tutorials/testing-function/)
- 官方教程二：**Testing a Simple Regular Expression Library**
  [https://klee-se.org/tutorials/testing-regex/](https://klee-se.org/tutorials/testing-regex/)
- 使用符号化环境（命令行参数/符号文件/STDIN/STDOUT 等）
  [Using a symbolic environment](https://klee-se.org/tutorials/using-symbolic/)
- KLEE 选项总览（`--external-calls`、`--external-call-warnings` 等）
  [https://klee-se.org/docs/options/](https://klee-se.org/docs/options/)
- 使用 KLEE 的 uClibc（为什么/何时需要它）
  [https://klee-se.org/tutorials/testing-coreutils/](https://klee-se.org/tutorials/testing-coreutils/)
- KLEE 工具（`ktest-tool`、`klee-stats` 等）
  [https://klee-se.org/docs/tools/](https://klee-se.org/docs/tools/)
- KLEE 内建函数（`klee_make_symbolic`、`klee_assume`、`klee_print_expr` 等）
  [https://klee-se.org/docs/intrinsics/](https://klee-se.org/docs/intrinsics/)
- `sort.c`（社区镜像，可对照）
  [https://trong.loang.net/~cnx/klee/plain/examples/sort/sort.c?h=2.0.x&id=64c67386a3c5eb4baa23847d737fd812312117f9](https://trong.loang.net/~cnx/klee/plain/examples/sort/sort.c?h=2.0.x&id=64c67386a3c5eb4baa23847d737fd812312117f9)

---

## 1. `sort.c` 代码概览（已用 `klee_print_expr` 替换 `printf`）

- `main()`：将 `int input[4]` 整块设为符号：`klee_make_symbolic(&input, sizeof(input), "input")`，调用 `test(input, sizeof(input) / sizeof(input[0]))`。
- `test()`：复制两份输入（`temp1/ temp2`），分别用 `insertion_sort()` 与**故意不完整**的 `bubble_sort()` 排序；用 `klee_warning/klee_print_expr` 打印数组；最后逐元素断言 `assert(temp1[i] == temp2[i])`。
- `bubble_sort()`：只进行**一趟**扫描就 `break`（有缺陷）；因此对某些输入与插入排序结果不一致，断言会失败。
- 为了方便查看数组，本教程增加了一个小工具函数 `dump_array_klee(title, arr, n)`：用 `klee_warning` 打印标题、用 `klee_print_expr` 逐元素打印（保持“符号性”，不会触发外部调用策略）。

---

## 2. 编译与运行

### 2.1 编译为 LLVM bitcode（`.bc`）
```bash
cd /tmp/klee_src/examples/sort

clang -I ../../include -emit-llvm -g -c -O0 -Xclang -disable-O0-optnone sort.c

#（可选）快速检查符号：

llvm-nm sort.bc | grep -E 'main|klee_make_symbolic|bubble_sort|insertion_sort'
```

### 2.2 运行 KLEE

由于已经移除 printf，最小命令即可：

```bash
klee --emit-all-errors --only-output-states-covering-new --solver-backend=stp sort.bc
```

说明：
- 本程序不做文件/系统调用，memcpy/memmove 会被降低为 LLVM intrinsic，malloc/free 有 KLEE 运行时支持，因此无需 --libc=uclibc 或 --posix-runtime。
- 若你的代码加入了 POSIX 交互（如打开文件、读写 STDIN/STDOUT），再参考 Using a symbolic environment 的方式加 --posix-runtime 以及 -sym-* 参数。

预期现象：KLEE 会探索到使 insertion_sort 与“残缺冒泡排序”输出不同的输入，触发断言，生成 .assert.err 和对应 .ktest。

⸻

## 3. 查看失败用例与统计

列目录（klee-last 指向最近一次运行目录）：

```bash
ls -1 klee-last
```

找到断言失败（形如 test000123.assert.err）：

```bash
ls klee-last/test*assert.err
```

用 ktest-tool 查看同编号的输入（工具文档见 KLEE Tools）：

```bash
$ ktest-tool klee-last/test000001.ktest
ktest file : 'klee-last/test000001.ktest'
args       : ['sort.bc']
num objects: 2
object 0: name: 'nelem'
object 0: size: 4
object 0: data: b'\x03\x00\x00\x00'
object 0: hex : 0x03000000
object 0: int : 3
object 0: uint: 3
object 0: text: ....
object 1: name: 'input'
object 1: size: 12
object 1: data: b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
object 1: hex : 0x000000000000000000000000
object 1: text: ............

#（可选）查看统计：

$ klee-stats klee-last
------------------------------------------------------------------------
|  Path   |  Instrs|  Time(s)|  ICov(%)|  BCov(%)|  ICount|  TSolver(%)|
------------------------------------------------------------------------
|klee-last|   13988|     0.29|    96.84|    86.84|     569|       85.81|
------------------------------------------------------------------------
```

⸻

## 4. 回放失败用例（原生二进制 + libkleeRuntest）

做法与教程1一致（“Replaying a test case”）：
https://klee-se.org/tutorials/testing-function/

```bash
# 重新编译并链接回放库
gcc -I /tmp/klee_src/include -DREPLAY -L /tmp/klee_build130stp_z3/lib \
  -c sort.c -o sort.o && \
  gcc sort.o -o sort.out -DREPLAY -L /tmp/klee_build130stp_z3/lib \
  -lkleeRuntest && rm -f sort.o

# 指定要回放的 failing ktest
$ KTEST_FILE=klee-last/test000001.ktest ./sort.out
$ echo $?
0
$ KTEST_FILE=klee-last/test000002.ktest ./sort.out
sort.out: sort.c:150: test: Assertion `0` failed.
Aborted (core dumped) # 断言失败
klee@f6ef5b170bac:/tmp/klee_src/examples/sort$ cat klee-last/test000002.assert.err # 查看对应的错误信息
Error: ASSERTION FAIL: 0
File: sort.c
Line: 150
assembly.ll line: 308
State: 2
Stack:
        #000000308 in test(array=21576460271616, nelem=symbolic) at sort.c:150
        #100000492 in main() at sort.c:171
```

查看导致这一断言失败的输入：

```bash
$ ktest-tool klee-last/test000002.ktest
ktest file : 'klee-last/test000002.ktest'
args       : ['sort.bc']
num objects: 2
object 0: name: 'nelem'
object 0: size: 4
object 0: data: b'\x03\x00\x00\x00'
object 0: hex : 0x03000000
object 0: int : 3
object 0: uint: 3
object 0: text: ....
object 1: name: 'input'
object 1: size: 12
object 1: data: b'\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00'
object 1: hex : 0x010000000100000000000000
object 1: text: ............
```

⸻

## 5. 缺陷成因与修复

原因：bubble_sort() 只做一趟扫描后就 break，无法保证全局有序，因此与 insertion_sort() 的输出不等价。KLEE 会自动生成“需要多趟冒泡才有序”的输入触发断言失败。

修复示例（直到本趟无交换才停止）：

```c
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
    if (done) break;  // 没有交换才退出
  }
}
```

重新编译并运行 KLEE，若修复正确，将不再出现 .assert.err。

⸻

## 6.（可选）给输入加约束，控制路径规模

输入是 4 个 32 位整型，理论路径空间很大。可像教程二那样加范围约束（例如 [-10, 10]），以削减状态数（内建函数文档见 KLEE Intrinsics）：

```c
for (int i = 0; i < 4; ++i) {
  klee_assume(input[i] >= -10);
  klee_assume(input[i] <=  10);
}
```

⸻

## 7. 常见问答（FAQ）
- Q：为什么不再需要 --external-calls=all/--libc=uclibc？
  - A： 因为我们已将 printf 替换为 KLEE 内建输出（klee_warning/klee_print_expr），不再把符号数据传给外部函数。而本程序又没有文件/系统调用，因此最小命令即可完成分析。若将来需要符号化文件/STDIN/命令行参数，请参考 Using a symbolic environment。
- Q：memcpy/memmove/malloc 会不会也算“外部函数”？
  - A： memcpy/memmove 在 LLVM 中通常会降为 llvm.memcpy/llvm.memmove intrinsic，KLEE 内置支持；malloc/free 由 KLEE 运行时处理。真正需要小心的是以符号实参调用的外部库函数（本教程里已避免）。

⸻

## 8. 清理并多次试验

多次运行会生成 klee-out-0, klee-out-1, ... 与 klee-last。需要重来时：

```bash
rm -rf klee-out-* klee-last
```

⸻

## 9. 你将掌握的要点（对应官方教程的综合实践）
	1.	把数组整体设为符号变量（参见教程一：https://klee-se.org/tutorials/testing-function/）。
	2.	用交叉验证（两种排序）构造可判定断言，让 KLEE 自动找出不一致输入。
	3.	阅读 .err 与 .ktest，并回放失败用例（见 https://klee-se.org/docs/tools/ 与 https://klee-se.org/tutorials/testing-function/）。
	4.	正确处理外部输出：用 klee_print_expr/避免把符号实参传给外部库；如需符号化环境，请参考 https://klee-se.org/tutorials/using-symbolic/。
	5.	用 klee_assume 合理约束输入空间（文档见 https://klee-se.org/docs/intrinsics/）。

⸻


