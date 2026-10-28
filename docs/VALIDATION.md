# 发布验证记录

日期：2026-10-28。环境：WSL2 Ubuntu 22.04，Linux 6.6.87.2-microsoft-standard-WSL2，Intel Core i7-14700K，g++ 11.4.0，Python 3.10.12，C++17 `-O2 -Wall -Wextra`。在 Linux `/tmp` 新目录复制源码后构建，没有使用旧二进制。

## 已执行

1. `make -j2`：通过。
2. `make test`：五组多通道配置的全部最终寄存器/out/status 比较及 VCD 比较通过；无效尺寸拒绝通过。
3. `make test-models SYSTEMC=/home/xylon/work/_tools/systemc-install`：通过，20 周期 RTL/SystemC/数学参考相同，最终 accum=240。本机使用原有本地 SystemC 安装；新电脑可安装 `libsystemc-dev`。
4. `make bench`：通过；20,000 周期，N=2048，K=8，R=1。轮询 308.099 ms，派发 111.188 ms，2.77×；checksum 均为 `668230c087396633`，且另有直接逐项状态比较。
5. `make profile`：通过。独立 `-pg` 构建，60,000 周期，N=2048，K=8，R=12；轮询墙钟 1435.409 ms，派发 1001.520 ms。与默认 bench 配置不同。

gprof flat profile：轮询 `update_ports` self-time 63.89%；优化 `signal_changed` 2.86%；tick 占比由 33.33% 变为 94.29%。百分比是不同进程各自的采样分母，不是 CPU 利用率，也不能直接相减解释整个运行时间。

原始构建输出见 [2026-10-28.log](validation/2026-10-28.log)，gprof 原始报告见 [基线](validation/gprof_poll.txt) 与 [派发](validation/gprof_dispatch.txt)。

## 发布时修正

- 移除硬编码的本机 SystemC 路径，支持系统 pkg-config 和 SYSTEMC 安装前缀。
- 派发只处理真实变化，并在输入写完后批量通知，统一两版求值时点。
- 修复 VCD 在记录前清空变化的逻辑，按独立采样快照输出周期末信号。
- 校验全部寄存器，补充直接状态比较，不只比较 hash。
- 补齐 SystemC 内部状态敏感性与复位，修正 RTL 激励最后一次输入的时钟边界，并加入逐周期参考校验。
- 增加参数检查、自动验证、CI 配置及可选 Dockerfile。

## 验证边界

本次数据来自单台机器的一批示例测量，不是统计学性能结论。Docker 构建未在本机执行。GitHub Actions 的实际运行状态以仓库 Actions 为准。自动化验证覆盖当前合成模型，不证明任意芯片或通用仿真内核正确。
