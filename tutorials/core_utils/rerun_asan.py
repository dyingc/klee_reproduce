#!/usr/bin/env python3
import os, sys

# 你的ASan echo 路径（请按你的环境改；常见有 /home/klee/coreutils-6.11/obj-llvm/src/echo_asan）
PROD = "echo"
ECHO_BIN = b"/home/klee/coreutils-6.11/obj-llvm/src/" + PROD.encode() + b"_asan"

# 构造和 ktest 一样的 argv 与 stdin
argv = [ECHO_BIN, b"\x01\xff"]          # argv[0] 是程序名，argv[1] 是那两个字节
stdin_payload = b"\x00"                 # 单字节 NUL

# 建立管道：父进程写，子进程读
r, w = os.pipe()

pid = os.fork()
if pid == 0:
    # 子进程：把管道读端接到 fd 0 (stdin)
    os.dup2(r, 0)
    os.close(r); os.close(w)
    # 用空环境也行，这里传当前环境（注意 bytes/str 都可，保持简单用空）
    env = {}
    # 直接 exec 原生 echo（argv 用 bytes，能保留非 ASCII 字节）
    os.execve(ECHO_BIN, argv, env)
else:
    # 父进程：写入 NUL 到子进程 stdin
    os.close(r)
    os.write(w, stdin_payload)
    os.close(w)
    # 等待子进程结束
    _, status = os.waitpid(pid, 0)
    code = os.waitstatus_to_exitcode(status)
    print(f"\n[Parent] child exit code = {code}")