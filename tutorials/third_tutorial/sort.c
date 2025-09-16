/*
 * 教程核心目标：
 * 1) 把长度为 4 的 int 数组整体设为“符号输入”（symbolic），让 KLEE 自动探索所有可能取值；
 * 2) 用两种排序（插入排序 vs. 冒泡排序）处理同一输入；
 * 3) 通过断言 assert(temp1[i] == temp2[i]) 交叉验证两种实现是否等价；
 * 4) 如果实现不等价（本文件的 bubble_sort 故意“只做一趟”→ 有缺陷），KLEE 会生成能触发断言失败的反例。
 *
 * 说明：为避免在符号实参上传给外部库函数（printf）时触发 KLEE 的外部调用策略，
 * 本版本将所有 printf 改为 KLEE 内建的输出：klee_warning / klee_print_expr。
 * 这两者由 KLEE 提供，不会丢失符号性，也不需要放宽 --external-calls。
 *
 * 进阶补充（可选）：
 * - 路径空间太大时，可以对输入加范围约束（klee_assume），例如 [-10, 10]，以减少状态爆炸。
 * - 运行时常用选项：--only-output-states-covering-new、-max-time、-max-forks 等，详见官方文档。
 */

#include "klee/klee.h"  // klee_make_symbolic / klee_assume / klee_print_expr / klee_warning / klee_assert

#include <string.h>     // memcpy()/memmove()：clang 会降为 LLVM 内建，KLEE 能处理
#include <assert.h>     // assert()，但通常推荐 klee_assert(0) 更纯粹

/* ----- 回放适配层：REPLAY 下禁用 KLEE 的日志/具体化输出 ----- */
/* ----- 回放适配层：REPLAY 下禁用 KLEE 的日志/具体化输出 ----- */
#ifdef REPLAY
  /* 回放时不需要这些日志；把它们编译成 no-op */
  #define klee_warning(msg)        ((void)0)
  #define klee_warning_once(msg)   ((void)0)

  /* 回放时输入已是具体值；把 get_value 变成恒等宏，避免与头文件声明冲突 */
  #define klee_get_value_i32(x)    (x)

  /* 回放时静音数组转储：给出“同名、同签名”的空实现即可（不要用宏替换函数名） */
  static inline void dump_array_klee(const char *title, const int *a, unsigned n) {
    (void)title; (void)a; (void)n;
  }
#else

  /* ==========
  * 一行数组打印（十六进制），无 libc、无堆内存
  * 设计要点：
  *  - 用 klee_get_value_i32 将元素在当前路径上具体化；
  *  - 在栈上申请足够的缓冲区（VLA），拼接字符串；
  *  - 最后用 klee_warning(line) 打印一整行。
  * ========== */

  static inline void append_char(char *buf, unsigned *p, char c) { buf[(*p)++] = c; }

  static void append_hex_u32(char *buf, unsigned *p, unsigned v) {
    static const char HEX[] = "0123456789abcdef";
    append_char(buf, p, '0'); append_char(buf, p, 'x');
    int started = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
      unsigned nib = (v >> shift) & 0xF;
      if (nib || started || shift == 0) { append_char(buf, p, HEX[nib]); started = 1; }
    }
  }

  static void dump_array_klee(const char *title, const int *a, unsigned n) {
    char line[12 * (n ? n : 1) + 32];
    unsigned pos = 0;

    for (const char *t = title; *t; ++t) append_char(line, &pos, *t);
    append_char(line, &pos, ' '); append_char(line, &pos, '=');
    append_char(line, &pos, ' '); append_char(line, &pos, '[');

    for (unsigned i = 0; i < n; ++i) {
      if (i) { append_char(line, &pos, ','); append_char(line, &pos, ' '); }
      unsigned vi = (unsigned)klee_get_value_i32(a[i]);
      append_hex_u32(line, &pos, vi);
    }

    append_char(line, &pos, ']');
    line[pos] = '\0';
    klee_warning(line);
  }
#endif

/*
 * insert_ordered：
 *  - 把 item 插入到“当前已有 i 个有序元素”的 temp 数组中，使之仍然有序。
 *  - 用 memmove 把 [i, nelem-1] 整段右移一个元素，然后 array[i] = item。
 *  - 假设 array[0..nelem-1] 的“有效区间”已是有序的（由调用者保证）。
 */
static void insert_ordered(int *array, unsigned nelem, int item) {
  unsigned i = 0;
  for (; i != nelem; ++i) {
    if (item < array[i]) {
      memmove(&array[i+1], &array[i], sizeof(*array) * (nelem - i));
      break;
    }
  }
  array[i] = item;  // 如果 item 最大，i==nelem，直接放在末尾
}

/*
 * bubble_sort（有意残缺实现）：
 *  - 正常冒泡需多趟扫描，直到某趟无交换为止；
 *  - 此处实现无论是否有交换，都立刻 break → 只做一趟；
 *  - 因此常不能完全排序，存在缺陷。
 */
void bubble_sort(int *array, unsigned nelem) {
  for (;;) {
    int done = 1;
    for (unsigned i = 0; i + 1 < nelem; ++i) {
      if (array[i+1] < array[i]) {
        int t = array[i+1]; array[i+1] = array[i]; array[i] = t;
        done = 0;
      }
    }
    break;  // ⚠️ 有意：直接退出 → 只做一趟
  }
}

/*
 * insertion_sort：
 *  - 经典插入排序：临时数组 temp，前 i 个元素保持有序；
 *  - 每次将 array[i] 插入 temp[0..i-1]；
 *  - 最后复制回 array。
 *  - 本实现用 VLA（栈上变长数组），避免 malloc/free。
 */
void insertion_sort(int *array, unsigned nelem) {
  int temp[nelem];
  for (unsigned i = 0; i != nelem; ++i)
    insert_ordered(temp, i, array[i]);
  memcpy(array, temp, sizeof(*array) * nelem);
}

/*
 * test：
 *  - 拷贝同一输入到 temp1/2，分别执行插入排序与冒泡排序；
 *  - 如果结果不一致，打印输入/两种结果，然后 klee_assert(0)；
 *  - 否则保持安静通过。
 */
void test(int *array, unsigned nelem) {
  int temp1[nelem];
  int temp2[nelem];

  memcpy(temp1, array, sizeof(*array) * nelem);
  memcpy(temp2, array, sizeof(*array) * nelem);

  insertion_sort(temp1, nelem);
  bubble_sort(temp2, nelem);

  for (unsigned i = 0; i != nelem; ++i) {
    if (temp1[i] != temp2[i]) {
      klee_warning("Mismatch found; dumping arrays:");
      dump_array_klee("input",          array, nelem);
      dump_array_klee("insertion_sort", temp1,  nelem);
      dump_array_klee("bubble_sort",    temp2,  nelem);
      klee_assert(0);  // 路径终止，生成反例
    }
  }
}

int main() {
  int nelem; // 符号化的待排序元素个数

  klee_make_symbolic(&nelem, sizeof(nelem), "nelem");
  klee_assume(nelem == 3); // 固定个数，便于分析

  int input[nelem]; // 符号化的待排序数组

  klee_make_symbolic(&input, sizeof(input), "input");

  //（可选）范围约束，控制状态数量：
  for (int i = 0; i < nelem; ++i) {
    klee_assume(input[i] >= 0x0);
    klee_assume(input[i] <= 0xF);
  }

  test(input, nelem);
  return 0;
}