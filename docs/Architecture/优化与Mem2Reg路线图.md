# 优化与 Mem2Reg 完善路线图

本文档面向课程项目实现，不追求一次做到工业级 LLVM，但要求做到：结构清楚、功能可验证、后续能继续扩展。

## 1. 最终目标

### 1.1 课程可交付目标

最终希望 `-O1` 打开后，编译器具备一条稳定的中端优化流水线：

```text
Verify
  -> SimplifyCFG
  -> Verify
  -> Mem2Reg
  -> Verify
  -> ConstantFold
  -> Verify
  -> TrivialPhi
  -> Verify
  -> DCE
  -> Verify
```

其中核心目标是完成标量局部变量的 mem2reg：

```text
alloca/load/store 形式的局部标量变量
  -> SSA value + phi
```

需要支持：

- 单基本块变量提升。
- `if/else` 分支后的变量合流。
- `while` 循环中的循环变量 phi。
- 优化后 LLVM 风格 IR 可以输出。
- 优化后 RISC-V64 ASM 可以生成并通过现有测试。
- 不错误提升数组、指针、全局变量、地址逃逸变量。

### 1.2 稳健版目标

在课程可交付目标基础上，继续增强：

- CFG 清理更完整，比如常量条件分支折叠、空跳转块合并。
- trivial phi 删除更彻底。
- phi lowering 更稳健，覆盖 critical edge、parallel copy、循环 phi。
- DCE 和 ConstantFold 可以反复协同，清掉更多冗余 IR。
- VerifyPass 覆盖更多 IR 不变量，作为每个 pass 的安全网。

### 1.3 暂不追求的工业级目标

这些可以作为后续拔高，但不建议作为当前第一目标：

- 完整 LLVM `undef/poison` 语义。
- 复杂 alias analysis。
- 指针变量和数组元素的 mem2reg。
- 全局值编号 GVN 的完整版本。
- 生产级寄存器分配器。
- 大规模 SSA destruction 和复杂 parallel copy 调度。

## 2. 当前状态

当前项目已经具备：

- `IRPass` 和 `IRPassManager`。
- `IRCFG`，可以构建基本块前驱/后继。
- 支配关系和支配边界计算。
- `Mem2RegPass`，支持局部标量 alloca 提升。
- `PhiInst`、`IRBuilder::createPhi()` 和 def-use 机制。
- `VerifyPass`，检查 CFG、phi incoming、terminator、use-def。
- `ConstantFoldPass`，折叠简单二元常量表达式、常量比较和常量 `zext`。
- `TrivialPhiPass`，删除所有 incoming 都相同的 phi。
- `DCEPass`，删除无副作用且无人使用的死指令。
- `SimplifyCFGPass`，当前已支持常量条件分支折叠、不可达块删除、保守空跳转块旁路。
- 基础 phi lowering，优化后 ASM 测试目前可以通过。

当前 `-O1` 全量测试状态：

```text
IR: 250/250
ASM: 22/22
ERR: 10/10
```

## 3. 核心概念要求

实现和报告时，至少要能解释这些概念。

### 3.1 Pass

pass 是编译器流水线中的一个处理步骤。

```text
Analysis Pass
  只分析，不改 IR，例如 CFG、Dominator。

Transform Pass
  会改 IR，例如 Mem2Reg、DCE、ConstantFold。

Backend Pass
  偏后端，例如 phi lowering、寄存器分配。
```

### 3.2 CFG

CFG 是控制流图。节点是基本块，边是跳转关系。

mem2reg 需要 CFG 来知道：

- 一个基本块有哪些前驱。
- 一个基本块有哪些后继。
- 变量定义会沿着哪些路径流动。
- 哪些位置需要插入 phi。

### 3.3 Dominator 和 Dominance Frontier

如果从入口块到 `B` 的所有路径都必须经过 `A`，则 `A` 支配 `B`。

dominance frontier 可以粗略理解为：

```text
一个定义从某些路径流过来，但控制流开始合并的边界位置。
```

mem2reg 用 dominance frontier 决定 phi 插在哪里。

### 3.4 SSA 和 Phi

SSA 要求每个变量只被定义一次。

分支合流时，一个 SSA 名字可能来自不同前驱，所以需要 phi：

```llvm
%x = phi i32 [ 1, %then ], [ 2, %else ]
```

它的意思是：

```text
如果从 then 来，x = 1
如果从 else 来，x = 2
```

### 3.5 RAUW

RAUW 是 `replace all uses with`。

mem2reg、constant fold、trivial phi 都会用到它：

```text
把旧 Value 的所有使用点替换成新 Value。
```

