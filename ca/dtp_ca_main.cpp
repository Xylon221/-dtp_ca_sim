// ============================================================================
// dtp_ca_main.cpp — DTP 芯片 CA 模型 + 端口更新机制基准
//
// 复现实习工作：基于 uniBench 将 DTP 芯片 RTL 转为 SystemC CA 模型，
// 针对端口更新机制 O(n) 轮询的性能瓶颈，设计 unordered_map 信号地址 ->
// 成员函数指针的 O(1) 地址驱动更新方案，对比两版仿真速度与 CPU 占比。
//
// DTP（Data Transfer Processor，数据搬移处理器）建模为：控制器 + N 个
// 传输通道。每周期控制器只驱动 K 个通道（K << N，稀疏活动），其余通道空闲。
// 基线引擎仍每周期轮询全部 N×P 个端口；优化引擎只对真正变化的信号做派发。
//
// 用法： ./dtp_ca_sim [cycles] [N] [K] [R] [--vcd] [--poll|--dispatch]
//   N：通道数；K：每周期活动通道；R：每通道寄存器数；P=6 为每通道输入端口。
// ============================================================================

#include "dtp_ca.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// 传输通道（Module 子类）：DTP 芯片的功能单元
//   en/data/mode/addr/len/ctrl 为 6 个输入端口；out/status 为输出；
//   regs[0] 为累加器，其余寄存器为普通时序状态。
// ---------------------------------------------------------------------------
struct Channel : Module {
    Signal *en, *data, *mode, *addr, *len, *ctrl, *out, *status;

    // 成员函数：输入端口变化时触发（成员函数指针的派发目标）
    void on_input_change() { eval(); }

    // noinline：使组合/时序函数在 gprof 剖析中独立可见
    __attribute__((noinline)) void eval() override {
        uint32_t a = regs[0];
        uint32_t v = (en->val)
            ? (a + data->val + mode->val + addr->val + len->val + ctrl->val)
            : a;
        out->set(v);
        status->set(v & 0xFFu);
    }

    __attribute__((noinline)) void tick() override {
        // 时钟：使能时更新寄存器组（regs[0] 累加器 + 其余时序状态）
        if (en->val) {
            regs[0] = out->val;
            for (size_t j = 1; j < regs.size(); ++j)
                regs[j] = regs[j] + out->val + (uint32_t)j;
        }
    }
};

// ---------------------------------------------------------------------------
// DTP 芯片：N 个通道 + 全部信号
// ---------------------------------------------------------------------------
struct Chip {
    int N;
    std::vector<Channel> ch;
    std::vector<Signal> en, data, mode, addr, len, ctrl, out, status;

    explicit Chip(int n, int R = 1)
        : N(n), ch(n), en(n), data(n), mode(n), addr(n), len(n), ctrl(n), out(n), status(n) {
        for (int i = 0; i < n; ++i) {
            ch[i].en = &en[i];  ch[i].data = &data[i];  ch[i].mode = &mode[i];
            ch[i].addr = &addr[i]; ch[i].len = &len[i]; ch[i].ctrl = &ctrl[i];
            ch[i].out = &out[i]; ch[i].status = &status[i];
            ch[i].ins = { &en[i], &data[i], &mode[i], &addr[i], &len[i], &ctrl[i] };
            ch[i].outs = { &out[i], &status[i] };
            ch[i].regs.assign(R, uint32_t(i * 0x9E3779B9u));
        }
    }

    std::vector<Signal*> all_signals() {
        std::vector<Signal*> s;
        for (int i = 0; i < N; ++i) {
            s.push_back(&en[i]); s.push_back(&data[i]); s.push_back(&mode[i]);
            s.push_back(&addr[i]); s.push_back(&len[i]); s.push_back(&ctrl[i]);
            s.push_back(&out[i]); s.push_back(&status[i]);
        }
        return s;
    }
};

// 通道的端口更新成员函数指针（派发目标）
static void (Channel::*CH_FN)() = &Channel::on_input_change;

