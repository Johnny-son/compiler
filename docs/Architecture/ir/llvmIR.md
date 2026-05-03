# LLVM IR 学习与扩展指南

本文档用于配合当前自研 IR 的扩展。核心目标是解释：LLVM IR 为什么需要 `alloca / load / store`，为什么普通值必须是 SSA，为什么控制流要用 BasicBlock 和 terminator，以及这些规则如何映射到当前项目的 `Module / Function / BasicBlock / Instruction / IRBuilder / Value` 结构。

---

## 1. LLVM IR 是什么

LLVM IR 有三种等价形态：

```text
内存对象形式     LLVM C++ API 中的 Module / Function / BasicBlock / Instruction
二进制 bitcode   .bc 文件
文本汇编形式     .ll 文件
```

文本 IR 例如：

```llvm
%sum = add i32 %a, %b
ret i32 %sum
```

在内存中不是字符串，而是：

```text
Function
└── BasicBlock
    ├── Add Instruction
    └── ReturnInst
```

LLVM IR 是强类型、SSA-based、低层但可优化的中间表示。

---

## 2. SSA：普通值不能被重新赋值

LLVM IR 的普通寄存器值是 SSA 值。每个 SSA 值只定义一次。

正确：

```llvm
%x = add i32 1, 2
%y = mul i32 %x, 3
```

错误：

```llvm
%x = add i32 1, 2
%x = add i32 %x, 1
```

如果需要新值，应生成新名字：

```llvm
%x = add i32 1, 2
%x.next = add i32 %x, 1
```

这意味着源语言中的可变变量不能直接映射成一个可反复赋值的 LLVM Value。

---

## 3. 为什么需要 alloca / load / store

LLVM 的关键规则：

```text
寄存器值是 SSA 的。
内存位置不是 SSA 的。
```

源代码：

```c
int x;
x = 3;
return x;
```

典型 LLVM lowering：

```llvm
entry:
  %x.addr = alloca i32
  store i32 3, ptr %x.addr
  %x.val = load i32, ptr %x.addr
  ret i32 %x.val
```

含义：

```text
%x.addr 是一个 SSA 值，表示地址，本身不会变。
%x.addr 指向的内存内容可以通过 store 改变。
load 从地址读取当前值，并产生新的 SSA 值。
```

对于命令式语言前端，最简单的变量策略是：

```text
变量名 -> alloca 得到的地址
读变量 -> load 地址
写变量 -> store 新值到地址
```

这与你们当前 IR 的 memory-based lowering 一致。

---

## 4. alloca：局部变量槽位

`alloca` 在当前函数的栈帧上分配局部内存，函数返回时自动释放。

```llvm
%x.addr = alloca i32
```

现代 LLVM 文本 IR 使用 opaque pointer，通常写：

```llvm
store i32 3, ptr %x.addr
%v = load i32, ptr %x.addr
```

旧版教程常写：

```llvm
store i32 3, i32* %x.addr
%v = load i32, i32* %x.addr
```

思想相同：`alloca` 产生地址，`store` 写地址，`load` 读地址。

局部变量的 alloca 通常放在 entry block 开头：

```llvm
entry:
  %a.addr = alloca i32
  %b.addr = alloca i32
  %c.addr = alloca i32
```

好处：

1. 局部变量集中管理。
2. 便于 `mem2reg` 把 alloca 提升成 SSA。
3. 前端实现简单。

---

## 5. store：写内存

```llvm
store i32 3, ptr %x.addr
```

含义：

```text
把 i32 值 3 写入 %x.addr 指向的内存。
```

`store` 没有结果值，不能写成：

```llvm
%t = store i32 3, ptr %x.addr
```

在自研 IR 中，`StoreInst` 应该是：

```text
操作数：value, ptr
结果类型：void
terminator：false
```

典型调用：

```cpp
builder.createStore(value, ptr);
```

---

## 6. load：读内存

```llvm
%x.val = load i32, ptr %x.addr
```

含义：

```text
从 %x.addr 指向的内存读取一个 i32，结果是新的 SSA 值 %x.val。
```

每次读取都应产生新的 SSA 值：

```llvm
%x1 = load i32, ptr %x.addr
store i32 10, ptr %x.addr
%x2 = load i32, ptr %x.addr
```

当前项目中的 `emitRValue()` 本质上就是：

```text
如果当前 Value 是地址，就生成 load。
如果已经是常量或指令结果，就直接返回。
```

---

## 7. 左值与右值

