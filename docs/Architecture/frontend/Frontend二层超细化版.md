# Frontend 二层超细化版

本文档覆盖 `src/frontend` 下所有文件。每个文件包含：

- 关键函数调用链图（谁调谁）
- 修改建议
- 常见坑位

## 1. `src/frontend/antlr4/MiniC.g4`

文件职责：定义 MiniC 的词法与语法规则，驱动 ANTLR 生成器产物。

关键函数调用链图：

```text
CMake custom command
  -> antlr4 -Dlanguage=Cpp MiniC.g4
     -> 生成 MiniCLexer/MiniCParser/MiniCVisitor/MiniCBaseVisitor
```

修改建议：

- 新语法先在 `g4` 增规则，再同步 `CSTVisitor` 节点转换。
- 对运算优先级改动必须同时验证 `relExp/eqExp/lAndExp/lOrExp/addExp/mulExp/unaryExp` 分层。
- `if/else`、`while`、函数形参与实参列表这类成组规则，改文法时要连同 `statement/funcDef/unaryExp` 入口一起检查。
- 数组语法需要同时覆盖声明维度、左值下标、初始化列表和函数形参数组退化；当前入口是 `arrayDims/funcArrayDims/initVal/lVal`。
- 基本类型扩展时要区分 `funcType` 和 `basicType`：函数返回允许 `void`，变量、常量和形参不允许 `void`。
- 浮点常量需要覆盖普通小数、科学计数法和十六进制浮点形式，词法规则必须放在整数字面量规则之前。

常见坑位：

- 关键字规则顺序放在 `T_ID` 后面会被误识别为标识符。
- 左递归/优先级写错会造成解析树结构异常。
- `const int a;` 的错误期望会随着 `arrayDims?` 多出 `[`，错误测试需要同步预期文本。

## 2. `src/frontend/include/AttrType.h`

文件职责：定义前端属性结构（字面量、标识符、类型）。

关键函数调用链图：

```text
CSTVisitor::visit* 
  -> 构造 digit_int_attr / var_id_attr / type_attr
     -> AST 工厂函数消费这些属性
```

修改建议：

- 新增 token 属性（如字符串字面量）时优先扩这里，再扩 AST 构造。
- 新增基本类型时同步扩 `BasicType`、`type_attr` 到 `Type` 的映射，以及 CSTVisitor 的类型规则。

常见坑位：

- `var_id_attr.id` 是裸指针，生命周期管理要统一（当前配合 `strdup/free`）。

## 3. `src/frontend/include/AST.h`

文件职责：AST 节点类型与接口定义。

关键函数调用链图：

```text
CSTVisitor::visit*
  -> ast_node::New/create_*
     -> 产出 AST
IRGenerator::run
  -> 读取 ast_node fields (node_type/sons/type/val/blockInsts)
```

修改建议：

- 新语法节点要先扩 `ast_operator_type`，再补工厂函数，最后补 visitor。
- 对 `ast_node` 字段新增尽量保持“语法字段”与“中端暂存字段”边界清晰。
- 数组相关节点当前是 `AST_OP_ARRAY_DIMS`、`AST_OP_ARRAY_ACCESS`、`AST_OP_INIT_LIST`；函数形参数组第一维省略用 `firstArrayDimOmitted` 标记。
- 浮点字面量使用 `AST_OP_LEAF_LITERAL_FLOAT`，节点类型设置为 `FloatType`。

常见坑位：

- 忘记把新节点加入 `isLeafNode`/图形化映射，会导致可视化不完整。

## 4. `src/frontend/AST.cpp`

文件职责：实现 AST 节点构造、插入、删除与便捷工厂。

关键函数调用链图：

```text
MiniCCSTVisitor::visit*
  -> ast_node::New(...)
  -> ast_node::create_func_def/create_func_call/createVarDeclNode
main.cpp::compile
  -> ast_node::Delete(astRoot)
```

修改建议：

- 若引入新复合节点，优先新增 `create_*` 工厂而不是在 visitor 内手拼，降低重复。
- 大改内存策略时可考虑 `unique_ptr`，但要与现有中端接口一起迁移。

常见坑位：

- `create_func_def(type_attr&, var_id_attr&,...)` 会 `free(id.id)`，外部禁止重复释放。
- `Delete` 是递归释放，循环引用会崩（当前树结构不应出现环）。

## 5. `src/frontend/include/ASTGenerator.h`

文件职责：前端入口类声明。

关键函数调用链图：

```text
main.cpp::compile
  -> ASTGenerator(input).run()
  -> getASTRoot()
```

修改建议：

- 如果前端要支持多输入源（字符串/内存缓冲），可在此层抽象输入接口。

常见坑位：

- `astRoot` 所有权仍在调用方，释放职责在上层。

## 6. `src/frontend/ASTGenerator.cpp`

文件职责：驱动 ANTLR 词法/语法并调用 CST visitor 生成 AST。

关键函数调用链图：

```text
ASTGenerator::run
  -> std::ifstream open
  -> ANTLRInputStream
  -> MiniCLexer
  -> CommonTokenStream
  -> MiniCParser::compUnit
  -> MiniCCSTVisitor::run
```

