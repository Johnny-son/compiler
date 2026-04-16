# docs

存放开发中的设计文档  

## 参考文档
[llvm参考文档](https://evian-zhang.github.io/llvm-ir-tutorial/)  

## 设计思路

### log信息
位置: common/Status.cpp/h  
使用方法:  
```cpp
Status::Error();
```

### 模块
frontend -> ir -> backend。
frontend: inputFile -> ASTGenerator -> CSTVisitor -> AST  
ir: astRoot -> IRGenerator -> module  
backend: module -> CodeGenerator -> asm  

目前基本是照搬exp4, 然后删了没用的东西, 前端改了下位置,可读性好一点  
ir那里应该是改成llvm了, 但是应该比较史, 不过能过测试
arm32 改成 riscv64了
ci的话等明天看看吧, ai用完了

寄存器分配采用的是最简单的,后续可以改成线性扫描或者图着色