源代码：

```c
x = x + 1;
```

左边的 `x` 是写入位置，需要地址。

右边的 `x` 是当前值，需要 load。

lowering：

```llvm
%x.old = load i32, ptr %x.addr
%x.new = add i32 %x.old, 1
store i32 %x.new, ptr %x.addr
```

在 IRGenerator 中应该保持约定：

```text
标识符节点返回地址。
赋值左侧不要 emitRValue()。
赋值右侧必须 emitRValue()。
赋值表达式按右侧先求值、左侧后求地址的顺序生成。
普通二元表达式的左右操作数必须 emitRValue()。
```

---

## 8. 源语言 const 与 LLVM 内存模型

LLVM IR 本身没有“这个栈槽是 C 语言 const 变量”的直接语义。`const int a = 10;` 在前端语义上表示后续不能再给 `a` 赋值，但 lowering 到 LLVM 风格 IR 时，第一版仍然可以使用普通地址槽位：

```llvm
%a.addr = alloca i32
store i32 10, ptr %a.addr
```

关键点是：**const 检查属于前端/IRGenerator 的语义检查，不是依赖 LLVM 的 store 自动报错。**

当前项目做法：

```text
const 声明 -> 仍创建 AllocaInst 或 GlobalVariable
声明阶段 -> 对这个地址 Value 调用 setConstValue(true)
赋值阶段 -> 如果左值地址 isConstValue()，报 E1303
```

这保持了两层职责：

```text
LLVM-like IR 负责表达 load/store/CFG。
IRGenerator 负责拦截源语言不允许的 const 重新赋值。
```

因此普通初始化是合法的：

```c
const int a = 10;
```

但重新赋值必须在生成 `store` 前被拦截：

```c
a = 20; // E1303
```

---

## 9. BasicBlock 与 terminator

LLVM 函数由基本块组成。每个基本块是一段顺序执行的指令，最后必须以 terminator 结束。

常见 terminator：

```text
ret
br
switch
invoke
```

`ret`：

```llvm
ret i32 %value
ret void
```

`br`：

```llvm
br label %next
br i1 %cond, label %then, label %else
```

条件跳转的条件必须是 `i1`。如果源语言条件是 `i32`，需要：

```llvm
%cond = icmp ne i32 %x, 0
br i1 %cond, label %then, label %else
```

这对应当前项目的 `emitCondValue()`。

错误 IR：

```llvm
entry:
  %x = add i32 1, 2
then:
  ret i32 %x
```

`entry` 没有 terminator。

正确：

```llvm
entry:
  %x = add i32 1, 2
  br label %then

then:
  ret i32 %x
```

---

## 10. if 的 IR 结构

源代码：

```c
if (x > 0) {
    y = 1;
} else {
    y = 2;
}
return y;
```

典型 IR：

```llvm
entry:
  %x.val = load i32, ptr %x.addr
  %cond = icmp sgt i32 %x.val, 0
  br i1 %cond, label %if.then, label %if.else

if.then:
  store i32 1, ptr %y.addr
  br label %if.end

if.else:
  store i32 2, ptr %y.addr
  br label %if.end

if.end:
  %y.val = load i32, ptr %y.addr
  ret i32 %y.val
```

关键接口映射：

```text
func->createBlock("if.then")
func->createBlock("if.else")
func->createBlock("if.end")
builder.createCondBr(cond, thenBlock, elseBlock)
builder.setInsertPoint(thenBlock)
builder.createBr(endBlock)
builder.getInsertBlock()->hasTerminator()
```

---

## 11. while / break / continue

源代码：

```c
while (x < 10) {
    x = x + 1;
}
```

典型 IR：

```llvm
entry:
  br label %while.cond

while.cond:
  %x.val = load i32, ptr %x.addr
  %cond = icmp slt i32 %x.val, 10
  br i1 %cond, label %while.body, label %while.end

while.body:
  %x.old = load i32, ptr %x.addr
  %x.new = add i32 %x.old, 1
  store i32 %x.new, ptr %x.addr
  br label %while.cond

while.end:
  ret void
```

`break` 和 `continue`：

```text
break    -> br while.end
continue -> br while.cond
```

当前项目的 `loopTargets` 栈正是为此服务：

```text
push({condBlock, endBlock})
continue -> condBlock
break -> endBlock
pop()
```

`for` 循环与 `while` 本质上也是基本块跳转，只是多了初始化和步进块。

源代码：

```c
for (i = 0; i < n; i++) {
    sum = sum + i;
}
```

