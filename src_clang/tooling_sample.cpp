//------------------------------------------------------------------------------
// Tooling sample. Demonstrates:
//
// * How to write a simple source tool using libTooling.
// * How to use RecursiveASTVisitor to find interesting AST nodes.
// * How to use the Rewriter API to rewrite the source code.
//
// Eli Bendersky (eliben@gmail.com)
// This code is in the public domain
//------------------------------------------------------------------------------
/*
将这个例子的代码作为蓝本进行改造，就可以很快地做出属于自己的source-to-source编译器。

先说说代码的思路：

1.ASTConsumer负责读取Clang解析出来的AST树 
2.在ASTConsumer中重写HandleTopLevelDecl函数用以检测源码中的函数声明语句（见上面效果代码） 
3. RecursiveASTVisitor类负责实际对源码的改写 
4. 在RecursiveASTVisitor中重写VisitStmt函数与VisitFunctionDecl函数实现源码中目标语素的检测以及改写动作 
5. 改写好的源码送入Rewriter类中，进行写入源代码文件的动作 



编译：
clang++ $(llvm-config --cxxflags --ldflags --libs --system-libs) tooling_sample.cpp -lclangAST -lclangASTMatchers -lclangAnalysis -lclangBasic -lclangDriver -lclangEdit -lclangFrontend -lclangFrontendTool -lclangLex -lclangParse -lclangSema -lclangEdit -lclangRewrite -lclangRewriteFrontend -lclangStaticAnalyzerFrontend -lclangStaticAnalyzerCheckers -lclangStaticAnalyzerCore -lclangCrossTU -lclangIndex -lclangSerialization -lclangToolingCore -lclangTooling -lclangFormat -o main 

运行：
先声明库目录：
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ewenwan/wanyouwen/software/llvm/llvm_lib 

运行：
./c2c_main ../inputs/cfunc_with_if.c 


输出：
** Creating AST consumer for: /home/ewenwan/wanyouwen/software/llvm/project/llvm-clang-samples-master/src_clang/../inputs/cfunc_with_if.c
FunctionDecl 0x1e62ee8 </home/ewenwan/wanyouwen/software/llvm/project/llvm-clang-samples-master/src_clang/../inputs/cfunc_with_if.c:1:1, line:5:1> line:1:6 foo 'void (int *, int *)'
|-ParmVarDecl 0x1e62d90 <col:10, col:15> col:15 used a 'int *'
|-ParmVarDecl 0x1e62e10 <col:18, col:23> col:23 used b 'int *'
`-CompoundStmt 0x1e63198 <col:26, line:5:1>
  `-IfStmt 0x1e63180 <line:2:3, line:4:3>
    |-BinaryOperator 0x1e63090 <line:2:7, col:14> 'int' '>'
    | |-ImplicitCastExpr 0x1e63078 <col:7, col:10> 'int' <LValueToRValue>
    | | `-ArraySubscriptExpr 0x1e63038 <col:7, col:10> 'int' lvalue
    | |   |-ImplicitCastExpr 0x1e63020 <col:7> 'int *' <LValueToRValue>
    | |   | `-DeclRefExpr 0x1e62fe0 <col:7> 'int *' lvalue ParmVar 0x1e62d90 'a' 'int *'
    | |   `-IntegerLiteral 0x1e63000 <col:9> 'int' 0
    | `-IntegerLiteral 0x1e63058 <col:14> 'int' 1
    `-CompoundStmt 0x1e63168 <col:17, line:4:3>
      `-BinaryOperator 0x1e63148 <line:3:5, col:12> 'int' '='
        |-ArraySubscriptExpr 0x1e63108 <col:5, col:8> 'int' lvalue
        | |-ImplicitCastExpr 0x1e630f0 <col:5> 'int *' <LValueToRValue>
        | | `-DeclRefExpr 0x1e630b0 <col:5> 'int *' lvalue ParmVar 0x1e62e10 'b' 'int *'
        | `-IntegerLiteral 0x1e630d0 <col:7> 'int' 0
        `-IntegerLiteral 0x1e63128 <col:12> 'int' 2
FunctionDecl 0x1e63318 </home/ewenwan/wanyouwen/software/llvm/project/llvm-clang-samples-master/src_clang/../inputs/cfunc_with_if.c:7:1, col:26> col:6 bar 'void (float, float)'
|-ParmVarDecl 0x1e631c8 <col:10, col:16> col:16 x 'float'
`-ParmVarDecl 0x1e63248 <col:19, col:25> col:25 y 'float'
** EndSourceFileAction for: /home/ewenwan/wanyouwen/software/llvm/project/llvm-clang-samples-master/src_clang/../inputs/cfunc_with_if.c
// Begin function foo returning void
void foo(int* a, int *b) {
  if (a[0] > 1) // the 'if' part
  {
    b[0] = 2;
  }
}
// End function foo

void bar(float x, float y); // just a declaration 

*/


#include <sstream>
#include <string>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"      // 递归 AST遍历
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"        // 重写 c代码
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolingSampleCategory("Tooling Sample");

