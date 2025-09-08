# KLEE 快速入门：测试一个小函数

欢迎！本 README 面向 **KLEE**（一个基于 LLVM 的动态符号执行工具）的初学者。我们将通过一个简单示例，带你了解 **如何用 KLEE 自动生成测试用例**。

---

## 示例函数

我们要测试的函数如下，用于判断整数 `x` 的符号：

```c
int get_sign(int x) {
  if (x == 0)
    return 0;

  if (x < 0)
    return -1;
  else
    return 1;
}
```

逻辑示意图：

```
        +------+
   x==0 | Yes  | return 0
        +------+
            |
            v
        +-----------+
  x<0?  | Yes       | return -1
        | No        | return 1
        +-----------+
```

KLEE 将会探索这 3 条路径，并为每条路径生成一个测试用例。

⸻

1. 编写测试驱动（test driver）

我们需要一个 main()，把输入 a 变成符号变量：

```c
#include <klee/klee.h>

int get_sign(int x);

int main() {
  int a;
  klee_make_symbolic(&a, sizeof(a), "a");
  return get_sign(a);
}
```

这里的 klee_make_symbolic() 是关键：
👉 它告诉 KLEE “把 a 看作可以取任何值”。

⸻

2. 编译为 LLVM 位码

运行以下命令生成 .bc 文件：

```bash
$ clang -I ../../include -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone get_sign.c
```

得到 get_sign.bc，这就是 KLEE 可以执行的输入。

⸻

3. 运行 KLEE

执行：

```bash
$ klee get_sign.bc
```

KLEE 会输出类似结果：

```
KLEE: output directory = "klee-out-0"
KLEE: done: total instructions = 33
KLEE: done: completed paths = 3
KLEE: done: generated tests = 3
```

结果解读：
* completed paths = 3 ：三条不同执行路径
* generated tests = 3 ：三个对应测试用例

效果图示例（终端输出）：

（图片仅示意，可能与实际输出不同）

⸻

4. 输出目录

KLEE 的结果保存在 klee-out-0 文件夹，结构大致如下：

```
klee-out-0/
├── assembly.ll
├── info
├── run.stats
├── run.istats
├── test000001.ktest
├── test000002.ktest
└── test000003.ktest
```

其中：
* test00000X.ktest 就是生成的测试用例；
* run.stats 记录统计数据；
* info、messages.txt 用于调试和查看运行信息。

KLEE 还会自动生成一个 符号链接 klee-last 指向最新的结果目录，便于快速访问。

⸻

5. 可视化路径覆盖（进阶）

你可以用 ktest-tool 来查看测试用例内容：

```bash
$ ktest-tool klee-last/test000001.ktest
```

输出类似：

```
ktest file : 'klee-last/test000001.ktest'
args       : ['get_sign.bc']
num objects: 1
object 0: name: 'a'
object 0: size: 4
object 0: data: 00 00 00 00
```

这里 a = 0，对应 return 0 的路径。

⸻

6. 学习小结

步骤	学到的知识
测试驱动	使用 klee_make_symbolic() 让输入变成符号变量
编译	生成 LLVM bitcode .bc 文件供 KLEE 执行
运行 KLEE	自动探索不同路径，生成覆盖完整逻辑的测试用例
输出分析	查看 .ktest 文件，理解不同路径对应的输入


⸻

7. 下一步学习

当你掌握了这个小例子，可以继续学习：
* 符号化命令行参数、stdin、文件
* KLEE 测试正则表达式库
* 使用 KLEE 测试 GNU Coreutils

⸻

最后寄语

这就是 KLEE 的基本用法：
写测试驱动 → 编译 → 运行 KLEE → 得到自动生成的测试。

有了它，你可以轻松探索复杂程序的不同执行路径！🚀

⸻

👉 更多教程请见 [KLEE官方教程](https://klee-se.org/docs/#tutorials)。