典型 lowering：

```llvm
  store i32 0, ptr %i.addr
  br label %for.cond

for.cond:
  %i.val = load i32, ptr %i.addr
  %cond = icmp slt i32 %i.val, %n
  br i1 %cond, label %for.body, label %for.end

for.body:
  ...
  br label %for.step

for.step:
  %i.old = load i32, ptr %i.addr
  %i.next = add i32 %i.old, 1
  store i32 %i.next, ptr %i.addr
  br label %for.cond

for.end:
```

因此 `for` 的循环上下文是：

```text
push({stepBlock, endBlock})
continue -> stepBlock
break -> endBlock
pop()
```

前端允许 `for (int i = 0, j = 0; i < n; i++, j++)` 这类写法。变量声明仍按 memory-based lowering 在入口块创建 alloca；逗号分隔的初始化或步进表达式在 AST 中折成顺序 block，IRGenerator 逐条翻译。

自增自减不是 LLVM 的独立指令，而是 load、加减一、store 的组合：

```llvm
; i++
%old = load i32, ptr %i.addr
%next = add i32 %old, 1
store i32 %next, ptr %i.addr
```

前置表达式返回 `%next`，后置表达式返回 `%old`。

---

## 12. PHI：SSA 控制流合流

如果不用 memory-based lowering，而是直接生成 SSA，if/else 后的值需要 phi。

源代码：

```c
int x;
if (cond)
    x = 1;
else
    x = 2;
return x;
```

memory-based：

```llvm
%x.addr = alloca i32
br i1 %cond, label %then, label %else

then:
  store i32 1, ptr %x.addr
  br label %end

else:
  store i32 2, ptr %x.addr
  br label %end

end:
  %x = load i32, ptr %x.addr
  ret i32 %x
```

SSA + phi：

```llvm
br i1 %cond, label %then, label %else

then:
  br label %end

else:
  br label %end

end:
  %x = phi i32 [ 1, %then ], [ 2, %else ]
  ret i32 %x
```

phi 规则：

```text
phi 必须在基本块开头。
每个 incoming 是 [value, predecessorBlock]。
phi 的结果是普通 SSA 值。
```

命令式语言前端可以先生成 alloca/load/store，再由 `mem2reg` 优化插入 phi。

---

## 13. GEP：数组和结构体地址计算

`getelementptr` 简称 GEP，用于计算复合对象内部元素的地址。它只算地址，不读写内存。

源代码：

```c
int arr[10];
arr[i] = 42;
```

IR 思路：

```llvm
%elem.addr = getelementptr [10 x i32], ptr %arr.addr, i32 0, i32 %i
store i32 42, ptr %elem.addr
```

读取元素：

```llvm
%elem.addr = getelementptr [10 x i32], ptr %arr.addr, i32 0, i32 %i
%elem = load i32, ptr %elem.addr
```

写入元素：

```llvm
%elem.addr = getelementptr [10 x i32], ptr %arr.addr, i32 0, i32 %i
store i32 %value, ptr %elem.addr
```

添加数组功能时，建议：

```text
arr[i] 作为左值：生成 GEP，返回元素地址。
arr[i] 作为右值：生成 GEP，再 load。
arr[i] = v：生成 GEP，再 store。
```

当前项目已经按这个方式接入数组：

```text
局部/全局数组 a[i]   -> getelementptr base, 0, i
函数形参数组 a[i]   -> 先 load 形参指针槽位，再 getelementptr base, i
子数组作为实参 buf[0] -> 先 GEP 到子数组，再 GEP 0, 0 退化为首元素指针
```

数组初始化：

```text
局部数组无初始化   -> store [N x ...] zeroinitializer, [N x ...]* %arr.addr
局部数组全零初始化 -> store [N x ...] zeroinitializer, [N x ...]* %arr.addr
局部数组非全零初始化 -> 展开初始化列表，逐元素 GEP + store
全局数组初始化 -> 输出 LLVM 聚合常量，缺省元素补 0
空初始化 {}    -> 全部补 0
```

大数组或高维数组非常依赖全零初始化优化。例如：

```c
int a[2][2][2][2][2][2][2][2][2][2][2][2][2][2][2][2][2][2][2] = {0};
```

如果逐元素展开，会生成超过五十万条 `getelementptr + store`。当前项目会先识别 `{}`、`{0}` 和显式元素全为 0 的初始化列表，然后输出聚合零初始化：

