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

### 4.1 回放测试用例（推荐：原生二进制 + klee-replay）

**为什么用 klee-replay？**
做法与教程1`不`一致（[Replaying a test case](https://klee-se.org/tutorials/testing-function/)）。这是因为当测试是用 POSIX runtime 生成的（比如命令行里用了 `-posix-runtime`、`-sym-arg`、`-sym-args`、`--sym-stdin`、`--sym-files` 等），.ktest 文件中记录的是“POSIX 语义”的输入：符号化的 argv、stdin、以及虚拟文件的内容与元数据，比如：`['password.bc', '-sym-arg', '5']`。
klee-replay 会解释这些 POSIX 语义，把 .ktest 中的对象正确注入到原生程序运行时（填好 argv、喂入 stdin、创建并填充文件等）。
而 -lkleeRuntest + KTEST_FILE=... 不会理解这些 POSIX 选项，它只会把 .ktest 的 args 字段直接当作字符串 argv 传给程序，导致你看到的现象：程序收到的 argv[1] 实际是字面量 "-sym-arg"，而不是被具体化的“hello…”，于是退出码变成 1。

小判别法：ktest-tool 若显示类似：
args : ['program.bc', '-sym-arg', '5'] / 出现 stdin/A-content 等对象 → 这是 POSIX 测试，用 klee-replay。

步骤：

```bash
# 在你的容器里
$ cd /tmp/klee_src/examples/password

# 1) 编译原生二进制（不要链接 -lkleeRuntest）
$ gcc -O0 -g password.c -o password_native && ls -l password_native
-rwxr-xr-x 1 klee klee 17152 Sep 20 00:43 password_native

# 2) 回放指定的 ktest（自动按 POSIX 语义注入 argv/stdin/files）。注意“EXIT STATUS: NORMAL”
$ klee-replay ./password_native klee-last/test000006.ktest
KLEE-REPLAY: NOTE: Test file: klee-last/test000006.ktest
KLEE-REPLAY: NOTE: Arguments: "./password_native" "hello"
KLEE-REPLAY: NOTE: Storing KLEE replay files in /tmp/klee-replay-GwYELV
KLEE-REPLAY: NOTE: EXIT STATUS: NORMAL (0 seconds)
KLEE-REPLAY: NOTE: removing /tmp/klee-replay-GwYELV
```

可选验证：

```bash
# 看看这个 ktest 是否属于 POSIX 测试（有 -sym-arg / stdin / A-content 等）
$ ktest-tool klee-last/test000006.ktest
ktest file : 'klee-last/test000006.ktest'
args       : ['password.bc', '-sym-arg', '5']
num objects: 1
object 0: name: 'arg00'
object 0: size: 6
object 0: data: b'hello\xff'
object 0: hex : 0x68656c6c6fff
object 0: text: hello.
```

该 test case 的确产生了`hello`的字符串。

⸻

## 5. `-sym-files` 用法（含 `-sym-stdin`/`-sym-stdout`）

这里是一个新的，文件输入版的 password_files.c，如果命令行给出文件名就从该文件读；否则回退到标准输入： ￼

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
clang -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone password_files.c
klee --libc=uclibc -posix-runtime password_files.bc -sym-stdin 10
```

KLEE 会探索一条使得 `stdin` 前 5 个字节为 "hello" 的路径，并打印 **Password found in standard input**。

#### 5.1.1 测试用例结果

```bash
# 看看这个 ktest 是否属于 POSIX 测试（有 -sym-arg / stdin / A-content 等）
$ ktest-tool klee-last/test000005.ktest
ktest file : 'klee-last/test000005.ktest'
args       : ['password_files.bc', '-sym-stdin', '10']
num objects: 2
object 0: name: 'stdin'
object 0: size: 10
object 0: data: b'hello\xff\xff\xff\xff\xff'
object 0: hex : 0x68656c6c6fffffffffff
object 0: text: hello.....
object 1: name: 'stdin_stat'
object 1: size: 144
object 1: data: b'w\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\xff\xff\xff\xff\x01\x00\x00\x00\x00\x00\x00\x00\xa4\x81\x00\x00\xe8\x03\x00\x00\xe8\x03\x00\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x00\x10\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff>\n\xceh\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xffL\x0c\xceh\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xffL\x0c\xceh\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
object 1: hex : 0x770000000000000001000000ffffffff0100000000000000a4810000e8030000e8030000ffffffff0000000000000000ffffffffffffffff0010000000000000ffffffffffffffff3e0ace6800000000ffffffffffffffff4c0cce6800000000ffffffffffffffff4c0cce6800000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
object 1: text: w.......................................................................>..h............L..h............L..h....................................
```

**结果解析**：

- 两个对象是什么？
- stdin（size=10）：符号化标准输入的内容。前 5 个字节被约束为 "hello"；后 5 个字节程序未使用，保持不受约束，因此被实例化为 0xff 占位。
- stdin_stat（size=144）：fd=0 的元数据（类似 fstat 信息，mode/uid/gid/timestamps 等）。你的程序不读它，所以绝大多数字段保持不受约束（很多 0xff）。
- 它如何满足路径条件？
	1.	argc < 2（没有文件名参数）→ 走 check_password(0) 分支。
	2.	read(0, buf, 5) != -1（POSIX 模型里有足够字节可读）。
	3.	约束：buf[0..4] == "hello"。
求解器据此给 stdin[0..4] 具体化为 68 65 6c 6c 6f，其余字节自由，随意取 ff。
- 和 angr 的对应：
等价于 state.posix.stdin = BVS(8*10) 并添加约束 stdin[0..4] == b"hello"；后续字节未约束。

### 5.1.2 回放测试用例

步骤：

```bash
# 在你的容器里
$ cd /tmp/klee_src/examples/password

# 1) 编译原生二进制（不要链接 -lkleeRuntest）
$ gcc -O0 -g password_files.c -o password_files_native && ls -l password_files_native
-rwxr-xr-x 1 klee klee 18032 Sep 20 02:10 password_files_native

# 2) 回放指定的 ktest（自动按 POSIX 语义注入 argv/stdin/files）。注意“EXIT STATUS: NORMAL”
$ klee-replay ./password_files_native klee-last/test000005.ktest
KLEE-REPLAY: NOTE: Test file: klee-last/test000005.ktest
KLEE-REPLAY: NOTE: Arguments: "./password_files_native"
KLEE-REPLAY: NOTE: Storing KLEE replay files in /tmp/klee-replay-w2KhIR
KLEE-REPLAY: NOTE: Creating file /tmp/klee-replay-w2KhIR/fd0 of length 10
KLEE-REPLAY: NOTE: EXIT STATUS: NORMAL (0 seconds)
KLEE-REPLAY: NOTE: removing /tmp/klee-replay-w2KhIR
$ klee-replay ./password_files_native klee-last/test000006.ktest
KLEE-REPLAY: NOTE: Test file: klee-last/test000006.ktest
KLEE-REPLAY: NOTE: Arguments: "./password_files_native"
KLEE-REPLAY: NOTE: Storing KLEE replay files in /tmp/klee-replay-FtlB4n
KLEE-REPLAY: NOTE: Creating file /tmp/klee-replay-FtlB4n/fd0 of length 10
KLEE-REPLAY: NOTE: EXIT STATUS: ABNORMAL 1 (0 seconds)
KLEE-REPLAY: NOTE: removing /tmp/klee-replay-FtlB4n
```

### 5.2 用 `-sym-files` 提供符号文件

`-sym-files <NUM> <N>` 会创建 `NUM` 个大小为 `N` 的符号文件，自动命名为 A,B,C…
例如只要 1 个大小 10 的文件，就这样启动，并把 `A` 当作命令行参数传给程序：

```bash
klee --libc=uclibc -posix-runtime password_files.bc A -sym-files 1 10
```

这会让程序从名为 `A` 的符号文件读取数据；某条路径上其前 `5` 字节具体化为 "hello"，从而打印 **Password found in A**。

`-sym-stdout` 也可以一起用，把标准输出当成符号化的“外部交互”，但在本示例中非必需。 ￼

#### 5.2.1 测试用例结果

```bash
# 看看这个 ktest 是否属于 POSIX 测试（有 -sym-arg / stdin / A-content / -sym-files 等）
$ ktest-tool klee-last/test000005.ktest
ktest file : 'klee-last/test000005.ktest'
args       : ['password_files.bc', 'A', '-sym-files', '1', '10']
num objects: 2
object 0: name: 'A_data'
object 0: size: 10
object 0: data: b'hello\xff\xff\xff\xff\xff'
object 0: hex : 0x68656c6c6fffffffffff
object 0: text: hello.....
object 1: name: 'A_data_stat'
object 1: size: 144
object 1: data: b'w\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\xff\xff\xff\xff\x01\x00\x00\x00\x00\x00\x00\x00\xa4\x81\x00\x00\xe8\x03\x00\x00\xe8\x03\x00\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x00\x10\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xffW\r\xceh\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x99\x0f\xceh\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x99\x0f\xceh\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff'
object 1: hex : 0x770000000000000001000000ffffffff0100000000000000a4810000e8030000e8030000ffffffff0000000000000000ffffffffffffffff0010000000000000ffffffffffffffff570dce6800000000ffffffffffffffff990fce6800000000ffffffffffffffff990fce6800000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
object 1: text: w.......................................................................W..h...............h...............h....................................
```

**说明**：

- args：['password_files.bc', 'A', '-sym-files', '1', '10']
表示 POSIX runtime 创建了 1 个大小为 10 字节、名为 A 的符号文件，并把 "A" 作为 argv[1] 传给程序。
- A_data（size=10）：文件 A 的内容。
前 5 字节被求解为 "hello" 以满足分支；后 5 字节程序未使用，保持不受约束，实例化为 0xff。
- A_data_stat（size=144）：文件 A 的元数据（相当于 struct stat：类型、权限、时间戳等）。
程序不读这些字段，所以多数保持不受约束（大量 0xff）；KLEE 仅保证“存在、可读、长度=10”等使 open/read 成功的最小一致性。
- 为何满足路径条件
	1.	argc >= 2 且 argv[1]=="A"；
	2.	open("A", O_RDONLY) != -1；
	3.	read(fd, buf, 5) != -1 且读到的 buf[0..4] == "hello"；
⇒ check_password 返回 1，主程序返回 0。
- 与 angr 类比：等价于 SimFile('A', content=BVS(80))，并加约束 content[0..4]=="hello"；其余字节自由。

### 5.2.2 回放测试用例

步骤：

```bash
# 在你的容器里
$ cd /tmp/klee_src/examples/password

# 1) 编译原生二进制（不要链接 -lkleeRuntest）
$ gcc -O0 -g password_files.c -o password_files_native && ls -l password_files_native
-rwxr-xr-x 1 klee klee 18032 Sep 20 02:10 password_files_native

# 2) 回放指定的 ktest（自动按 POSIX 语义注入 argv/stdin/files）。注意“EXIT STATUS: NORMAL”
$ klee-replay ./password_files_native klee-last/test000005.ktest
KLEE-REPLAY: NOTE: Test file: klee-last/test000005.ktest
KLEE-REPLAY: NOTE: Arguments: "./password_files_native"
KLEE-REPLAY: NOTE: Storing KLEE replay files in /tmp/klee-replay-w2KhIR
KLEE-REPLAY: NOTE: Creating file /tmp/klee-replay-w2KhIR/fd0 of length 10
KLEE-REPLAY: NOTE: EXIT STATUS: NORMAL (0 seconds)
KLEE-REPLAY: NOTE: removing /tmp/klee-replay-w2KhIR
$ klee-replay ./password_files_native klee-last/test000006.ktest
KLEE-REPLAY: NOTE: Test file: klee-last/test000006.ktest
KLEE-REPLAY: NOTE: Arguments: "./password_files_native"
KLEE-REPLAY: NOTE: Storing KLEE replay files in /tmp/klee-replay-FtlB4n
KLEE-REPLAY: NOTE: Creating file /tmp/klee-replay-FtlB4n/fd0 of length 10
KLEE-REPLAY: NOTE: EXIT STATUS: ABNORMAL 1 (0 seconds)
KLEE-REPLAY: NOTE: removing /tmp/klee-replay-FtlB4n
```

**结果解析**：

- args：['password_files.bc', 'A', '-sym-files', '1', '10']
表示 POSIX runtime 创建了 1 个大小为 10 字节、名为 A 的符号文件（暂存于诸如 `/tmp/klee-replay-pHW7Aq/A` 的路径），并把 "A" 作为 argv[1] 传给程序。
- A_data（size=10）：文件 A 的内容。
前 5 字节被求解为 "hello" 以满足分支；后 5 字节程序未使用，保持不受约束，实例化为 0xff。
- A_data_stat（size=144）：文件 A 的元数据（相当于 struct stat：类型、权限、时间戳等）。
程序不读这些字段，所以多数保持不受约束（大量 0xff）；KLEE 仅保证“存在、可读、长度=10”等使 open/read 成功的最小一致性。
- 为何满足路径条件
	1.	argc >= 2 且 argv[1]=="A"；
	2.	open("A", O_RDONLY) != -1；
	3.	read(fd, buf, 5) != -1 且读到的 buf[0..4] == "hello"；
⇒ check_password 返回 1，主程序返回 0。
- 与 angr 类比：等价于 SimFile('A', content=BVS(80))，并加约束 content[0..4]=="hello"；其余字节自由。

**查看打开文件**

```bash
# 的确打开文件“/tmp/klee-replay-pHW7Aq/A”并写入/读入了 "hello"
$ strace -f -e openat,read,write,close,unlink -s 64 -o /tmp/aaa klee-replay ./password_files_native klee-last/test000005.ktest >/dev/null 2>&1 && cat /tmp/aaa | grep hello -B1 -A1
1121  openat(AT_FDCWD, "/tmp/klee-replay-pHW7Aq/A", O_RDWR|O_CREAT, 0644) = 3
1121  write(3, "hello\377\377\377\377\377", 10) = 10
1121  close(3)                          = 0
--
1123  openat(AT_FDCWD, "A", O_RDONLY)   = 3
1123  read(3, "hello", 5)               = 5
1123  close(3)                          = 0
```

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