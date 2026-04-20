#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/CallGraph.h"
#include <map>
#include <string>
#include <vector>

namespace llvm {

// 单函数的结构化特征 + 复杂度度量
struct FunctionMetrics {
  std::string Name;

  // 结构化特征
  unsigned InstructionCount  = 0;
  unsigned BasicBlockCount   = 0;
  unsigned BranchCount       = 0;   // 条件分支数（BranchInst）
  unsigned CallCount         = 0;   // 函数调用次数
  unsigned MaxLoopDepth      = 0;   // 最深循环嵌套层
  unsigned MaxNestingDepth   = 0;   // 最深基本块控制流嵌套
  unsigned PhiNodeCount      = 0;
  unsigned MemoryOpCount     = 0;   // load + store
  unsigned ArithmeticOpCount = 0;
  unsigned CompareOpCount    = 0;
  unsigned CastOpCount       = 0;

  // 被调用的函数列表（直接调用）
  std::vector<std::string> Callees;

  // 指令按 opcode 分布：opcode_name -> count
  std::map<std::string, unsigned> InstrDist;

  // 传统复杂度度量
  // McCabe 圈复杂度：V(G) = E - N + 2P
  //   E = 边数，N = 节点数，P = 连通分量（函数=1）
  unsigned CyclomaticComplexity = 0;

  // Halstead 度量
  unsigned DistinctOperators = 0;   // η1
  unsigned DistinctOperands  = 0;   // η2
  unsigned TotalOperators    = 0;   // N1
  unsigned TotalOperands     = 0;   // N2
  double   HalsteadVolume    = 0.0; // V = N * log2(η)
  double   HalsteadDifficulty= 0.0; // D = (η1/2) * (N2/η2)
  double   HalsteadEffort    = 0.0; // E = D * V

  // 综合复杂度评分（0-100，越高越复杂）
  double CompositeScore = 0.0;
};

// 模块级汇总
struct ModuleMetrics {
  std::string ModuleName;
  std::vector<FunctionMetrics> Functions;

  // 调用图：caller -> [callee, callee, ...]
  std::map<std::string, std::vector<std::string>> CallGraph;

  double AverageCompositeScore = 0.0;
  double MaxCompositeScore     = 0.0;
  std::string MostComplexFunction;
};

// New Pass Manager 版本（LLVM 20 推荐）
class ComplexityAnalysisPass : public PassInfoMixin<ComplexityAnalysisPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

  // 允许跳过不需要分析的函数（如声明）
  static bool isRequired() { return false; }

private:
  FunctionMetrics analyzeFunction(Function &F, LoopInfo &LI);
  void computeHalstead(Function &F, FunctionMetrics &FM);
  unsigned computeMaxLoopDepth(Loop *L);
  unsigned computeMaxNestingDepth(Function &F);
  double computeCompositeScore(const FunctionMetrics &FM);
  void printJSON(const ModuleMetrics &MM, raw_ostream &OS);
};

} // namespace llvm