```llvm
store [2 x [2 x ...]] zeroinitializer, [2 x [2 x ...]]* %a.addr
```

全局大数组也同理，直接输出：

```llvm
@buffer = global [50000000 x i32] zeroinitializer, align 4
```

---

## 14. 类型系统

LLVM IR 是强类型的。常见类型：

```text
i1          条件
i8          byte / char
i32         32 位整数
float       32 位浮点
void        无返回值
ptr         现代 opaque pointer
[10 x i32]  数组
{ i32, i8 } 结构体
```

指令类型必须匹配：

```llvm
%z = add i32 %x, %y
%f = fadd float %a, %b
%cond = icmp slt i32 %x, 0
br i1 %cond, label %a, label %b
```

错误：

```llvm
%z = add i32 %x, i1 %cond
br i32 %x, label %a, label %b
```

常见转换：

```text
i32 条件 -> icmp ne i32 value, 0 -> i1
float 条件 -> fcmp one float value, 0.0 -> i1
比较表达式作为普通值 -> icmp 得 i1，再 zext 到 i32
int -> float -> sitofp
float -> int -> fptosi
```

当前项目的 float 接入约定：

```text
float 字面量      -> ConstFloat，输出 LLVM 十六进制浮点常量文本
float 算术        -> fadd/fsub/fmul/fdiv
float 比较        -> fcmp oeq/one/olt/ole/ogt/oge，再 zext 到 i32
float 数组        -> ArrayType(FloatType)，GEP/load/store 路径与 int 数组一致
int/float 混合表达式 -> 先按需要插入 sitofp/fptosi
void 函数         -> ReturnInst::createRetVoid 或函数尾部自动补 ret void
```

---

## 15. 函数和调用

函数定义：

```llvm
define i32 @add(i32 %a, i32 %b) {
entry:
  %sum = add i32 %a, %b
  ret i32 %sum
}
```

函数声明：

```llvm
declare i32 @getint()
declare void @putint(i32)
```

调用：

```llvm
%x = call i32 @getint()
call void @putint(i32 %x)
```

规则：

```text
非 void call 有结果。
void call 没有结果。
实参数量和类型应匹配函数签名。
```

当前项目映射：

```text
module->newFunction()
module->findFunction()
builder.createCall(callee, args, name)
```

编译单元推荐三阶段：

```text
第一遍：预声明所有函数
第二遍：处理全局变量
第三遍：生成函数体
```

---

## 16. Value / User / Use

LLVM 中常量、参数、函数、基本块、指令都可以是 `Value`。

大多数指令是 `User`，因为它们使用其他 Value。

```llvm
%sum = add i32 %a, %b
```

关系：

```text
%a, %b 是被使用的 Value。
add 指令是 User。
%sum 是 add 指令本身这个 Value。
```

这意味着：

```text
没有“临时变量”和“产生它的指令”的分离。
指令本身就是它产生的值。
```

当前项目的 `Instruction : User : Value` 是正确方向。

优化依赖 def-use，例如：

```llvm
%x = add i32 %a, 0
%y = mul i32 %x, 2
```

可以把 `%x` 的所有 use 替换为 `%a`：

```llvm
%y = mul i32 %a, 2
```

这对应 `replaceAllUseWith()`。

---

## 17. IRBuilder

LLVM C++ API 中的 `IRBuilder` 负责在当前插入点创建指令。

```cpp
BasicBlock *BB = BasicBlock::Create(Context, "entry", TheFunction);
Builder.SetInsertPoint(BB);

Value *Sum = Builder.CreateAdd(L, R, "addtmp");
Builder.CreateRet(Sum);
```

核心思想：

```text
Builder 记录当前插入位置。
CreateXXX 创建指令并插入当前 BasicBlock。
有结果的指令返回 Value*。
名字只是提示，重名会自动处理。
```

当前项目的 `IRBuilder` 与这个模型一致：

```text
setInsertPoint(block)
createAdd(lhs, rhs, name)
createStore(value, ptr)
createCondBr(cond, trueBlock, falseBlock)
createRet(value)
```

建议：新增 LLVM IR 功能时，优先扩展 `IRBuilder`，不要让 IRGenerator 到处直接 new 指令。

---

## 18. Verifier：IR 不是能打印就正确

LLVM 有 verifier 检查 IR 是否 well-formed。

常见错误：

```text
BasicBlock 没有 terminator。
terminator 后还有普通指令。
br 条件不是 i1。
ret 类型和函数返回类型不匹配。
指令操作数类型不匹配。
phi 不在基本块开头。
phi incoming block 不正确。
SSA 值定义不支配使用。
```

