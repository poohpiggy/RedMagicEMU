# 振动进程文件隔离实验（不接入产品）

此目录只验证一个候选：在自造 Linux 子进程中，使用 Landlock 限制当前线程及其后代的文件访问，只允许通过已确认的 Guest 根目录访问。它不是振动桥，不运行原厂 HAL，也不适合直接用于实际 Guest。函数返回负值时没有建立隔离；调用方必须拒绝继续启动。最低要求 Landlock ABI 5，当前测试通过传入不可能达到的 ABI 验证版本不足时返回失败，并非模拟了所有内核不支持的情况。

在已支持 Landlock ABI 5 的非 root Linux 环境运行：

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  tools/diagnostics/vibrator-landlock/guard.c \
  tools/diagnostics/vibrator-landlock/guard_test.c \
  -o /tmp/rme-vib-landlock-test
/tmp/rme-vib-landlock-test
```

测试使用自造临时目录，验证 Guest 目录内的文件可读写，目录外的绝对路径、已打开目录描述符加相对路径、符号链接及可用时的 `openat2` 不可用于打开文件。它也验证限制前打开的目录外文件描述符仍可写入：实际启动前必须关闭不该继承的描述符；已存在的其他线程也不能由此保证受限。实验完全信任所允许的目录，未验证其中的硬链接、挂载或设备文件是否指向不应访问的对象。Landlock 不覆盖所有文件元数据操作，设备 `ioctl`、原厂 HAL 所需的 `/dev` 和 `/proc` 白名单、Android 内核支持与 SELinux 交互均未验证。不能据此启动原厂振动服务或声称宿主硬件访问已完全隔离。

测试还在关闭继承的监听描述符后，让受限子进程分别新建并连接 Guest 根目录外的文件系统 UNIX socket 和 abstract UNIX socket，使用 `SOCK_SEQPACKET` 做双向通信；前者模拟 Binder 通道的位置，后者模拟振动桥通道。仅验证测试规则和当前 Linux 内核的行为，不证明真实 Binder 协议、Android socket 或设备兼容。Landlock、`close_range` 或所需 UNIX socket 类型不可用时测试输出跳过信息并返回 77，不得当作通过。

实验另在单线程子进程中使用 `rme_vib_close_unneeded_fds`：调用内核 `close_range`，只保留显式给出的 Guest 根目录描述符和原有的标准输入、输出及错误描述符。测试覆盖保留描述符之前及之后的外部描述符均被关闭；如果内核不支持或关闭失败，调用方必须立即终止，不能继续启动。该函数不验证保留的标准描述符是否可信，也不适用于已经存在其他线程或必须继承其他描述符的进程。测试不证明 Android 支持，也没有接入产品。

另有一个独立的 `nodev` 与嵌套挂载实验，需要 Linux、Python 3、C 编译器、`unshare`、`mount`、`umount`，以及允许普通用户创建 user/mount namespace 的内核：

```sh
python3 tools/diagnostics/vibrator-landlock/nodev_namespace_test.py
```

脚本先创建独立命名空间，并将其中的挂载传播设为 private；只在自建临时目录内绑定本机 `/dev/zero`。测试分别验证 `nodev` 拒绝打开字符设备、普通文件可读写、Guest 根目录下的嵌套设备挂载以及模拟 sysfs 的嵌套目录挂载均被拒绝。它还在自造子进程中验证示例过滤器拒绝 `mount` 和 `mknodat`。测试成功输出 `NODEV_NAMESPACE_TEST_OK`；命名空间不可用时返回 77，不能当作通过。

其中 sysfs 是普通目录模型，并非真实宿主 sysfs；当前 WSL 环境无法在该用户命名空间绑定真实 `/sys`。示例挂载检查和系统调用过滤仅用于实验，不是完整的隔离实现；没有验证真实 HAL、Android 普通应用能否创建命名空间、内核/SELinux 支持或并发修改，不能据此启用振动服务。
