# KLEE 进阶教程：测试正则表达式库

[官方教程](https://klee-se.org/tutorials/testing-regex/)

欢迎来到 KLEE 的进阶教程！本 README 将通过一个正则表达式匹配函数的例子，展示如何使用 **KLEE** 发现程序中的内存错误，以及如何使用 KLEE 的高级特性。

---

## 1. 示例程序概览

我们要测试的是一个简单的正则表达式匹配函数，它支持以下模式：

| 模式 | 含义 | 示例 |
|------|------|------|
| `c` | 匹配字符 c | `a` 匹配 "a" |
| `.` | 匹配任意字符 | `.` 匹配 "x" |
| `^` | 匹配字符串开头 | `^hello` 匹配 "hello world" |
| `$` | 匹配字符串结尾 | `world$` 匹配 "hello world" |
| `*` | 前一个字符重复 0 或多次 | `a*` 匹配 "", "a", "aaa" |

核心匹配逻辑示意：

```
         match()
            |
            v
       matchhere()
       /    |    \
      /     |     \
    普通  星号*   结束$
   字符  matchstar()
```

源代码位于 KLEE 源码树的 `examples/regexp` 目录下。

⸻

## 2. 编写测试驱动

初始版本的测试驱动（有 bug 的版本）：

```c
#include "klee/klee.h"

#define SIZE 7

int match(char *re, char *text);

int main() {
  // 输入的正则表达式
  char re[SIZE];

  // 让输入变成符号变量
  klee_make_symbolic(re, sizeof re, "re");

  // 尝试匹配固定字符串 "hello"
  match(re, "hello");

  return 0;
}
```

⚠️ **问题预警**：这个版本存在一个问题，我们稍后会修复它！

⸻

## 3. 编译程序

从 `examples/regexp` 目录执行：

```bash
$ clang -I ../../include -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone Regexp.c
```

参数解释：
- `-I ../../include`：让编译器找到 `klee/klee.h`
- `-emit-llvm`：生成 LLVM 位码
- `-c`：只编译不链接
- `-g`：包含调试信息
- `-O0 -Xclang -disable-O0-optnone`：禁用优化但允许 KLEE 自行优化

验证编译结果：

```bash
$ llvm-nm Regexp.bc
U klee_make_symbolic
---------------- T main
---------------- T match
---------------- t matchhere
---------------- t matchstar
```

⸻

## 4. 使用 KLEE 执行（发现错误）

运行 KLEE：

```bash
$ klee --only-output-states-covering-new Regexp.bc
```

输出结果：

```
KLEE: output directory = "klee-out-0"
KLEE: ERROR: Regexp.c:23: memory error: out of bound pointer
KLEE: NOTE: now ignoring this error at this location
KLEE: ERROR: Regexp.c:25: memory error: out of bound pointer
KLEE: NOTE: now ignoring this error at this location
KLEE: done: total instructions = 4848112
KLEE: done: completed paths = 6675
KLEE: done: partially completed paths = 763
KLEE: done: generated tests = 16
```

### 结果分析

| 指标 | 数值 | 含义 |
|------|------|------|
| 🔴 **错误数** | 2 | 发现了两个内存越界错误 |
| 📊 **执行指令数** | ~480万 | KLEE 模拟执行的指令总数 |
| 🛤️ **完成路径数** | 6675 | 完整探索的执行路径 |
| ⚠️ **部分完成路径** | 763 | 因错误而中断的路径 |
| 🧪 **生成测试数** | 16 | 覆盖新代码的测试用例 |

💡 **提示**：使用 `--only-output-states-covering-new` 避免生成重复测试（否则会生成 6677 个！）

⸻

## 5. 错误报告分析

KLEE 为每个错误生成 `.err` 文件，让我们看看其中一个：

```
Error: memory error: out of bound pointer
File: .../klee/examples/regexp/Regexp.c
Line: 23
Stack:
#0 00000146 in matchhere (re=14816471, text=14815301)
#1 00000074 in matchstar (c, re=14816471, text=14815301)
...
#9 00000327 in main ()
Info:
  address: 14816471
  next: object at 14816624 of size 4
  prev: object at 14816464 of size 7
```

### KLEE 可检测的错误类型

| 错误类型 | 文件后缀 | 说明 |
|----------|----------|------|
| **ptr** | `.ptr.err` | 无效内存访问 |
| **free** | `.free.err` | 重复或无效的 free() |
| **abort** | `.abort.err` | 程序调用了 abort() |
| **assert** | `.assert.err` | 断言失败 |
| **div** | `.div.err` | 除零错误 |
| **user** | `.user.err` | KLEE 使用错误 |

⸻

## 6. 修复测试驱动

### 问题根源

❌ **问题**：正则表达式缓冲区完全符号化，但 `match` 函数期望以 `\0` 结尾的字符串！

### 解决方案一：显式设置结束符

```c
int main() {
  char re[SIZE];
  klee_make_symbolic(re, sizeof re, "re");

  // ✅ 确保字符串以 \0 结尾
  re[SIZE - 1] = '\0';

  match(re, "hello");
  return 0;
}
```

### 解决方案二：使用 klee_assume

```c
int main() {
  char re[SIZE];
  klee_make_symbolic(re, sizeof re, "re");

  // ✅ 假设最后一个字符是 \0
  klee_assume(re[SIZE - 1] == '\0');

  match(re, "hello");
  return 0;
}
```

### 两种方案对比

| 特性 | 方案一（显式赋值） | 方案二（klee_assume） |
|------|-------------------|---------------------|
| **简单性** | ⭐⭐⭐ 更直观 | ⭐⭐ 需理解约束 |
| **灵活性** | ⭐⭐ 固定修改 | ⭐⭐⭐ 可添加复杂约束 |
| **测试用例** | 最后字节可为任意值 | 强制最后字节为 '\0' |
| **适用场景** | 简单约束 | 复杂条件组合 |

⸻

## 7. 高级用法：klee_assume

`klee_assume` 可以添加更复杂的约束：

```c
// 示例1：排除以 ^ 开头的正则表达式
klee_assume(re[0] != '^');

// 示例2：限制只测试小写字母
for (int i = 0; i < SIZE-1; i++) {
  klee_assume(re[i] >= 'a' && re[i] <= 'z' || re[i] == '\0');
}

// 示例3：确保至少包含一个 *
int has_star = 0;
for (int i = 0; i < SIZE-1; i++) {
  if (re[i] == '*') has_star = 1;
}
klee_assume(has_star);
```

⚠️ **注意事项**：
- 避免在 `klee_assume` 中使用 `&&` 和 `||`（会产生分支）
- 使用 `&` 和 `|` 代替短路运算符
- 尽量拆分为多个简单的 `klee_assume` 调用

⸻

## 8. 运行时控制选项

KLEE 提供多种选项控制执行：

### 时间和资源限制

```bash
# 限制运行时间为 10 分钟
$ klee -max-time=10min Regexp.bc

# 限制符号分支数为 100
$ klee -max-forks=100 Regexp.bc

# 限制内存使用为 2GB
$ klee -max-memory=2048 Regexp.bc
```

### 测试生成策略

```bash
# 只生成覆盖新代码的测试（推荐）
$ klee --only-output-states-covering-new Regexp.bc

# 为所有错误路径生成测试
$ klee --emit-all-errors Regexp.bc
```

⸻

## 9. 结果验证

修复后重新运行 KLEE：

```bash
$ klee --only-output-states-covering-new Regexp.bc
KLEE: output directory = "klee-out-1"
KLEE: done: total instructions = 4848112
KLEE: done: completed paths = 7438
KLEE: done: generated tests = 16
```

✅ **成功！** 没有内存错误了！

查看生成的测试用例：

```bash
$ ktest-tool klee-last/test000001.ktest
ktest file : 'klee-last/test000001.ktest'
args       : ['Regexp.bc']
num objects: 1
object 0: name: 're'
object 0: size: 7
object 0: data: b'hello\x00\x00'
```

⸻

## 10. 学习总结

### 关键技能掌握

| 学习要点 | 掌握内容 |
|----------|----------|
| 🔍 **错误检测** | KLEE 能自动发现内存越界、空指针等错误 |
| 🛠️ **测试驱动修复** | 理解符号执行对输入的要求（如字符串结束符） |
| 🎯 **约束添加** | 使用 klee_assume 精确控制测试范围 |
| ⚙️ **运行控制** | 掌握时间、内存、路径数等限制选项 |
| 📊 **结果分析** | 解读错误报告和调用栈信息 |

### 最佳实践建议

1. **始终确保字符串正确结束** - C 字符串函数需要 `\0` 结束符
2. **使用简单的约束表达式** - 避免复杂的布尔运算
3. **限制测试生成数量** - 使用 `--only-output-states-covering-new`
4. **设置合理的资源限制** - 防止 KLEE 运行时间过长
5. **仔细分析错误报告** - 理解错误的根本原因

⸻

## 11. 扩展练习

试试这些挑战来加深理解：

### 🏆 初级挑战
- 修改测试驱动，测试不同长度的正则表达式
- 添加约束，只生成包含 `.` 和 `*` 的测试用例

### 🏆 中级挑战
- 让匹配的文本也变成符号变量
- 使用 klee_assert 添加自定义断言

### 🏆 高级挑战
- 分析 KLEE 生成的所有测试用例，找出边界情况
- 比较不同约束策略对路径探索的影响

⸻

## 12. 下一步学习

掌握了正则表达式测试后，你可以继续学习：

- 📚 **测试 GNU Coreutils** - 真实世界的命令行工具
- 🔧 **使用 KLEE 选项** - 深入了解搜索策略
- 🎯 **编写复杂约束** - 掌握符号执行的精髓
- 📊 **覆盖率分析** - 评估测试的完整性

⸻

### 🚀 结语

通过这个教程，你已经学会了：
- 使用 KLEE 自动发现内存错误
- 编写正确的测试驱动
- 使用 klee_assume 添加约束
- 分析和解决符号执行中的问题

KLEE 不仅能生成测试，更能帮你发现隐藏的 bug！继续探索，让你的代码更加健壮！

⸻

👉 更多教程请见 [KLEE官方文档](https://klee-se.org/docs/)