建议给自研 IR 添加 `IRVerifier`：

```text
检查每个 BasicBlock 是否以 terminator 结束。
检查 terminator 后是否有指令。
检查 br 条件是否 i1。
检查 ret 类型是否匹配。
检查 load 操作数是否是指针。
检查 store value 类型是否与目标地址兼容。
检查 call 参数数量和类型。
检查 phi 位置和 incoming。
```

---

## 19. mem2reg

命令式语言前端直接生成 SSA 较复杂。常用路线：

```text
前端生成 alloca/load/store。
优化阶段用 mem2reg/SROA 提升到 SSA。
LLVM 自动插入 phi。
```

例如：

```llvm
%x.addr = alloca i32
store i32 1, ptr %x.addr
%x = load i32, ptr %x.addr
ret i32 %x
```

可能优化成：

```llvm
ret i32 1
```

if/else 中的变量也可能被提升成 phi。

---

## 20. opaque pointer

较新 LLVM 默认使用 opaque pointer，文本 IR 中指针写作：

```llvm
ptr
```

而不是旧版的：

```llvm
i32*
[10 x i32]*
```

现代写法：

```llvm
%p = alloca i32
store i32 1, ptr %p
%v = load i32, ptr %p
```

对当前项目的建议：

```text
内部 Type 系统可以保留 PointerType 和 elementType，便于教学和类型检查。
输出真实 LLVM IR 时，根据目标 LLVM 版本选择 ptr 语法。
load/store/GEP 的元素类型仍需要明确。
```

---

## 21. LLVM 概念与当前项目映射

| LLVM 概念 | 当前项目结构 | 建议 |
|---|---|---|
| Module | `Module` | 顶层容器，管理函数、全局变量、常量、作用域 |
| Function | `Function` | 保存返回类型、参数、基本块、局部命名 |
| BasicBlock | `BasicBlock` | 保证 terminator 规则 |
| Instruction | `Instruction` 子类 | 保持 `Instruction : User : Value` |
| IRBuilder | `IRBuilder` | 新指令优先添加 builder 接口 |
| Value | `Value` | 所有可被使用实体都是 Value |
| User/Use | `User` / `Use` | 维护 def-use |
| const 语义 | `Value::constValue` + `IRGenerator` 检查 | 声明阶段标记，赋值阶段报 E1303 |
| alloca | `AllocaInst` | 局部变量槽位 |
| load | `LoadInst` | 右值读取 |
| store | `StoreInst` | 赋值、初始化、参数落槽 |
| br | `BranchInst` | if/while/break/continue |
| ret | `ReturnInst` | 函数返回 |
| call | `CallInst` | 函数调用 |
| phi | `PhiInst` | SSA 合流，可先预留 |
| GEP | `GetElementPtrInst` | 数组、结构体、指针元素地址计算 |
| fcmp | `FCmpInst` | 浮点比较 |
| cast | `CastInst` | `sitofp` / `fptosi` |
| verifier | 待补充 | 强烈建议添加 |

---

## 22. 添加 LLVM IR 功能的推荐顺序

### 22.1 先补 verifier

```text
BasicBlock terminator 检查
ret 类型检查
br 条件类型检查
load/store 类型检查
call 参数数量和类型检查
GEP 索引检查
```

### 22.2 巩固 memory-based lowering

```text
变量声明 -> createEntryAlloca + bindValue
const 声明 -> 标记地址 Value 的 constValue
变量读取 -> emitRValue -> load
变量写入 -> createStore
条件表达式 -> emitCondValue -> i1
```

### 22.3 扩展数组和 GEP

```text
ArrayType
PointerType
GetElementPtrInst
数组声明
数组元素左值
数组元素右值
数组元素赋值
数组形参退化
数组初始化列表展开
```

### 22.4 扩展 void / float

```text
funcType -> int / void / float
basicType -> int / float
FloatType
ConstFloat
BinaryInst 浮点 op
FCmpInst
CastInst
运行库 getfloat/getfarray/putfloat/putfarray 声明
```

### 22.5 准备 SSA / phi

短期保留 memory-based。

长期可以实现简化版 mem2reg 或 SSA 构造，再使用 `PhiInst`。

### 22.5 输出真实 LLVM IR

需要注意：

```text
LLVM 版本和 opaque pointer 语法
target triple
datalayout
declare 内置函数签名
全局变量初始化规则
命名规则
类型严格匹配
```