修改建议：

- 若要增强错误恢复（而非即时报错退出），在 `MiniCErrorListener` 和 parser 配置处扩展。
- 若要做 include 展开，可在 `run()` 前增加预处理步骤。

常见坑位：

- 忘记移除默认 error listener 会出现重复报错输出。
- `syntaxError` 只是记录错误，不会自动终止，后续需要统一检查 `hasError`。

## 7. `src/frontend/include/CSTVisitor.h`

文件职责：声明 CST -> AST 访问器接口。

关键函数调用链图：

```text
ASTGenerator::run
  -> MiniCCSTVisitor::run(root)
     -> visitCompUnit/visitFuncDef/... (override)
```

修改建议：

- 新文法规则要先在此声明 override，再在 `.cpp` 实现。
- 数组规则新增后，需要声明 `visitArrayDims`、`visitFuncArrayDims`、`visitInitVal`，否则生成基类方法不会被项目 visitor 接住。
- 增加 `funcType` 后，需要声明并实现 `visitFuncType`，否则函数返回类型会继续被旧的 `T_INT` 路径固定住。

常见坑位：

- 方法签名与 ANTLR 生成基类不一致会导致“以为 override 实际没覆盖”。

## 8. `src/frontend/CSTVisitor.cpp`

文件职责：CST 到 AST 的核心语义结构转换。

关键函数调用链图：

```text
run
  -> visitCompUnit
     -> visitVarDecl / visitConstDecl / visitFuncDef
        -> visitFuncFParams / visitBlock
           -> visitBlockItemList -> visitStatement
              -> visitIfStatement / visitWhileStatement / visitBreakStatement / visitContinueStatement
              -> visitExpr -> visitLOrExp -> visitLAndExp -> visitEqExp -> visitRelExp
                 -> visitAddExp -> visitMulExp -> visitUnaryExp -> visitPrimaryExp
```

修改建议：

- 新语法继续沿”文法分层 + 左结合构树”扩，关系/逻辑表达式保持与 `add/mul` 同一套路。
- `visitCompUnit` 必须保留源码顺序，避免把全局声明和函数定义重排。
- `visitBlockItem` 需要同时处理 `statement | varDecl | constDecl`。
- `visitVarDecl/visitConstDecl` 中数组声明的孩子顺序固定为：类型、名字、可选 `AST_OP_ARRAY_DIMS`、可选初始化。
- `visitFuncDef` 读取 `funcType`，`visitVarDecl/visitConstDecl/visitFuncFParam` 读取 `basicType`，避免把 `void` 误放进变量声明。
- `visitLVal` 中无下标仍生成普通标识符叶子；带下标才生成 `AST_OP_ARRAY_ACCESS`。
- `visitInitVal` 中普通表达式直接返回表达式节点，花括号初始化生成 `AST_OP_INIT_LIST`，支持空列表和嵌套列表。
- `visitFuncFParam` 中 `int a[]`、`float a[]` 或多维数组形参会把 `AST_OP_ARRAY_DIMS` 挂到形参节点下，第一维省略交给 IR 层降成指针。

常见坑位：

- 一元负号当前转成 `0 - expr`，IR 层会按右侧类型做 int/float 隐式转换。
- `visitExpressionStatement` 返回 `nullptr` 表示空语句，上层插入孩子前必须判空。
- 给新运算符补了文法却没同步 `buildLeftAssociativeBinaryTree` 的操作符映射，会生成错误 AST。
- 把 `visitCompUnit` 写成“先收集变量、再收集函数”会破坏源码顺序。
- 数组维度表达式在前端只保留 AST，不在 CST 阶段强行求值；是否为常量正整数由 IR 层检查。

## 9. `src/frontend/include/Graph.h`

文件职责：声明 AST 图导出接口。

关键函数调用链图：

```text
main.cpp::compile (-T)
  -> OutputAST(astRoot, file)
```

修改建议：

- 如果要支持主题/布局参数，可扩 `OutputAST` 参数对象而不是新增全局变量。

常见坑位：

- 接口参数是 `std::string` 值传递，频繁调用可改 `const std::string&`。

## 10. `src/frontend/Graph.cpp`

文件职责：Graphviz 图构建与渲染。

关键函数调用链图：

```text
OutputAST
  -> graph_visit_ast_node(root)
     -> genLeafGraphNode / genInternalGraphNode
        -> getNodeName
  -> gvLayout
  -> gvRenderFilename
```

修改建议：

- 新 AST 节点出现时，优先补 `getNodeName` 映射。
- 可以把样式参数（颜色、shape、font）提取成配置常量。

常见坑位：

- 没有 `USE_GRAPHVIZ` 时函数是空实现，别把“无输出”误判为 AST 为空。
- Graphviz 资源释放顺序错会内存泄漏（当前顺序是对的）。

## 11. `src/frontend/autogenerated/MiniCLexer.h`

文件职责：ANTLR 生成词法器声明。

关键函数调用链图：

