# 自造进程监督探针

这个离线程序只创建和跟踪自己启动的父子进程。它在自造父进程安装只针对测试调用的 seccomp 过滤器，跟踪 fork，并检查能否在子进程恢复前观察到同 UID 的 `setresuid` 请求。测试调用被过滤器截获，不改变真实 UID；程序不跟踪 Guest、宿主其他应用或设备。进程被限制在测试本身，带超时并使用 `PTRACE_O_EXITKILL`。

```sh
gcc -std=gnu11 -Wall -Wextra -Werror tools/diagnostics/zygote-trace/supervision_probe.c -o /tmp/rme_supervision_probe
/tmp/rme_supervision_probe
```

退出 77 或 `SUPERVISION_*_UNAVAILABLE` 代表内核或安全策略不支持，不是通过。`SUPERVISION_PROBE_OK` 只说明本机 Linux 能观察这一个自造例子：不能证明 Android 宿主允许 ptrace、目标 Guest 的 zygote fork/USAP 路径受监管、任何请求来自可信的分配者、UID 被正确分配、权限数据受隔离或振动桥安全。此探针不属于产品，也不授权或接线振动。