// ---------------------------------------------------------------------------
// 控制器：每周期向 K 个通道注入新数据包（序号单调递增，保证 data 每次都变化，
// 从而轮询版与派发版都能正确检测到活动，两版语义完全一致）
// ---------------------------------------------------------------------------
static inline void controller_write(Chip& c, int base, int K, uint32_t& pkt) {
    for (int k = 0; k < K; ++k) {
        int i = (base + k) % c.N;
        c.en[i].set(1u);
        c.data[i].set(pkt);            // 数据包序号（单调递增，必然变化）
        c.mode[i].set(pkt & 3u);
        c.addr[i].set(pkt >> 2);
        c.len[i].set(pkt >> 4);
        c.ctrl[i].set(pkt & 7u);
        ++pkt;
    }
}

// ---------------------------------------------------------------------------
// 基线：O(n) 轮询端口更新
// ---------------------------------------------------------------------------
static void run_poll(Chip& c, int cycles, int K, const char* vcd_path) {
    PollEngine eng;
    eng.modules.reserve(c.N);
    for (int i = 0; i < c.N; ++i) {
        eng.modules.push_back(&c.ch[i]);
        eng.add_port(&c.en[i],   &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.data[i], &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.mode[i], &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.addr[i], &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.len[i],  &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.ctrl[i], &c.ch[i], static_cast<UpdateFn>(CH_FN));
    }

    VcdWriter vcd;
    std::vector<Signal*> all = c.all_signals();
    if (vcd_path) vcd.open(vcd_path, all);

    int base = 0;
    uint32_t pkt = 1;
    for (int cyc = 0; cyc < cycles; ++cyc) {
        controller_write(c, base, K, pkt);      // 控制器写信号
        eng.update_ports();                     // O(n) 轮询：检查全部端口
        for (Module* m : eng.modules) m->tick(); // 时序
        if (vcd_path) { vcd.tick(); vcd.sample(all); }
        base = (base + K) % c.N;
    }
    if (vcd_path) vcd.close();
}

// ---------------------------------------------------------------------------
// 优化：unordered_map 信号地址 -> 成员函数指针，O(1) 地址驱动更新
// ---------------------------------------------------------------------------
static void run_dispatch(Chip& c, int cycles, int K, const char* vcd_path) {
    DispatchEngine eng;
    for (int i = 0; i < c.N; ++i) {
        eng.add_port(&c.en[i],   &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.data[i], &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.mode[i], &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.addr[i], &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.len[i],  &c.ch[i], static_cast<UpdateFn>(CH_FN));
        eng.add_port(&c.ctrl[i], &c.ch[i], static_cast<UpdateFn>(CH_FN));
    }

    VcdWriter vcd;
    std::vector<Signal*> all = c.all_signals();
    if (vcd_path) vcd.open(vcd_path, all);

    int base = 0;
    uint32_t pkt = 1;
    std::vector<Signal*> changed;
    changed.reserve(static_cast<size_t>(K) * 6);
    auto write = [&](Signal& sig, uint32_t value) {
        if (sig.val != value) { sig.set(value); changed.push_back(&sig); }
    };
    for (int cyc = 0; cyc < cycles; ++cyc) {
        changed.clear();
        // Collect actual changes, then evaluate after all inputs are written.
        // Allocate the queue once outside the cycle loop (see below).
        for (int k = 0; k < K; ++k) {
            int i = (base + k) % c.N;
            write(c.en[i], 1u);
            write(c.data[i], pkt);
            write(c.mode[i], pkt & 3u);
            write(c.addr[i], pkt >> 2);
            write(c.len[i], pkt >> 4);
            write(c.ctrl[i], pkt & 7u);
            ++pkt;
        }
        eng.update_ports(changed);
        for (int i = 0; i < c.N; ++i) c.ch[i].tick();   // 时序
        if (vcd_path) { vcd.tick(); vcd.sample(all); }
        base = (base + K) % c.N;
    }
    if (vcd_path) vcd.close();
}