```text
ASTGenerator::run
  -> MiniCLexer lexer(&input)
```

修改建议：

- 不手改。修改词法逻辑请回到 `MiniC.g4`。

常见坑位：

- 手改后会在下一次 CMake 重新生成时被覆盖。

## 12. `src/frontend/autogenerated/MiniCLexer.cpp`

文件职责：ANTLR 词法器实现。

关键函数调用链图：

```text
MiniCLexer
  -> tokenize input stream
  -> CommonTokenStream
```

修改建议：

- 仍然不手改，所有规则改动回到 grammar。

常见坑位：

- 版本升级 ANTLR 后，该文件格式会变化，避免基于行号写死脚本。

## 13. `src/frontend/autogenerated/MiniCParser.h`

文件职责：ANTLR 语法器声明与 Context 类型定义。

关键函数调用链图：

```text
ASTGenerator::run
  -> MiniCParser parser(&tokens)
  -> parser.compUnit() -> CompUnitContext
```

修改建议：

- 如果 visitor 访问不到某规则，先检查这里是否确实生成了对应 Context。

常见坑位：

- 规则重命名后，老的 `visitXxx` 代码会编译失败或失配。

## 14. `src/frontend/autogenerated/MiniCParser.cpp`

文件职责：ANTLR 语法器实现，负责构建 CST。

关键函数调用链图：

```text
MiniCParser::compUnit
  -> funcDef/varDecl/... 子规则递归下降
  -> 返回 CST 根
```

修改建议：

- 不改源码，改 `MiniC.g4` 后重生成。

常见坑位：

- 语法歧义时，生成器会调整预测行为，CST 形状可能变化，visitor 需回归验证。

## 15. `src/frontend/autogenerated/MiniCVisitor.h`

文件职责：ANTLR visitor 接口定义。

关键函数调用链图：

```text
MiniCCSTVisitor (手写)
  : public MiniCBaseVisitor
  -> 覆盖 MiniCVisitor 中声明的方法
```

修改建议：

- 不改自动生成文件；新增规则后重新生成并同步手写 visitor。

常见坑位：

- 接口变化后如果没重新编译全部对象，可能出现链接错误。

## 16. `src/frontend/autogenerated/MiniCVisitor.cpp`

文件职责：visitor 相关默认实现（生成器产物）。

关键函数调用链图：

```text
默认 visitor 分派
  -> 由 MiniCBaseVisitor/子类覆盖接管
```

修改建议：

- 不手改。

常见坑位：

- 误以为这里可以放业务逻辑，实际应在手写 `CSTVisitor.cpp`。

## 17. `src/frontend/autogenerated/MiniCBaseVisitor.h`

文件职责：BaseVisitor 声明，提供默认“访问子节点”行为。

关键函数调用链图：

```text
MiniCCSTVisitor
  -> override 部分方法
  -> 未覆盖方法回落到 BaseVisitor 默认逻辑
```

修改建议：

- 若要强约束“必须覆盖某规则”，在手写 visitor 侧做编译期策略，不改此文件。

常见坑位：

- 忘覆盖关键规则时，默认行为可能静默通过但 AST 不完整。

## 18. `src/frontend/autogenerated/MiniCBaseVisitor.cpp`

文件职责：BaseVisitor 默认实现。

关键函数调用链图：

```text
visitXxx default
  -> visitChildren(ctx)
```

修改建议：

- 不改。

常见坑位：

- 过度依赖默认行为会隐藏 AST 缺失问题。

## 19. `src/frontend/autogenerated/MiniC.tokens`

文件职责：parser token 映射表（符号名 <-> 编号）。

关键函数调用链图：

```text
MiniC.g4
  -> antlr generate
     -> MiniC.tokens
```

修改建议：

- 作为调试工件使用，不手改。

常见坑位：

- token 编号变更后，依赖旧编号的外部工具脚本会失效。

## 20. `src/frontend/autogenerated/MiniCLexer.tokens`

文件职责：lexer token 映射输出。

关键函数调用链图：

```text
MiniC.g4
  -> antlr generate
     -> MiniCLexer.tokens
```

修改建议：

- 不手改；与 `MiniC.tokens` 一并看差异更稳妥。

常见坑位：

- 仅看其中一份 tokens 容易误判词法/语法 token 对齐关系。

## 21. `src/frontend/autogenerated/MiniC.interp`

文件职责：parser 解释表/ATN 序列化数据。

关键函数调用链图：

```text
MiniC.g4
  -> antlr generate
     -> MiniC.interp
```

修改建议：

- 仅作为生成产物保留，不做手工编辑。

常见坑位：

- 把它当源码改没有意义，下次生成会覆盖。

## 22. `src/frontend/autogenerated/MiniCLexer.interp`

文件职责：lexer 解释表/ATN 序列化数据。

关键函数调用链图：

```text
MiniC.g4
  -> antlr generate
     -> MiniCLexer.interp
```

修改建议：

- 不手改；若出现词法异常，回查 grammar 与生成版本。

常见坑位：

- ANTLR 版本变化会让 `.interp` 变化较大，不宜用于业务 diff 评审。