// By implementing RecursiveASTVisitor, we can specify which AST nodes
// we're interested in by overriding relevant methods.  通过 RecursiveASTVisitor 可以遍历特点的节点 并添加重写内容
class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
  // RecursiveASTVisitor类负责实际对源码的改写 
public:
  MyASTVisitor(Rewriter &R) : TheRewriter(R) {}
 
  // 在RecursiveASTVisitor中重写VisitStmt函数与VisitFunctionDecl函数实现源码中目标语素的检测以及改写动作 
  // 改写好的源码送入Rewriter类中，进行写入源代码文件的动作 
  
  // 语句statements Stmt  遍历          expression表达式
  bool VisitStmt(Stmt *s) {
    // Only care about If statements.
    if (isa<IfStmt>(s)) {    // 是 if 语句
      IfStmt *IfStatement = cast<IfStmt>(s);
      Stmt *Then = IfStatement->getThen();

      TheRewriter.InsertText(Then->getBeginLoc(), "// the 'if' part\n", true,
                             true);
                // 在if语句 后面添加注释   getLocStart() 旧接口 ---> getBeginLoc()

      Stmt *Else = IfStatement->getElse();
      if (Else)
        TheRewriter.InsertText(Else->getBeginLoc(), "// the 'else' part\n",
                               true, true);
                // 在else语句后面添加注释
    }

    return true;
  }

  // 函数定义(function definitions) 遍历
  bool VisitFunctionDecl(FunctionDecl *f) {
    // Only function definitions (with bodies), not declarations.
    if (f->hasBody()) {       // 有函数体 函数定义
      
      // 函数体是一个语句集和 statements set
      Stmt *FuncBody = f->getBody();

      // Type name as string
      QualType QT = f->getReturnType();       // 函数返回值类型
      std::string TypeStr = QT.getAsString(); // 对于的类型字符串

      // Function name
      DeclarationName DeclName = f->getNameInfo().getName(); // 函数名
      std::string FuncName = DeclName.getAsString();

      // Add comment before  生成函数头注释
      std::stringstream SSBefore;
      SSBefore << "// Begin function " << FuncName << " returning " << TypeStr
               << "\n";
      
      // 获取函数开头位置
      SourceLocation ST = f->getSourceRange().getBegin();
      // 插入注释
      TheRewriter.InsertText(ST, SSBefore.str(), true, true);

      // And after 添加函数尾注释
      std::stringstream SSAfter;
      SSAfter << "\n// End function " << FuncName;
      ST = FuncBody->getEndLoc().getLocWithOffset(1); // 函数体结束后的后面一个位置   getLocEnd() 旧接口 ---> getEndLoc()
      //  插入注释
      TheRewriter.InsertText(ST, SSAfter.str(), true, true);
    }

    return true;
  }

private:
  // 代码重写类对象实例
  Rewriter &TheRewriter;
};

// ASTConsumer负责读取Clang解析出来的AST树 并调用 MyASTVisitor 进行 匹配与改写
// Implementation of the ASTConsumer interface for reading an AST produced
// by the Clang parser.
class MyASTConsumer : public ASTConsumer {
public:
  MyASTConsumer(Rewriter &R) : Visitor(R) {}

  // Override the method that gets called for each parsed top-level
  // declaration.   遍历声明
  // 在ASTConsumer中重写HandleTopLevelDecl函数用以检测源码中的函数声明语句
  bool HandleTopLevelDecl(DeclGroupRef DR) override {
    for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      // Traverse the declaration using our AST visitor.
      
      // 逐个遍历 调用 MyASTVisitor 对源码进行匹配与改写
      Visitor.TraverseDecl(*b);
      
      (*b)->dump();
    }
    return true;
  }

private:
  MyASTVisitor Visitor;   // 上面定义的 遍历AST类实例
};


// 前端 动作执行 类

// For each source file provided to the tool, a new FrontendAction is created.
class MyFrontendAction : public ASTFrontendAction {
public:
  MyFrontendAction() {}
  void EndSourceFileAction() override {
    // 源码管理器
    SourceManager &SM = TheRewriter.getSourceMgr();
    llvm::errs() << "** EndSourceFileAction for: "
                 << SM.getFileEntryForID(SM.getMainFileID())->getName() << "\n";  // 打印源码文件名

    // Now emit the rewritten buffer.
    TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef file) override {
    llvm::errs() << "** Creating AST consumer for: " << file << "\n";
    TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<MyASTConsumer>(TheRewriter);
  }

private:
  Rewriter TheRewriter;
};

int main(int argc, const char **argv) {
  CommonOptionsParser op(argc, argv, ToolingSampleCategory);
  ClangTool Tool(op.getCompilations(), op.getSourcePathList());

  // ClangTool::run accepts a FrontendActionFactory, which is then used to
  // create new objects implementing the FrontendAction interface. Here we use
  // the helper newFrontendActionFactory to create a default factory that will
  // return a new MyFrontendAction object every time.
  // To further customize this, we could create our own factory class.
  return Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
}
