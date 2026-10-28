// ============================================================================
// dtp_ca.h — 轻量 Cycle-Accurate（CA）建模框架，复现 uniBench 的端口更新机制
//
// 背景：基于 uniBench 平台将 DTP 芯片 RTL 转换为 SystemC CA 模型进行仿真验证。
// CA 模型由若干 Module 组成，Module 之间通过 Signal（信号）连接。每个周期：
//   1) tick()   —— 时序逻辑：寄存器更新；
//   2) 端口更新 —— 把发生变化信号的"新值"通知给读它的 Module，触发其重新求值。
//
// 本框架实现两套"端口更新机制"，对应实习期间的两版实现：
//
//   PollEngine（基线）     —— 每周期 O(n) 轮询：遍历全部端口，逐个检查信号是否
//                            变化，变化才调用对应 Module 的更新函数。
//                            开销与端口总数成正比，即使绝大多数端口无变化。
//
//   DispatchEngine（优化） —— unordered_map<Signal*, vector<PortBinding>> 将
//                            "信号地址"映射到"成员函数指针"。信号一变化就按地址
//                            O(1) 哈希查找并直接派发，只处理真正变化的信号。
//
// 两套引擎共享完全相同的 Module 计算逻辑与调度顺序，唯一变量是端口更新机制，
// 从而保证计时/剖析结果反映的是"端口更新方式"的差异。
// ============================================================================

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <algorithm>

// ---------------------------------------------------------------------------
// Signal：CA 模型中的信号线
// ---------------------------------------------------------------------------
struct Signal {
    uint32_t val = 0;      // 当前值
    uint32_t prev = 0;     // 上一拍值（用于"是否变化"检测）
    uint32_t id = 0;       // 信号编号（VCD 输出用）

    // 写信号：若值变化则标记 dirty
    void set(uint32_t v) {
        prev = val;
        val = v;
    }
    bool changed() const { return val != prev; }
    // 周期末同步 prev=val（清 dirty）
    void commit() { prev = val; }
};

// ---------------------------------------------------------------------------
// Module：CA 模型中的功能单元
// ---------------------------------------------------------------------------
struct Module;

// 成员函数指针：端口更新时调用的模块成员函数
using UpdateFn = void (Module::*)();

struct Module {
    std::vector<Signal*> ins;    // 输入端口绑定的信号
    std::vector<Signal*> outs;   // 输出端口驱动的信号
    std::vector<uint32_t> regs;  // 内部寄存器（时序状态）

    virtual ~Module() = default;
    // 组合逻辑：读取 ins，写 outs
    virtual void eval() = 0;
    // 时序逻辑：更新 regs（仅在本模块被"时钟使能"时由引擎调用）
    virtual void tick() = 0;
};

// ---------------------------------------------------------------------------
// 端口绑定：把"信号"和"读它的模块 + 更新成员函数"关联起来
// ---------------------------------------------------------------------------
struct PortBinding {
    Module* m;
    UpdateFn fn;
};

// ---------------------------------------------------------------------------
// PollEngine —— 基线：O(n) 轮询端口更新
// ---------------------------------------------------------------------------
struct PollEngine {
    std::vector<Module*> modules;                       // 全部模块
    std::vector<std::pair<Signal*, PortBinding>> ports; // 全部端口（扁平）
    std::vector<uint32_t> snapshot;                     // 每端口上次看到的值

    // 注册端口：信号 s 变化时要调用 m->fn
    void add_port(Signal* s, Module* m, UpdateFn fn) {
        ports.push_back({s, {m, fn}});
        snapshot.push_back(s->val);
    }

    // 端口更新（被优化的热路径）：O(总端口数) 轮询
    // noinline 使其在 gprof 剖析中作为独立函数出现（复现"Gprof 定位端口更新函数"）
    __attribute__((noinline)) void update_ports() {
        for (size_t i = 0; i < ports.size(); ++i) {
            Signal* s = ports[i].first;
            if (s->val != snapshot[i]) {          // 逐个端口检查是否变化
                snapshot[i] = s->val;
                PortBinding& b = ports[i].second;
                (b.m->*b.fn)();                    // 调用成员函数指针
            }
        }
    }
};

// ---------------------------------------------------------------------------
// DispatchEngine —— 优化：unordered_map 信号地址 -> 成员函数指针，O(1) 派发
// ---------------------------------------------------------------------------
struct DispatchEngine {
    std::unordered_map<Signal*, std::vector<PortBinding>> map;

    // 注册端口：信号 s 变化时要调用 m->fn
    void add_port(Signal* s, Module* m, UpdateFn fn) {
        map[s].push_back({m, fn});
    }

    // 信号变化时调用（地址驱动更新）：O(1) 哈希查找 + 直接派发
    // noinline 使其在 gprof 剖析中作为独立函数出现
    __attribute__((noinline)) void signal_changed(Signal* s) {
        auto it = map.find(s);                     // O(1) 哈希查找（按信号地址）
        if (it == map.end()) return;
        for (PortBinding& b : it->second)
            (b.m->*b.fn)();                        // 调用成员函数指针
    }

    // 端口更新：只对"本周期真正变化"的信号做派发
    void update_ports(const std::vector<Signal*>& changed) {
        for (Signal* s : changed)
            signal_changed(s);
    }
};

// ---------------------------------------------------------------------------
// 信号记录（VCD 波形输出用）
// ---------------------------------------------------------------------------
struct VcdWriter {
    FILE* f = nullptr;
    uint64_t t = 0;
    std::vector<uint32_t> observed;
    void open(const char* path, const std::vector<Signal*>& sigs) {
        f = std::fopen(path, "w");
        if (!f) { std::perror(path); std::exit(1); }
        observed.clear();
        for (Signal* s : sigs) observed.push_back(s->val);
        std::fprintf(f, "$timescale 1ns $end\n$scope module dtp $end\n");
        char code[64];
        for (size_t i = 0; i < sigs.size(); ++i) {
            size_t k = 0; int x = (int)i + 1;
            while (x) { code[k++] = char('!' + (x % 94)); x /= 94; }
            code[k++] = '!';
            code[k] = 0;
            sigs[i]->id = (uint32_t)i + 1;
            std::fprintf(f, "$var wire 32 %s sig%zu $end\n", code, i);
        }
        std::fprintf(f, "$enddefinitions $end\n$dumpvars\n");
        for (size_t i = 0; i < sigs.size(); ++i) {
            char code[64]; size_t k = 0; int x = (int)i + 1;
            while (x) { code[k++] = char('!' + (x % 94)); x /= 94; }
            code[k++] = '!'; code[k] = 0;
            std::fprintf(f, "b%s %s\n", bin32(sigs[i]->val), code);
        }
        std::fprintf(f, "$end\n");
    }
    void sample(const std::vector<Signal*>& sigs) {
        if (!f) return;
        for (size_t index = 0; index < sigs.size(); ++index) {
            Signal* s = sigs[index];
            if (s->val != observed[index]) {
                observed[index] = s->val;
                char code[64]; size_t k = 0; int x = (int)s->id;
                while (x) { code[k++] = char('!' + (x % 94)); x /= 94; }
                code[k++] = '!'; code[k] = 0;
                std::fprintf(f, "#%llu\nb%s %s\n",
                             (unsigned long long)t, bin32(s->val), code);
            }
        }
    }
    void tick() { ++t; }
    void close() { if (f) std::fclose(f); }

    static const char* bin32(uint32_t v) {
        static char buf[33];
        for (int i = 31; i >= 0; --i) buf[31 - i] = ((v >> i) & 1) ? '1' : '0';
        buf[32] = 0;
        return buf;
    }
};
