# Guest UID 事件顺序模型（离线诊断）

该目录只检查一组假定已认证的事件如何排序和关联，不读取 Guest、不创建监听端口、不调用宿主振动，也不参与产品构建。事件中的 `source`、`child` 和 `uid` 都只是测试数据；它们本身不携带或验证内核身份、pidfd、权限或不可伪造的凭据。`uid_model_candidate` 只是诊断快照，**不是 UID 分配、授权决定或产品放行接口**。

模型假设两类流程：普通请求后创建子进程，以及先预创建、收到请求后再 specialize。两者都要求请求、子进程、UID 和假设的最终确认一致后才产生快照；错误事件、重复或乱序、子进程退出及 `uid_model_stop` 均使当前模型失效。它只描述单个子进程，不涉及并发进程，也不证明在应用代码运行前建立了同步屏障。

目前仓库没有已验证的系统请求、zygote fork、USAP specialize 和最终确认事件源；特别是模型中的 zygote 最终确认是需要另行实现并验证的假设，不能当作目标固件现有回调。不同事件来自不同进程，统一的单调序号也需要由未来受信监督者创建。创建新模型会重新初始化全部状态，跨模型的会话编号唯一性、事件认证、pidfd 生命周期和身份来源均须由外部完成；不得用这些来源标签代替真实认证。

本模型不验证 `VIBRATE`、AppOps、用户、共享 UID、包安装更新、权限变更或持久化状态；目标 ROM 是否采用相同事件和顺序尚未验证。上述条件解决并经过设备验收前，不得接入正式振动通道。

在仓库根目录的 Linux 上运行：

```sh
gcc -std=gnu11 -Wall -Wextra -Werror tools/diagnostics/uid-ledger/uid_event_model.c tools/diagnostics/uid-ledger/uid_event_model_test.c -o /tmp/rme_uid_event_model_test
/tmp/rme_uid_event_model_test
```