对于 `PhiInst`，不仅要替换 operands，还要同步替换 incoming 列表。

## 4. 分阶段实现要求

### 阶段 0：确认基线

目标：

- 确认不开优化时现有功能不回归。
- 确认 `-O1` 能跑通当前 pass 流水线。

要求：

- `-O0` 默认测试通过。
- `-O1` 现有测试通过。
- 优化只在 `gOptLevel > 0` 时启用。

验收命令：

```sh
cmake --build build -j
bash scripts/run_ci_tests.sh --all --compiler ./build/compiler --show-failures
MINIC_EXTRA_ARGS='-O1' bash scripts/run_ci_tests.sh --all --compiler ./build/compiler --show-failures
```

### 阶段 1：Pass 机制和 Verify 安全网

目标：

- 有统一 pass 接口。
- 有 pass manager 管理优化顺序。
- 每个重要 transform 后都能 Verify。

要求：

- `IRPass::run(Module&)` 返回是否成功。
- `IRPassManager` 可以顺序运行多个 pass。
- `VerifyPass` 至少检查：
  - 基本块有 terminator。
  - terminator 是最后一条指令。
  - phi 只出现在基本块开头。
  - phi incoming 和 CFG predecessor 数量一致。
  - phi incoming block 必须是 CFG predecessor。
  - use-def 双向链不能断。
  - `br` 和 `ret` 的基本结构正确。

完成标志：

- 故意破坏 phi incoming 时，VerifyPass 能报错。
- 正常 `-O1` 流水线全绿。

### 阶段 2：Mem2Reg 主体

目标：

完成标量局部变量从内存形式到 SSA 形式的提升。

要求：

- 只提升函数内局部 `alloca`。
- 只提升整数和浮点标量。
- 只允许 `load/store` 使用该 alloca。
- 如果 alloca 被 GEP、call、store 自身、返回等方式使用，不能提升。
- 不能提升数组。
- 不能提升指针逃逸变量。

实现步骤：

1. 收集 promotable alloca。
2. 构建 CFG。
3. 计算 dominator 和 dominance frontier。
4. 找到每个 alloca 的 store blocks。
5. 在 dominance frontier 插入 phi。
6. 沿 dominator tree 重命名变量。
7. 用当前 SSA 值替换 load。
8. 遇到 store 时更新当前 SSA 值栈。
9. 删除被提升的 alloca/load/store。
10. 修补 phi incoming。
11. 运行 VerifyPass。

完成标志：

单基本块：

```c
int x;
x = 1;
x = x + 2;
return x;
```

优化后不应再有 `x` 对应的 `alloca/load/store`。

分支：

```c
int x;
if (a) {
    x = 1;
} else {
    x = 2;
}
return x;
```

优化后应有 phi。

循环：

```c
int i;
i = 0;
while (i < 10) {
    i = i + 1;
}
return i;
```

优化后循环头应有循环变量 phi。

### 阶段 3：IR 清理优化

目标：

mem2reg 之后会产生更多 SSA 值，也会暴露更多可删除、可折叠结构。这个阶段负责把 IR 打扫干净。

#### 3.1 ConstantFoldPass

要求：

- 折叠整数二元运算：`add/sub/mul/sdiv/srem`。
- 折叠浮点二元运算：`fadd/fsub/fmul/fdiv`。
- 除零和取模零不能折叠。
- 暂不折叠 `icmp`，除非能正确生成 `i1` 常量。

完成标志：

```llvm
%x = add i32 1, 2
ret i32 %x
```

变成：

```llvm
ret i32 3
```

#### 3.2 TrivialPhiPass

要求：

- 删除所有 incoming 都是同一个值的 phi。
- 支持忽略自引用 phi。
- 删除 phi 时要维护 use-def。

完成标志：

```llvm
%x = phi i32 [ 3, %then ], [ 3, %else ]
ret i32 %x
```

变成：

```llvm
ret i32 3
```

#### 3.3 DCEPass

要求：

- 删除无副作用且无人使用的指令。
- 保留 `store`、`call`、`br`、`ret`。
- 删除后维护 use-def。
- 需要循环运行，直到没有新死代码。

完成标志：

- 常量折叠和 trivial phi 后留下的无用计算可以被删掉。
- 所有现有测试通过。

#### 3.4 SimplifyCFGPass

当前已完成：

- 删除不可达基本块。
- 删除指向不可达块的 phi incoming。
- 常量条件分支折叠。
- 折叠条件分支时同步维护未选择目标块的 phi incoming。
- 保守删除单前驱、目标无 phi 的空跳转块。

后续要求：

- 更完整地删除空跳转块。
- 合并只有一条无条件跳转的中间块。
- 合并块时正确更新 phi incoming。
- 避免错误处理 critical edge。

