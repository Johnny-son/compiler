# Backend 二层超细化版

本文档覆盖 `src/backend` 下所有文件。每个文件都给出：

- 关键函数调用链图（谁调谁）
- 修改建议（改这个文件最稳妥的切入点）
- 常见坑位（最容易踩雷的点）

## 1. `src/backend/include/BackendDriver.h`

文件职责：后端门面声明，定义 `BackendDriver::run`。

关键函数调用链图：

```text
main.cpp::compile
  -> BackendDriver::run(module, output)
```

修改建议：

- 如果你要加“后端 Pass 管道”（如 peephole、汇编优化），优先从 `run()` 的阶段序列入手。
- 保持 `run()` 只做调度，不要塞具体翻译细节，避免头文件频繁抖动。

常见坑位：

- 在头文件里引入过多实现类会导致编译耦合暴涨，保持前置声明即可。

## 2. `src/backend/BackendDriver.cpp`

文件职责：后端总调度，实现全局段输出 + 函数级翻译。

关键函数调用链图：

```text
BackendDriver::run
  -> IRAdapter::adapt
  -> IRModuleView::globals
  -> (emit .bss/.data)
  -> IRModuleView::functions
     -> FrameLayoutBuilder::build
     -> InstructionSelector::run
     -> AsmPrinter::printFunction
```

修改建议：

- 新增后端阶段（如 MachineIR 层）时，在 `run()` 中明确插入新阶段，不要在 selector 内“偷偷做”。
- 全局变量段输出建议抽成独立私有函数（例如 `emitGlobals`），降低 `run()` 长度。
- 如果后续支持多个目标架构，可在此处加 target 分发（`switch target`）。

常见坑位：

- `.bss`/`.data` 分段顺序与重复输出标记（`emittedBss/emittedData`）要保持一致，否则汇编段会乱。
- `global.name()` 与 `IRName` 不同语义，误用会导致符号名不对。
- 忘记跳过 builtin 函数会生成非法汇编函数体。

## 3. `src/backend/include/IRAdapter.h`

文件职责：定义后端 IR 只读视图 API。

关键函数调用链图：

```text
BackendDriver / FrameLayout / InstructionSelector
  -> IRModuleView / IRFunctionView / IRInstView / IRValueView
```

修改建议：

- 新增 IR 指令种类时，先补 `IRInstKind`，再补 `IRInstView` 查询接口。
- 对外保持“只读、轻量”设计，不要在 View 上加写操作。

常见坑位：

- `kind()` 分类与真实 `dynamic_cast` 不一致会让 selector 分支跑偏。
- 在头文件做复杂逻辑会导致编译时间暴涨。

## 4. `src/backend/IRAdapter.cpp`

文件职责：实现视图分类与数据提取。

关键函数调用链图：

```text
IRAdapter::adapt
  -> IRModuleView(module)
     -> functions()/globals()
        -> IRFunctionView
           -> instructions()/params()/locals()
              -> IRInstView / IRValueView
```

修改建议：

- 每次扩展 IR 操作码时，必须同步更新 `classifyInstruction`。
- 如果调用指令类型变复杂（间接调用、函数指针），优先扩 `IRInstView` 接口而不是在 selector 里 `dynamic_cast`。
- 可以考虑缓存 `kind()`，减少重复 `dynamic_cast`。

常见坑位：

- `FuncCallInstruction` 与 `GotoInstruction` 的特化读取（`calledFunctionRaw/targetLabelRaw`）忘了判空会崩。
- 视图返回 `std::vector` 是拷贝语义，性能敏感路径要注意频繁构造。

## 5. `src/backend/include/FrameLayout.h`

文件职责：声明栈帧对象模型和构建器。

关键函数调用链图：

```text
BackendDriver::run
  -> FrameLayoutBuilder::build(function)
     -> FunctionFrameLayout
        -> InstructionSelector 使用 slotOf()/frameSize()
```

修改建议：

- 新增栈对象类型（如 spill slot）先扩 `StackObjectKind`。
- 保持 `FunctionFrameLayout` 仅保存布局结果，不承担策略计算。

常见坑位：

- 修改 `savedRaOffset/savedFpOffset` 时需同步校验 entry/exit 发射逻辑。
- `stackSlotSize` 和 `stackAlign` 的变更会影响全部偏移。

