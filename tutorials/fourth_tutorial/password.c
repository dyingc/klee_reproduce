#include <stdio.h>

/* 这个程序从命令行参数读取一个字符串 argv[1]，
 * 然后检查它是不是 “hello”。如果是，就打印
 * “Password found!”。否则结束。
 */

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
    // printf("Password found!\n");
    return 0;
  }

  return 1;
}