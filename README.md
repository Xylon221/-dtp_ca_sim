# DTP CA 模型：端口更新机制优化复现

用 C++17 对比两种端口更新机制：全量轮询与按变化信号地址派发。项目包含可执行基准、状态/波形校验，以及独立的单通道 RTL/SystemC 对照。

这是原实习工作核心方法的最小复现，不包含公司内部 uniBench、真实芯片 RTL、MPI 和前后门接口。多通道基准使用自建轻量 CA 框架；SystemC 对照使用 Accellera 库。源码没有自动把 RTL 转换成 SystemC 的工具链。

## 在另一台电脑复现

支持 Linux，推荐 Ubuntu 22.04/24.04；Windows 请在 WSL2 的 Ubuntu 终端执行。将仓库放在 Linux 的 `~/projects` 中，减少 `/mnt/c` 的 I/O 与时间戳干扰。

```bash
sudo apt-get update
sudo apt-get install -y git build-essential python3 binutils
mkdir -p ~/projects
cd ~/projects
git clone https://github.com/Xylon221/-dtp_ca_sim.git dtp_ca_sim
cd dtp_ca_sim
make -j2
make test
make bench
```

`make test` 应输出五组状态与 VCD 校验 PASS；`make bench` 输出耗时、状态校验和加速比。不同机器上的加速比会变化，没有固定通过阈值。

可选的 RTL/SystemC 验证：

```bash
sudo apt-get install -y iverilog libsystemc-dev pkg-config
make test-models
```

预期：`PASS: RTL == SystemC == reference, 20 cycles, final accum=240`。如使用自行编译的 SystemC：`make test-models SYSTEMC=/your/systemc-install`。`SYSTEMC` 应指向含 `include/` 和 `lib/` 或 `lib-linux64/` 的安装前缀。

## 常用命令

| 命令 | 用途 |
| --- | --- |
| `make test` | 验证零活动、稀疏写入、全通道写入、多寄存器、参数边界和 VCD |
| `make bench` | 20,000 周期、2,048 通道、每周期写 8 通道、每通道 1 寄存器 |
| `./dtp_ca_sim 20000 2048 8 12` | 自定义周期数、通道数、每周期写入通道数、寄存器数 |
| `make profile` | 独立 `-pg` 构建，生成两份 gprof 报告 |
| `make wave` | 生成 `wave_poll.vcd`、`wave_dispatch.vcd` |
| `make rtl` | 运行示例 RTL，输出波形和周期跟踪 |
| `make sc && ./dtp_systemc 20` | 运行单通道 SystemC 对照 |
| `make clean` | 清理生成产物 |

性能测试不要同时开启 VCD。分析大规模端口更新时，改变 N/K/R 观察收益如何随写入密度和时序工作量变化。

## Docker 备选

```bash
docker build -t dtp-ca-sim .
docker run --rm dtp-ca-sim
```

镜像构建时会执行 `make test` 和 `make test-models`。本次发布在 WSL 原生工具链完成验证，未在本机执行 Docker；Dockerfile 是可选入口。APT 包未锁定补丁版本，提供功能复现而非逐字节构建一致性。

## 源码导航

```text
ca/dtp_ca.h             Signal / Module / PortBinding、两套引擎、VCD
ca/dtp_ca_main.cpp      N 通道模型、写入激励、计时、完整最终状态比较
ca/dtp_systemc.cpp      独立单通道 SystemC 模型
rtl/dtp_core.v          独立单通道 RTL
rtl/dtp_tb.v            RTL 激励与逐周期 TRACE
scripts/verify.py       多通道引擎与 VCD 验证
scripts/verify_models.py  单通道 RTL/SystemC/数学参考比较
docs/TECHNICAL_REVIEW.md 面试复盘、复杂度、证据边界与追问
docs/VALIDATION.md      本次构建和实验记录
```

## 本次结果与项目关系

2026-10-28，i7-14700K / WSL2 / g++ 11.4 / `-O2`，一次默认基准：轮询 308.099 ms，派发 111.188 ms，约 **2.77×**。该数字是示例测量，不是跨机器保证。详细配置及独立 gprof 结果见 [验证记录](docs/VALIDATION.md)。

另一个项目 [BLIF 网表解析优化](https://github.com/Xylon221/-emu_compiler_blif) 研究编译阶段的字符串分配；本仓库研究模型运行阶段的端口更新。两者独立构建，目前没有自动串联流水线。

准备面试先读 [技术复盘](docs/TECHNICAL_REVIEW.md)，重点理解调度时序、平均复杂度、功能验证以及原工作数据与复现数据的区别。