// ---------------------------------------------------------------------------
// 一致性校验：两版引擎跑完后的最终状态必须逐位一致
// ---------------------------------------------------------------------------
static uint64_t checksum(const Chip& c) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < c.N; ++i) {
        for (uint32_t reg : c.ch[i].regs) { h ^= reg; h *= 1099511628211ULL; }
        h ^= c.out[i].val;   h *= 1099511628211ULL;
        h ^= c.status[i].val; h *= 1099511628211ULL;
    }
    return h;
}

int main(int argc, char** argv) {
    int cycles = (argc > 1) ? std::atoi(argv[1]) : 20000;
    int N      = (argc > 2) ? std::atoi(argv[2]) : 2048;
    int K      = (argc > 3) ? std::atoi(argv[3]) : 8;
    int R      = (argc > 4) ? std::atoi(argv[4]) : 1;   // 每通道寄存器数
    bool vcd   = false;
    bool only_poll = false, only_dispatch = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--vcd")) vcd = true;
        if (!std::strcmp(argv[i], "--poll")) only_poll = true;
        if (!std::strcmp(argv[i], "--dispatch")) only_dispatch = true;
    }

    if (cycles <= 0 || N <= 0 || K < 0 || K > N || R <= 0 ||
        (only_poll && only_dispatch)) {
        std::fprintf(stderr, "require cycles>0, N>0, 0<=K<=N, R>0; choose at most one engine\n");
        return 2;
    }
    const int P = 6;  // 每通道输入端口数
    std::printf("== DTP 芯片 CA 模型：端口更新机制对比 ==\n");
    std::printf("通道数 N=%d, 每通道输入端口 P=%d, 寄存器 R=%d, 每周期活动通道 K=%d, 周期数=%d\n",
                N, P, R, K, cycles);
    std::printf("端口总数=%d（基线每周期轮询全部端口；优化每周期至多派发 %d 个变化信号）\n\n",
                N * P, K * P);

    double ms_poll = 0, ms_dispatch = 0;
    uint64_t sum_poll = 0, sum_dispatch = 0;
    std::unique_ptr<Chip> baseline;

    if (!only_dispatch) {
        baseline = std::make_unique<Chip>(N, R);
        Chip& c1 = *baseline;
        auto t0 = Clock::now();
        run_poll(c1, cycles, K, vcd ? "wave_poll.vcd" : nullptr);
        auto t1 = Clock::now();
        ms_poll = std::chrono::duration<double, std::milli>(t1 - t0).count();
        sum_poll = checksum(c1);
        std::printf("基线 O(n) 轮询：%10.3f ms\n", ms_poll);
    }

    if (!only_poll) {
        Chip c2(N, R);
        auto t2 = Clock::now();
        run_dispatch(c2, cycles, K, vcd ? "wave_dispatch.vcd" : nullptr);
        auto t3 = Clock::now();
        ms_dispatch = std::chrono::duration<double, std::milli>(t3 - t2).count();
        sum_dispatch = checksum(c2);
        if (baseline) {
            for (int i = 0; i < N; ++i) {
                if (baseline->ch[i].regs != c2.ch[i].regs ||
                    baseline->out[i].val != c2.out[i].val ||
                    baseline->status[i].val != c2.status[i].val) {
                    std::fprintf(stderr, "FAIL: exact state mismatch at channel %d\n", i);
                    return 1;
                }
            }
        }
        std::printf("优化 O(1) 派发：%10.3f ms\n", ms_dispatch);
    }

    if (!only_poll && !only_dispatch) {
        bool eq = (sum_poll == sum_dispatch);
        std::printf("\n功能对齐校验: %s\n", eq ? "PASS（两版最终状态逐位一致）" : "FAIL");
        std::printf("  基线 checksum=%016llx\n", (unsigned long long)sum_poll);
        std::printf("  优化 checksum=%016llx\n", (unsigned long long)sum_dispatch);
        std::printf("  仿真提速           : %.2f x\n", ms_poll / ms_dispatch);
        if (vcd)
            std::printf("  已输出波形：wave_poll.vcd / wave_dispatch.vcd（GTKWave 可查看）\n");
        return eq ? 0 : 1;
    }
    return 0;
}
