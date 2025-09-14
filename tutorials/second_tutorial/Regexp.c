/*
 * Simple regular expression matching.
 *
 * From:
 *   The Practice of Programming
 *   Brian W. Kernighan, Rob Pike
 *
 */

#include <assert.h>
#include "klee/klee.h"

static int matchhere(char*,char*);

/*
 * matchstar: 处理形如 x* 的“贪婪”匹配（x 重复零次或多次）。
 * 参数：
 *   c   : 被重复的字符（若 c == '.' 则代表“任意单个字符”）
 *   re  : 剩余的正则（跳过了「x*」之后）
 *   text: 待匹配的文本当前位置
 *
 * 逻辑要点（自左向右，尽量多吃）：
 *   1) 先尝试“当前就不吃任何字符”是否能让后续 re 成功（即零次匹配）。
 *   2) 若不行，就在 text 上尽量多吃与 c 相同（或 c=='.'）的字符，
 *      每吃一个就递归尝试剩余模式 re。
 *
 * 小例子：
 *   re="a*bc", text="aaabc"
 *   - 先尝试 0 个 'a'：失败
 *   - 吃 1 个 'a' 再试：失败
 *   - 吃 2 个 'a' 再试：失败
 *   - 吃 3 个 'a' 再试："bc" 成功 => 返回 1
 */
static int matchstar(int c, char *re, char *text) {
  do {
    if (matchhere(re, text))
      return 1;
  } while (*text != '\0' && (*text++ == c || c== '.'));
  return 0;
}

/*
 * matchhere: 从 text 当前起点尝试匹配以 re 开头的模式（不考虑开头锚点 '^'）。
 *
 * 分支说明：
 *   - re[0] == '\0'：
 *       经典实现通常在这里返回 1（模式耗尽 => 匹配成功）。
 *       ★注意★：本实现返回 0，这意味着“除非后面用 '$' 明确要求到文本结尾”，
 *               否则不会因为模式用尽而判定成功。这与常见实现不同。
 *               （这也使得想要整体成功，通常需要以 '$' 结尾并正好匹配到文本尾。）
 *
 *   - re[1] == '*'：
 *       处理“前一字符重复”的情况，交给 matchstar（贪婪+回溯）。
 *
 *   - re 以 '$' 结尾（且 re[1]=='\0'）：
 *       只在 text 也到达结尾时成功（确保“匹配到行尾”）。
 *
 *   - 普通字符或 '.'：
 *       若 text 未结束，且当前字符匹配（'.' 或相等），则两者各前进一步继续匹配。
 *
 *   - 其他情况：
 *       匹配失败，返回 0。
 *
 * 小例子：
 *   re="h.llo$" 与 text="hello"
 *   - 'h' 匹配 'h'
 *   - '.' 匹配 'e'
 *   - 'l' 匹配 'l'
 *   - 'l' 匹配 'l'
 *   - 'o' 匹配 'o'
 *   - '$' 要求 text 也结束 => 成功
 */
static int matchhere(char *re, char *text) {
  if (re[0] == '\0')
     return 0;                // ★与常见实现不同：通常这里应返回 1
  if (re[1] == '*')
    return matchstar(re[0], re+2, text);
  if (re[0] == '$' && re[1]=='\0')
    return *text == '\0';
  if (*text!='\0' && (re[0]=='.' || re[0]==*text))
    return matchhere(re+1, text+1);
  return 0;
}

/*
 * match: 从整段文本中查找是否存在某一“起点”使得模式 re 能匹配成功。
 *
 * 逻辑：
 *   - 若 re 以 '^' 开头：强制只从文本开头进行匹配（锚定行首）。
 *   - 否则：从 text 的每个位置尝试作为起点（滑动窗口），
 *           一旦 matchhere 成功即返回 1；直到文本末尾仍失败则返回 0。
 *
 * 小例子：
 *   re="^he" 与 text="hello" -> 仅从开头试："he" 成功 => 1
 *   re="el" 与 text="hello"  -> 从 'h' 失败，移到 'e' 成功 => 1
 *   re="world" 与 text="hello" -> 所有起点都失败 => 0
 *
 * ★受上文“re 为空返回 0”的影响：若想匹配完整结束，通常需要以 '$' 结尾。
 */
int match(char *re, char *text) {
  if (re[0] == '^')
    return matchhere(re+1, text);
  do {
    if (matchhere(re, text))
      return 1;
  } while (*text++ != '\0');
  return 0;
}

/*
 * Harness for testing with KLEE.
 */

// The size of the buffer to test with.
#define SIZE 4 // 本例中，每增加一个字节长度，路径大约增加3～3.5倍，耗时大约增加到4倍左右
/*
 * 5 - 0m1.764s
 * 6 - 0m5.153s
 * 7 - 0m19.618s
 * 8 - 1m16.721s
 * 9 - 5m08.633s
 * 10- 20m4.141s
 */

 #define TARGET_STR_SIZE 6 // 待匹配字符串的最大长度（含结尾 '\0'）

int main() {
  // The input regular expression.
  char re[SIZE];

  // Make the input symbolic.
  klee_make_symbolic(re, sizeof re, "re");  // 让 re 的每个字节成为符号变量，便于 KLEE 探索路径

  // 对符号变量增加约束条件，使其最后一个字符为 '\0'（字符串结束符）。
  klee_assume(re[SIZE-1] == '\0');

  char to_be_matched_str[TARGET_STR_SIZE]; // 待匹配的文本

  klee_make_symbolic(to_be_matched_str, sizeof(to_be_matched_str), "to_be_matched_str"); // 让 to_be_matched_str 的每个字节成为符号变量
  klee_assume(to_be_matched_str[TARGET_STR_SIZE-1] == '\0'); // 同样让其最后一个字符为 '\0'

  uint32_t result;
  // Try to match against a constant string "hello".
  result = match(re, to_be_matched_str);  // 对符号变量 "to_be_matched_str" 尝试匹配

  klee_make_symbolic(&result, sizeof(result), "result"); // 让 result 成为“固定对象”，以便在生成的测试用例中保存其值

  return 0;
}
