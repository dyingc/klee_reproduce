/*
 * 教程核心目标：
 * 1) 把长度为 4 的 int 数组整体设为“符号输入”（symbolic），让 KLEE 自动探索所有可能取值；
 * 2) 用两种排序（插入排序 vs. 冒泡排序）处理同一输入；
 * 3) 通过断言 assert(temp1[i] == temp2[i]) 交叉验证两种实现是否等价；
 * 4) 如果实现不等价（本文件的 bubble_sort 故意“只做一趟”→ 有缺陷），KLEE 会生成能触发断言失败的反例。
 *
 * 关于你遇到的报错（external call with symbolic argument: printf）：
 * - KLEE 默认不允许“把符号数据作为参数传给外部函数”（如 printf）。因此这里会报错。
 * - 解决方法（任选其一）：
 *   A) 使用 uClibc：klee --libc=uclibc sort.bc
 *   B) 放宽策略：klee --external-calls=all sort.bc （或 --external-calls=concrete）
 *   C) 教学/调试时把下面的 printf 注释掉，或将其改为 klee_print_expr；
 *      klee_print_expr 的声明在 <klee/klee.h>，例如：klee_print_expr("input0", input[0]);
 *
 * 进阶补充（可选）：
 * - 路径空间太大时，可以对输入加范围约束（klee_assume），例如 [-10, 10]，以减少状态爆炸。
 * - 运行时常用选项：--only-output-states-covering-new、-max-time、-max-forks 等，详见教程二/官方文档
 * https://klee-se.org/tutorials/testing-regex/
 */

#include "klee/klee.h"  // KLEE 的内建函数声明：klee_make_symbolic / klee_assume / klee_print_expr 等，需要有 KLEE 支持（如：运行在 KLEE 容器中）

#include <assert.h>     // assert()
#include <stdio.h>      // printf() —— 注意：这会触发“外部调用”策略
#include <stdlib.h>     // malloc()/free()
#include <string.h>     // memcpy()/memmove()

/*
 * insert_ordered：
 *  - 把 item 插入到“当前已有 i 个有序元素”的 temp 数组中，使之仍然有序。
 *  - 用 memmove 把 [i, nelem-1] 整段右移一个元素，然后在空出的位置 array[i] = item。
 *  - 注意：这里假设 array[0..nelem-1] 的“有效区间”是有序的（调用者保证）。
 */
static void insert_ordered(int *array, unsigned nelem, int item) {
  unsigned i = 0;

  for (; i != nelem; ++i) {
    if (item < array[i]) {
      // 将 [array[i], array[nelem-1]] 整段右移 1 位，为 item 腾位置
      memmove(&array[i+1], &array[i], sizeof(*array) * (nelem - i));
      break;
    }
  }

  array[i] = item;  // 如果 item >= 所有元素，i == nelem，会直接放在“有效区间”末尾
}

/*
 * bubble_sort（有意引入的“残缺实现”）：
 *  - 典型冒泡需要多趟扫描，直到某一趟中没有发生交换为止。
 *  - 这里的实现虽然设置了 done 标志，但立刻在第一次外层迭代后 break；
 *    → 实际只做了一趟扫描，因此很多输入不会被完全排序（存在缺陷）。
 *  - 教程的设计正是利用这个缺陷，让 KLEE 找到使两种排序输出不同的输入，从而触发断言。
 */
void bubble_sort(int *array, unsigned nelem) {
  for (;;) {
    int done = 1;

    for (unsigned i = 0; i + 1 < nelem; ++i) {
      if (array[i+1] < array[i]) {
        int t = array[i + 1];
        array[i + 1] = array[i];
        array[i] = t;
        done = 0;  // 本趟发生过交换
      }
    }

    break;  // ⚠️ 有意为之：直接退出循环 → 只做一趟扫描，无法保证全局有序
    // 正确实现应为：if (done) break;  // 没有交换才退出；否则继续下一趟
  }
}

/*
 * insertion_sort：
 *  - 经典“插入排序”思路：维护一个临时数组 temp，前 i 个元素永远有序；
 *  - 每次把 array[i] 按序插入到 temp 的前 i 段，使之仍然有序；
 *  - 最终把 temp 复制回原数组。
 */
void insertion_sort(int *array, unsigned nelem) {
  int *temp = malloc(sizeof(*temp) * nelem);  // 临时有序数组

  for (unsigned i = 0; i != nelem; ++i)
    insert_ordered(temp, i, array[i]);        // 将 array[i] 插入到 temp[0..i-1] 的有序区

  memcpy(array, temp, sizeof(*array) * nelem); // temp 拷贝回原数组
  free(temp);
}

/*
 * test：
 *  - 为了“交叉验证”，把同一份输入各拷贝一份（temp1/2），分别喂给插入排序/冒泡排序；
 *  - 打印输入与两个排序的输出（注意 printf 的“外部调用”问题，见文件顶注释）；
 *  - 最后逐元素断言结果一致：若冒泡排序不正确 → 触发 assert 失败（KLEE 将产生反例）。
 */
void test(int *array, unsigned nelem) {
  int *temp1 = malloc(sizeof(*array) * nelem);
  int *temp2 = malloc(sizeof(*array) * nelem);

  // ⚠️ 这里打印的是“符号数据” → 可能触发 KLEE 的外部调用报错
  // 解决：--libc=uclibc 或 --external-calls=all，或把这几行 printf 注释掉/改用 klee_print_expr
  printf("input: [%d, %d, %d, %d]\n",
         array[0], array[1], array[2], array[3]);

  memcpy(temp1, array, sizeof(*array) * nelem);
  memcpy(temp2, array, sizeof(*array) * nelem);

  insertion_sort(temp1, nelem);
  bubble_sort(temp2, nelem);

  printf("insertion_sort: [%d, %d, %d, %d]\n",
         temp1[0], temp1[1], temp1[2], temp1[3]);

  printf("bubble_sort   : [%d, %d, %d, %d]\n",
         temp2[0], temp2[1], temp2[2], temp2[3]);

  // 交叉验证的关键断言：若两种排序结果不一致，则触发失败
  for (unsigned i = 0; i != nelem; ++i)
    assert(temp1[i] == temp2[i]);

  free(temp1);
  free(temp2);
}

int main() {
  // 初始数组（内容会被 klee_make_symbolic 覆盖为符号值；此处常量仅作占位）
  int input[4] = { 4, 3, 2, 1 };

  // 把整个数组（16 字节）标记为“符号输入”
  // KLEE 将探索 input[0..3] 的各种取值组合
  klee_make_symbolic(&input, sizeof(input), "input");

  // （可选）为控制状态规模，可加范围约束（取消注释即可）：
  // for (int i = 0; i < 4; ++i) {
  //   klee_assume(input[i] >= -10);
  //   klee_assume(input[i] <=  10);
  // }

  test(input, 4);
  return 0;
}