---

## 23. 推荐 IRGenerator 模板

函数：

```text
predeclare_function()
  -> module->newFunction(name, returnType, params)

ir_function_body()
  -> module->setCurrentFunction(func)
  -> entry = func->createBlock("entry")
  -> builder.setInsertPoint(entry)
  -> module->enterScope()
  -> 为每个形参 createEntryAlloca + store + bindValue
  -> 翻译语句块
  -> 如果最后没有 terminator，根据返回类型补 ret
  -> module->leaveScope()
```

表达式：

```text
整数常量 -> module->newConstInt()
变量名 -> module->lookupValue()，返回地址
const 声明 -> 复用变量声明路径，初始化后禁止再次赋值
二元表达式 -> emitRValue(lhs), emitRValue(rhs), builder.createXXX()
关系表达式 -> builder.createICmpXXX()，需要 i32 值时 zext
自增自减 -> load old，add/sub 1，store new；前置返回 new，后置返回 old
函数调用 -> 查 callee，实参 emitRValue，builder.createCall()
```

语句：

```text
赋值 -> 先右侧右值，再左侧地址，builder.createStore()
const 赋值 -> createStore 前检查左侧地址 isConstValue()，报 E1303
return -> emitRValue，builder.createRet()
if -> createBlock + createCondBr + 补 br + endBlock
while -> cond/body/end 三块 + loopTargets
break -> br loop.end
continue -> br loop.cond
```

---

## 24. 常见错误清单

### 24.1 把变量当 SSA 值反复赋值

错误：

```text
x = x + 1 时，直接修改 x 对应 Value。
```

正确：

```text
x 绑定地址。
load 得旧值。
add 得新 SSA 值。
store 写回地址。
```

### 24.2 对左值过早 load

赋值左侧需要地址，不能 `emitRValue()`。

### 24.3 BasicBlock 没有 terminator

每个块都要以 `br`、`ret` 等 terminator 结束。

### 24.4 terminator 后继续插入指令

一旦当前块 `ret` 或 `br`，后续指令必须插入新块。

### 24.5 br 条件不是 i1

`int` 条件应转为：

```llvm
%cond = icmp ne i32 %x, 0
```

### 24.6 GEP 后忘记 load/store

GEP 只算地址。

### 24.7 phi 放错位置

phi 必须在基本块开头。

### 24.8 call 参数不匹配

调用前应检查实参数量和类型。

### 24.9 忘记在 IR 层检查 const 赋值

`const` 是源语言语义，不会因为 LLVM IR 中使用了 `store` 自动被禁止。

正确做法：

```text
const 声明时标记地址 Value。
赋值时先检查左侧地址是否 isConstValue()。
如果是，则报 E1303，不生成 store。
```

---

## 25. 总结

LLVM IR 的核心学习路线：

```text
1. LLVM IR 有内存对象、bitcode、文本 .ll 三种形态。
2. 普通寄存器值是 SSA 值，不能重新赋值。
3. 可变变量通过内存建模：alloca 分配地址，store 写，load 读。
4. BasicBlock 构成 CFG，每个块必须以 terminator 结束。
5. 条件跳转需要 i1。
6. if/while/break/continue 本质是创建并连接 BasicBlock。
7. phi 用于 SSA 控制流合流。
8. 命令式前端可以先用 alloca/load/store，再交给 mem2reg。
9. GEP 只计算地址，数组元素读写仍然要 load/store。
10. Value/User/Use 是 def-use 基础。
11. IRBuilder 是生成指令的统一门面。
12. const 重新赋值应在 IRGenerator 中拦截，不依赖 LLVM store 检查。
```

对当前项目最重要的实现原则：

```text
IR 层作为库提供 LLVM-like 对象模型。
IRGenerator 根据 AST 语义调用接口。
变量名绑定地址，不直接绑定可变 SSA 值。
表达式需要右值时统一走 emitRValue()。
条件需要 i1 时统一走 emitCondValue()。
控制流统一通过 BasicBlock + BranchInst 建模。
后续 LLVM IR 功能优先通过 Type / Instruction / IRBuilder / Verifier 四层扩展。
```

---

## 参考资料

- LLVM Language Reference Manual
- LLVM Kaleidoscope Tutorial, Chapter 3: Code generation to LLVM IR
- LLVM Kaleidoscope Tutorial, Chapter 7: Mutable Variables
- LLVM Programmer's Manual
