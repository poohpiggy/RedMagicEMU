# Guest 振动发送端（未接线）

`guest_sender.c` 只对调用方交来的已连接 `AF_UNIX`、`SOCK_SEQPACKET` 描述符发送普通振动或取消请求。它不创建连接、不注册 HAL、不调用宿主振动器，也不校验 Guest 应用权限。调用方必须在交接前独立认证宿主及会话，并保证仅向已获准的请求开放发送能力；这些接线尚未完成。

调用顺序为：成功 `init` 后，由发送端持有该描述符；调用 `on`（1–5000 毫秒）或 `off`，停止时调用 `close`，所有使用线程结束后调用 `destroy`。初始化失败时描述符仍由调用方持有；`destroy` 之前不能再次初始化同一对象，`destroy` 之后不能继续使用。发送端使用互斥锁，不能在 signal handler 中调用。初始化会设置 `FD_CLOEXEC`；如果创建连接时可能并发执行 `exec`，调用方须以 `SOCK_CLOEXEC` 原子创建，并在希望关闭能触发对端 EOF 时关闭自己保留的所有描述符别名。

消息序号从 1 开始，使用宿主已有格式。发送成功只表示消息已排入 socket，不保证对端收到、授权或实际振动；发送失败或序号耗尽会退役并关闭该会话，不自动重试。失败或关闭后，对端在读到 EOF 前仍可能处理已经入队的消息，因此不能据此断言硬件未动作或队列已清空。关闭本端本身不保证手机立即停止；正式接线还须确保宿主在断连和停止时取消振动，并处理取消失败。

电脑 Linux 本机回归（仅自造进程与假宿主，无手机或真实振动）：

```sh
gcc -std=gnu11 -Wall -Wextra -Werror -pthread \
  native/gueststubs/vibrator/guest_sender.c \
  native/gueststubs/vibrator/tests/guest_sender_test.c \
  native/hostjni/vibration_host_gate.c \
  native/gueststubs/radio-bridge/gate.c \
  native/gueststubs/radio-bridge/receive.c \
  -o /tmp/rme_guest_vibration_sender_test
/tmp/rme_guest_vibration_sender_test
```

测试内一字节 ACK 只是为了同步假宿主，不属于产品协议。缺少 pidfd 时返回 77，表示跳过；Android 交叉编译不等于 Guest 或手机运行通过。
