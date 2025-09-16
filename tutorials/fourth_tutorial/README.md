# KLEE 进阶教程：使用符号化环境（Using Symbolic Environment）

[官方教程](https://klee-se.org/tutorials/using-symbolic/)

本指南将帮助你理解 `-sym-arg`, `-sym-args`, `-sym-files`, `-sym-stdin`, `-sym-stdout` 等选项，学会如何在命令行参数、输入文件、标准输入/输出等场合下使用符号化数据，让 KLEE 探索更多可能的执行路径。

⸻

## 1. 教程目的与背景

许多程序不仅从标准输入（stdin）或命令行参数（argv）获得输入，也可能从磁盘文件读取数据。把这些输入**符号化（symbolic）**，KLEE 就能系统性地遍历不同输入组合，发现隐藏 bug。官方教程聚焦两个最常用的选项：`-sym-arg` 与 `-sym-files`，并顺带演示 `-sym-stdin`/`-sym-stdout` 的基本用法。 ￼

主要学习内容（官方要点）：
- **`-sym-arg N`**：提供一个长度为 N 的**符号命令行参数**（若配合 `-sym-args` 可一次生成多参数）。 ￼
- **`-sym-args MIN MAX N`**：至少 MIN、至多 MAX 个符号参数，每个最大长度 N。 ￼
- **`-sym-files NUM N`**：创建 NUM 个**符号文件**（命名依次为 `A`,`B`,`C`…），每个大小 N。 ￼
- **`-sym-stdin N`**：让**标准输入**成为大小为 N 的符号数据。 ￼
- **`-sym-stdout`**：让**标准输出**成为符号的（通常与文件/管道交互相关）。 ￼

> 这些功能需要 **`-posix-runtime`**，它为被测程序提供 POSIX 风格的运行时环境（文件、argv、stdin/stdout 等）。 ￼

⸻

## 2. 示例程序（`password.c`，命令行参数版）

教程先用“检查密码”的最小示例演示 `-sym-arg`： ￼

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

这个程序从 argv[1] 读入字符串并判断是否为 "hello"。如果是，就打印 “Password found!”。否则结束。

⸻

## 3. 编译程序到 LLVM Bitcode

要用 KLEE 分析该程序，你需要先将它编译成 LLVM bitcode（.bc 文件）。典型步骤如下：

```bash
clang -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone password.c
```

参数解释：
- **-emit-llvm -c**：生成 LLVM bitcode，不链接为可执行文件。
- **-g**：保留调试信息，以便 KLEE 可以给出源代码的行号等信息。
- **-O0 -Xclang -disable-O0-optnone**：关闭优化，但允许 KLEE 自己做一些优化／处理，以便符号执行更准确／可控。

⸻

## 4. 使用 `-sym-arg` 启动 KLEE

**-sym-arg N** 选项使得程序的一个命令行参数 argv[1] 变为符号字符串，长度 N。例如你想让这个参数长度为 5：

```bash
klee -posix-runtime password.bc -sym-arg 5
```

KLEE 会探索多条路径，其中一条路径令符号参数具体化为 "hello"，从而打印 Password found! 并生成对应测试用例。

也可以用 `-sym-args MIN MAX N` 一次生成多个符号参数（至少 `MIN`、至多 `MAX` 个；每个最大长度 `N`）。

注意：`-posix-runtime` 是必要的，因为它让 KLEE 使用一个 **POSIX**‐兼容的运行时来处理文件、输入输出、命令行参数等，这样 `-sym-arg`, `-sym-files` 等选项才能正常工作。  ￼

运行结果可能类似于：
- KLEE 探索若干条路径，其中一条路径能让 `argv[1]` 正好匹配 “hello”
- KLEE 输出消息、生成测试用例等  ￼

⸻

## 5. `-sym-files` 用法（含 `-sym-stdin`/`-sym-stdout`）

这里是一个新的，文件输入版的 password.c，如果命令行给出文件名就从该文件读；否则回退到标准输入： ￼

```c
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int check_password(int fd) {
  char buf[5];
  if (read(fd, buf, 5) != -1) {
    if (buf[0] == 'h' && buf[1] == 'e' &&
        buf[2] == 'l' && buf[3] == 'l' &&
        buf[4] == 'o')
      return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  int fd;
  if (argc >= 2) {
    if ((fd = open(argv[1], O_RDONLY)) != -1) {
      if (check_password(fd)) {
        printf("Password found in %s\n", argv[1]);
        close(fd);
        return 0;
      }
      close(fd);
      return 1;
    }
  }

  if (check_password(0)) { // 0 == stdin
    printf("Password found in standard input\n");
    return 0;
  }
  return 1;
}
```

### 5.1 用 `-sym-stdin` 驱动标准输入

先让 `stdin` 成为符号数据（例如 10 字节），这样程序即使没有成功打开文件也能从 `stdin` 读取“任意”内容： ￼

```bash
clang -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone password.c
klee -posix-runtime password.bc -sym-stdin 10
```

KLEE 会探索一条使得 `stdin` 前 5 个字节为 "hello" 的路径，并打印 **Password found in standard input**。

### 5.2 用 `-sym-files` 提供符号文件

`-sym-files <NUM> <N>` 会创建 `NUM` 个大小为 `N` 的符号文件，自动命名为 A,B,C…
例如只要 1 个大小 10 的文件，就这样启动，并把 `A` 当作命令行参数传给程序：

```bash
klee -posix-runtime password.bc A -sym-files 1 10
```

这会让程序从名为 `A` 的符号文件读取数据；某条路径上其前 `5` 字节具体化为 "hello"，从而打印 **Password found in A**。

`-sym-stdout` 也可以一起用，把标准输出当成符号化的“外部交互”，但在本示例中非必需。 ￼

⸻

## 6. 常见输出与小贴士
- 看到 *WARNING: undefined reference to function: printf/strlen/strncmp* 属于 POSIX 运行时包裹与外部符号的正常提示；教程示例的运行输出也包含这些行，无需惊慌。 ￼
- 记得每次运行可能生成不同的 klee-out-N 目录，可通过 klee-last 快捷查看上一次结果。
- 若路径爆炸，可配合 `--only-output-states-covering-new`, `-max-time`, `-max-forks` 等控制开销（这些在其他教程有详细介绍）。

⸻

## 7. 练习建议
- 练习 A：把 `-sym-args` 与 `-sym-files` 结合，既让程序拿到一个符号文件名（如 `A`），又让另一个参数变成符号字符串，观察路径数。
- 练习 B：把读取长度改为可变（例如先读取一个长度再读内容），配合 `-sym-stdin` 约束输入格式。
- 练习 C：加入错误处理路径（如 open 失败、read 返回 -1），观察 KLEE 如何在不同系统调用返回值上分叉。

⸻

👉 **下一站**：[测试 GNU Coreutils](https://klee-se.org/tutorials/testing-coreutils/) - 在真实世界的程序上实战！