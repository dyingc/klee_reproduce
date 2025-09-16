# KLEE 进阶教程：使用符号化环境（Using Symbolic Environment）

[官方教程](https://klee-se.org/tutorials/using-symbolic/)

本指南将帮助你理解 `-sym-arg`, `-sym-args`, `-sym-files`, `-sym-stdin`, `-sym-stdout` 等选项。如何在命令行、输入文件、标准输入／输出等场合下使用符号化数据，从而让程序的这些部分成为符号变量，KLEE 可以探索各种可能的输入组合。

⸻

## 1. 教程目的与背景

许多程序不仅从标准输入（stdin）或者命令行参数（argv）获得输入，也可能从文件或多个参数读取数据。这些输入如果能被设为“符号的”（symbolic），KLEE 就能探索更多路径、发现更多潜在 bug。本教程介绍的是如何使用 KLEE 的符号环境选项来做到这一点。  ￼

主要学习内容：
- **-sym-arg N**：让 argv[1] 成为一个长度为 N 的符号字符串。
- **-sym-args MIN MAX N**：至少 MIN 个、最多 MAX 个符号参数，每个最长为 N。
- **-sym-files NUM N**：创建 NUM 个符号文件（例如 “A”, “B”, “C”…）每个大小为 N。
- **-sym-stdin N**：使标准输入成为符号化，大小 N。
- **-sym-stdout**：使标准输出也是符号的。使用这些可以让程序与外部交互的部分更灵活被探索。  ￼

⸻

2. 示例程序：检查密码（`password.c`）

官方教程中用这样一个程序做示例：

```c
#include <stdio.h>

int check_password(char *buf) {
  if (buf[0] == 'h' && buf[1] == 'e' &&
      buf[2] == 'l' && buf[3] == 'l' &&
      buf[4] == 'o')
    return 1;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2)
     return 1;

  if (check_password(argv[1])) {
    printf("Password found!\n");
    return 0;
  }

  return 1;
}
```

这个程序从命令行参数读取一个字符串 argv[1]，然后检查它是不是 “hello”。如果是，就打印 “Password found!”。否则结束。  ￼

⸻

3. 编译程序到 LLVM 位码

要用 KLEE 分析该程序，你需要先将它编译成 LLVM bitcode（.bc 文件）。典型步骤如下：

```bash
clang -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone password.c
```

参数解释：
- **-emit-llvm -c**：生成 LLVM bitcode，不链接为可执行文件。
- **-g**：保留调试信息，以便 KLEE 可以给出源代码的行号等信息。
- **-O0 -Xclang -disable-O0-optnone**：关闭优化，但允许 KLEE 自己做一些优化／处理，以便符号执行更准确／可控。

⸻

4. 使用 `-sym-arg` 启动 KLEE

**-sym-arg N** 选项使得程序的一个命令行参数 argv[1] 变为符号字符串，长度 N。例如你想让这个参数长度为 5：

```bash
klee -posix-runtime password.bc -sym-arg 5
```

注意：`-posix-runtime` 是必要的，因为它让 KLEE 使用一个 **POSIX**‐兼容的运行时来处理文件、输入输出、命令行参数等，这样 `-sym-arg`, `-sym-files` 等选项才能正常工作。  ￼

运行结果可能类似于：
- KLEE 探索若干条路径，其中一条路径能让 `argv[1]` 正好匹配 “hello”
- KLEE 输出消息、生成测试用例等  ￼

⸻

5. 其他符号环境选项

除了 `-sym-arg`，还有这些选项：

|          选项           |                          含义                               |
|------------------------|------------------------------------------------------------|
| **sym-args MIN MAX N** | 至少 MIN 个、最多 MAX 个符号参数，每个最长为 N。                 |
| **-sym-files NUM N**   | 创建 NUM 个符号文件（例如 “A”, “B”, “C”…），每个大小为 N。        |
| **-sym-stdin N**       | 使标准输入成为符号化，大小为 N。                                |
| **-sym-stdout**        | 使标准输出也是符号的。使用这些可以让程序与外部交互的部分更灵活被探索。 |

---

👉 **下一站**：[测试 GNU Coreutils](https://klee-se.org/tutorials/testing-coreutils/) - 在真实世界的程序上实战！