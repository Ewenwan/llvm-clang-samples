 /*
参考：https://zhuanlan.zhihu.com/p/102270840
1.获取待处理的C程序代码的LLVM IR字节码
clang -emit-llvm -c test.c -o test.bc

2.再编译项目代码
clang++ $(llvm-config --cxxflags --ldflags --libs) main.cpp -o main

3. 最后运行得到上文图示的结果
./main test.bc

打印 函数签名
C语言中的函数签名由以下几部分组成：
返回类型
函数名
参数个数及参数类型 

*/

// 引入相关LLVM头文件
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>             // IR 容器
#include <llvm/IRReader/IRReader.h>     // parseIRFile 解析IR文件到 Module
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/CommandLine.h>

using namespace llvm;

// LLVM上下文全局变量
static ManagedStatic<LLVMContext> GlobalContext;

// 命令行位置参数全局变量, 这个参数的含义是需要处理的LLVM IR字节码的文件名
static cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<filename>.bc"), cl::Required);

int main(int argc, char **argv) {
    // 诊断实例
    SMDiagnostic Err;
    // 格式化命令行参数,
    cl::ParseCommandLineOptions(argc, argv);
    // 读取并格式化LLVM IR字节码文件, 返回LLVM Module(Module是LLVM IR的顶级容器)
    std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, *GlobalContext);
    // 错误处理
    if (!M) {
        Err.print(argv[0], errs());
        return 1;
    }
    // 遍历Module中的每一个Function
    for (Function &F:*M) { // 范围for  解包  函数引用
        // 过滤掉那些以llvm.开头的无关函数
        if (!F.isIntrinsic()) {
            // 打印函数返回类型
            outs() << *(F.getReturnType());
            // 打印函数名
            outs() << ' ' << F.getName() << '(';
            // 遍历函数的每一个参数
            for (Function::arg_iterator it = F.arg_begin(), ie = F.arg_end(); it != ie; it++) {
                // 打印参数类型
                outs() << *(it->getType());
                if (it != ie - 1) {
                    outs() << ", ";
                }
            }
            // F.size()  // num of basic blocks
            outs() << ")\n";
        }
    }
}
