#include <klee/klee.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* 我们把原文件编译时用 -Dmain=echo_entry 重命名，然后在这里声明它 */
extern int echo_entry(int argc, char **argv);

/* 小工具：把一个符号字节归一成布尔开关（0/1） */
static int sym_toggle(const char *name) {
    unsigned char b;
    klee_make_symbolic(&b, sizeof(b), name);
    return (b & 1); // 只看最低位，避免过度分叉
}

/* 小工具：生成一个“安全的”符号字符串（最后1字节保证为'\0'） */
static void make_sym_cstr(char *buf, size_t n, const char *name, int printable_only) {
    klee_make_symbolic(buf, n, name);
    buf[n-1] = '\0';                         // 强制NUL终止
    if (printable_only) {                    // 先约束到可打印集，待需要再放开
        for (size_t i = 0; i < n-1; ++i) {
            // 允许空格和常见标点；避免 '\n' 干扰早期路径（需要时再放开）
            klee_assume(buf[i] == 0 || (buf[i] >= 0x20 && buf[i] <= 0x7e && buf[i] != '\n'));
        }
    }
}

int main(void) {
    /* -------- 1) 环境 gate 开关（小而可控） -------- */
    /*int g_join   = sym_toggle("tog_ECHO_JOIN");   // 控制走“obscure join”路径
    int g_buf    = sym_toggle("tog_ECHO_BUF");    // 是否自定义stdout缓冲
    int g_opt    = sym_toggle("tog_ECHO_OPT");    // 是否触发 optimizer
    int g_clean  = sym_toggle("tog_ECHO_CLEAN");  // cleanup 风格
    int g_strict = sym_toggle("tog_ECHO_STRICT"); // 二次释放/严格清理
    int g_hint   = sym_toggle("tog_ECHO_HINT");   // 是否提供buffer hint
    int g_join   = 1;   // 控制走“obscure

    if (g_join)   setenv("ECHO_JOIN",   "1", 1);
    if (g_buf)    setenv("ECHO_BUF",    "1", 1);   // 非"0"生效
    if (g_opt)    setenv("ECHO_OPT",    "1", 1);
    if (g_clean)  setenv("ECHO_CLEAN",  "1", 1);
    if (g_strict) setenv("ECHO_STRICT", "1", 1);
    if (g_hint)   setenv("ECHO_HINT",   "128", 1); // 给个中等大小提示，可调 */ // 先全开，后续再调整

    /* -------- 2) 构造 argv：固定最多3个参数，含1个可能是选项 -------- */
    // enum { MAX_ARGS = 3, ARG_LEN = 32 };
    enum { MAX_ARGS = 3, ARG_LEN = 48 }; // 调试时先缩小规模
    char a0[] = "./echo_challenge3";              // 程序名具体化
    static char a1[ARG_LEN], a2[ARG_LEN], a3[ARG_LEN];

    // 第一个“参数位”用来探索 -n/-e/-E 以及普通字符串
    // 先给它一个“类别选择字节”，降低分支爆炸
    unsigned char kind;
    klee_make_symbolic(&kind, sizeof(kind), "arg0_kind");
    kind %= 5; // 0..4 五种形态
    kind %= 3; // 0..2 三种形态，调试时先缩小规模

    //switch (kind) {
    //    case 0: strcpy(a1, "-e"); break;
    //    case 1: strcpy(a1, "-E"); break;
    //    default:
    //        make_sym_cstr(a1, ARG_LEN, "arg1_str", /*printable_only=*/1);
    //        break;
    //}
    switch (kind) {
        case 0: strcpy(a1, "-n"); break;
        case 1: strcpy(a1, "-e"); break;
        case 2: strcpy(a1, "-E"); break;
        case 3: strcpy(a1, "--"); break;     // 终止选项，后面全是字符串
        default:
            make_sym_cstr(a1, ARG_LEN, "arg1_str", /*printable_only=*/0); // 放开，允许不可打印字符
            break;
    }

    // 另外两个参数纯符号字符串，先限定可打印；后续若需要可放开
    make_sym_cstr(a2, ARG_LEN, "arg2_str", 1);
    make_sym_cstr(a3, ARG_LEN, "arg3_str", 0); // 放开，允许不可打印字符

    // 决定实际 argv 个数（再用一个小开关）
    unsigned char ac;
    klee_make_symbolic(&ac, sizeof(ac), "argc_pick");
    int arg_count = 1 + (ac % MAX_ARGS);         // 1..3 个“用户参数”
    // 组装 argv
    char *argv_all[1 + MAX_ARGS + 1];
    int argc_all = 1; argv_all[0] = a0;
    if (arg_count >= 1) argv_all[argc_all++] = a1;
    if (arg_count >= 2) argv_all[argc_all++] = a2;
    if (arg_count >= 3) argv_all[argc_all++] = a3;
    argv_all[argc_all] = NULL;

    /* -------- 3) 调用被测程序入口 -------- */
    return echo_entry(argc_all, argv_all);
}