## 6. `src/backend/FrameLayout.cpp`

文件职责：实现栈帧布局算法。

关键函数调用链图：

```text
FrameLayoutBuilder::build
  -> appendValueSlot(param)
  -> appendValueSlot(local)
  -> appendValueSlot(inst.result)
  -> 计算 outgoingArgArea
  -> alignTo(frame)
```

修改建议：

- 引入按类型紧凑布局时，把 `slotSizeForType` 与 `appendValueSlot` 一并重构。
- 可新增 debug dump（打印每个 slot），便于定位偏移问题。
- 如果后续支持 caller/callee-saved spill，建议显式建新 slot kind。

常见坑位：

- `cursor` 增长方向和负偏移语义混淆，容易导致地址反向。
- `hasSlot` 去重依赖 `Value*` 地址，跨阶段复制对象会失效。
- 忘记 16 字节对齐会破坏 ABI。

## 7. `src/backend/include/InstructionSelector.h`

文件职责：声明 IR -> RV64 指令选择器。

关键函数调用链图：

```text
BackendDriver::run
  -> InstructionSelector::run
     -> translateInst(kind)
        -> translateEntry/Exit/Assign/Binary/Call/Label/Goto
```

修改建议：

- 新指令支持要先补 `translateXxx`，再在 `translateInst` 分发接线。
- 保持 `loadValue/storeValue` 作为统一数据通道，避免模板逻辑散落。

常见坑位：

- 头文件加太多 inline 逻辑会导致难以维护。

## 8. `src/backend/InstructionSelector.cpp`

文件职责：后端最核心翻译文件，发射具体 RV64 指令模板。

关键函数调用链图：

```text
InstructionSelector::run
  -> for inst in function.instructions
     -> translateInst
        -> translateEntry
           -> storeValue(a0..a7 -> param slots)
        -> translateAssign
           -> loadValue(src) -> storeValue(dst)
        -> translateBinary
           -> load lhs/rhs -> op -> store result
        -> translateCall
           -> load args -> call -> store return
        -> translateExit
           -> load return -> restore fp/ra -> ret
```

修改建议：

- 把寄存器策略抽象成小组件（例如 `TempRegAllocator`），减少硬编码 `t0/t1/t2`。
- `translateCall` 建议补 ABI 注释，明确溢出参数布局规则。
- 可把 label 管理抽成 `LabelManager`，避免 selector 状态膨胀。
- 新增条件分支时推荐先补 `IRInstKind::BrCond` 再扩翻译。

常见坑位：

- `frameSize + savedRaOffset` 的寻址基准是 `sp` 还是 `fp`，极易写反。
- `loadOpcode/storeOpcode` 与类型大小不一致会造成高位脏数据。
- 全局变量访问必须先 `la` 再 `lw/sw`，直接内存操作会汇编错误。
- 超 8 参调用时写栈区偏移如果不按 slot 大小对齐会错参。

## 9. `src/backend/include/Asm.h`

文件职责：声明汇编对象模型（操作数/指令/函数/打印器）。

关键函数调用链图：

```text
InstructionSelector
  -> AsmFunction::emitOp / emitLabel
     -> AsmPrinter::printFunction / toString
```

修改建议：

- 若要支持注释增强，优先扩 `AsmInstruction.comment` 的生产点。
- 若支持多架构，可把 `AsmOperandKind` 维持通用，架构差异放打印层。

常见坑位：

- 内存操作数字符串格式必须统一 `offset(base)`，不要在 selector 自拼字符串。

## 10. `src/backend/Asm.cpp`

文件职责：实现汇编对象构造与文本输出。

关键函数调用链图：

```text
AsmOperand::reg/imm/mem/label/symbol
  -> AsmInstruction::makeOp/makeLabel
    -> AsmFunction::emit
      -> AsmPrinter::printFunction
```

修改建议：

- 如果要输出 `.cfi`、`.option`、节属性，集中在 `AsmPrinter` 增加，不要污染 selector。
- 支持“缩进风格/注释风格”可引入 `AsmPrintOptions`。

常见坑位：

- `printFunction` 每次会输出 `.text/.globl/.type/.size`，若你改成按 TU 聚合输出，注意去重。
- `toString` 与 `printFunction` 必须保持语义一致，否则调试文本和落盘结果会分叉。

