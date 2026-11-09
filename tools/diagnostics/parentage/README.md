# 自造进程父子关系离线诊断

本目录只用于电脑离线研究，不接入应用或设备，也不提供 Guest UID 登记、权限校验或振动功能。诊断在同一用户下创建受控父子进程，由内核提供实际发信者凭据，并用 pidfd 和 `/proc` 观察指定子进程的当前父子关系；子进程退出后不再接受观察结果。其返回值是一次观察，不是可信授权凭据。

前提尚未证明：实际 zygote 是否能以可信方式报告子进程及其 Guest UID；Guest 中同宿主 UID 的进程能否影响 zygote、Binder 或权限数据；目标 Android 宿主能否按此方式访问相应 `/proc` 节点；进程重启、PID 复用与并发期间的完整生命周期。**这些未解决前不得接入产品，也不得据此宣称权限隔离。**

本仓库此前对一台设备记录了普通应用 UID 的 pidfd 启动探针及 Guest 内 SO_PASSCRED 的自造父子进程实验（见 `docs/PROGRESS.md`），这不证明本诊断的整个组合可在 Android 宿主运行。以下命令仅运行本机 Linux 上的独立测试：

```sh
gcc -std=gnu11 -Wall -Wextra -Werror tools/diagnostics/parentage/parentage.c tools/diagnostics/parentage/parentage_test.c -o /tmp/rme_parentage_test
/tmp/rme_parentage_test
```

目标进程均由测试自身创建，测试不读取 Guest 数据、不使用硬件节点。缺少或禁止 pidfd 时测试以 77 退出，不能记为通过。