完成标志：

```llvm
br i1 true, label %then, label %else
```

可以简化为：

```llvm
br label %then
```

## 5. 后端 Phi Lowering 要求

mem2reg 完成后，IR 里会出现 phi。但真实机器没有 phi 指令，所以后端必须 lowering。

目标：

```llvm
%x = phi i32 [ %a, %then ], [ %b, %else ]
```

变成前驱边上的 copy：

```text
then -> join 边上：x = a
else -> join 边上：x = b
```

要求：

- 无条件分支前能插入 phi copy。
- 条件分支的 true/false 边都能处理。
- 多个 phi 同时存在时，要按 parallel copy 语义处理。
- 不能因为 copy 顺序覆盖源值。
- 遇到 critical edge 时，可以拆边或生成 edge copy block。
- 循环 phi 的 ASM 行为正确。
- 整数和浮点值都要考虑。

当前状态：

- 已有基础 phi lowering。
- 已有基本 parallel copy 防覆盖逻辑。
- 还需要继续压测 critical edge、循环、多 phi、浮点 phi。

完成标志：

- 带 `if/else` phi 的程序 ASM 正确运行。
- 带 `while` 循环 phi 的程序 ASM 正确运行。
- 带多个变量同时 phi 的程序 ASM 正确运行。

## 6. 测试要求

不一定要单独建专项测试目录，但开发时至少要覆盖这些场景。

### 6.1 手工 IR 检查

命令：

```sh
./build/compiler -S -I -O1 input.c -o output.ll
cat output.ll
```

检查点：

- 简单局部变量没有多余 `alloca/load/store`。
- 分支合流有必要 phi。
- trivial phi 会被删除。
- 常量表达式会被折叠。
- 不该提升的数组仍保留内存访问。

### 6.2 ASM 行为检查

命令：

```sh
./build/compiler -S -O1 input.c -o output.s
riscv64-linux-gnu-gcc output.s -o /tmp/a.out
qemu-riscv64 /tmp/a.out
echo $?
```

检查点：

- if/else 结果正确。
- while 结果正确。
- 多变量 phi 结果正确。
- 函数调用前后结果正确。

### 6.3 全量回归

每完成一个 pass 或一类改动，都跑：

```sh
cmake --build build -j
MINIC_EXTRA_ARGS='-O1' bash scripts/run_ci_tests.sh --all --compiler ./build/compiler --show-failures
```

如果改动可能影响 `-O0`，还要跑：

```sh
bash scripts/run_ci_tests.sh --all --compiler ./build/compiler --show-failures
```

## 7. 推荐开发顺序

### 第一优先级：把 mem2reg 做稳

1. 继续补 mem2reg 边界检查。
2. 增加循环样例压测。
3. 增加多变量同时提升样例压测。
4. 检查数组、GEP、指针逃逸不会被提升。

### 第二优先级：完善 CFG 清理

1. 继续完善空跳转块删除。
2. 合并简单基本块。
3. 更新 phi incoming。
4. 避免误合并 critical edge。
5. 每一步后跑 Verify。

### 第三优先级：加强后端 phi lowering

1. 构造多个 phi 同时赋值样例。
2. 构造循环 phi 样例。
3. 构造 true/false 边都需要 copy 的条件分支样例。
4. 检查是否需要拆 critical edge。
5. 确保 ASM 行为正确。

### 第四优先级：做更强优化

1. 更完整 ConstantFold。
2. 简单 CSE。
3. 简单 GVN。
4. 更好的寄存器分配。

## 8. 每个阶段的提交标准

每做完一个阶段，至少满足：

- 能构建。
- `-O1` 全量测试通过。
- VerifyPass 不报错。
- 有一个手工样例能展示该阶段效果。
- 文档或注释能解释为什么这么做。

建议每个阶段都记录：

```text
实现了什么
没有实现什么
为什么当前策略是安全的
用什么命令验证
下一步风险在哪里
```

## 9. 当前下一步建议

最推荐继续做：

```text
SimplifyCFGPass: 更完整的空块合并和基本块合并
```

原因：

- 常量条件分支折叠已经完成，下一步瓶颈是跳转链还不够短。
- 空块合并能继续缩短 `entry -> bb -> ret` 这类结构。
- 基本块合并能减少后端标签和跳转数量。
- 这一步会继续锻炼 CFG 改边时维护 phi incoming 的能力。

示例目标：

```llvm
entry:
  br label %bb
bb:
  br label %exit
exit:
  ret i32 7
```

逐步简化到：

```llvm
entry:
  br label %exit
exit:
  ret i32 7
```
