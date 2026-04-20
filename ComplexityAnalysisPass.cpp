#include "ComplexityAnalysisPass.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

#include <cmath>
#include <queue>
#include <set>
#include <algorithm>
#include <numeric>

using namespace llvm;

// 辅助：递归计算最深循环嵌套深度
unsigned ComplexityAnalysisPass::computeMaxLoopDepth(Loop *L) {
  unsigned MaxDepth = L->getLoopDepth();
  for (Loop *SubLoop : L->getSubLoops())
    MaxDepth = std::max(MaxDepth, computeMaxLoopDepth(SubLoop));
  return MaxDepth;
}

// 辅助：DFS计算CFG最深嵌套（支配树深度近似）
unsigned ComplexityAnalysisPass::computeMaxNestingDepth(Function &F) {
  if (F.empty()) return 0;

  // 用BFS深度作为近似嵌套层次
  std::map<BasicBlock *, unsigned> Depth;
  std::queue<BasicBlock *> Queue;
  BasicBlock *Entry = &F.getEntryBlock();
  Depth[Entry] = 1;
  Queue.push(Entry);
  unsigned MaxDepth = 1;

  while (!Queue.empty()) {
    BasicBlock *BB = Queue.front();
    Queue.pop();
    for (BasicBlock *Succ : successors(BB)) {
      if (Depth.find(Succ) == Depth.end()) {
        Depth[Succ] = Depth[BB] + 1;
        MaxDepth = std::max(MaxDepth, Depth[Succ]);
        Queue.push(Succ);
      }
    }
  }
  return MaxDepth;
}

// Halstead 度量计算
//   操作符 = 指令操作码
//   操作数 = 使用的操作数（Value）
void ComplexityAnalysisPass::computeHalstead(Function &F, FunctionMetrics &FM) {
  std::map<unsigned, unsigned> OpCount;     // opcode -> total uses
  std::map<Value *, unsigned> OperandCount; // value -> total uses

  for (auto &BB : F) {
    for (auto &I : BB) {
      unsigned OC = I.getOpcode();
      OpCount[OC]++;
      FM.TotalOperators++;

      for (Use &U : I.operands()) {
        Value *V = U.get();
        OperandCount[V]++;
        FM.TotalOperands++;
      }
    }
  }

  FM.DistinctOperators = (unsigned)OpCount.size();
  FM.DistinctOperands  = (unsigned)OperandCount.size();

  unsigned eta = FM.DistinctOperators + FM.DistinctOperands;
  unsigned N   = FM.TotalOperators    + FM.TotalOperands;

  if (eta > 1 && N > 0) {
    FM.HalsteadVolume     = N * std::log2((double)eta);
    FM.HalsteadDifficulty = FM.DistinctOperands > 0
        ? (FM.DistinctOperators / 2.0) * ((double)FM.TotalOperands / FM.DistinctOperands)
        : 0.0;
    FM.HalsteadEffort = FM.HalsteadDifficulty * FM.HalsteadVolume;
  }
}

// 综合复杂度评分模型（0~100，加权归一化）
// 权重设计（可按需调整）：
//   - McCabe 圈复杂度         35%
//   - 循环深度                20%
//   - Halstead Effort         20%
//   - 分支数 / 指令数         15%
//   - 调用深度 / 嵌套深度     10%
double ComplexityAnalysisPass::computeCompositeScore(const FunctionMetrics &FM) {
  // 各子项满分阈值（超过即为满分）
  constexpr double MaxCyclomatic  = 30.0;
  constexpr double MaxLoopDepth   = 6.0;
  constexpr double MaxHalstead    = 50000.0;
  constexpr double MaxBranchRatio = 0.5;  // branchCount / instrCount
  constexpr double MaxNesting     = 8.0;

  double ScoreCyclomatic = std::min(FM.CyclomaticComplexity / MaxCyclomatic, 1.0) * 35.0;
  double ScoreLoop       = std::min(FM.MaxLoopDepth         / MaxLoopDepth,  1.0) * 20.0;
  double ScoreHalstead   = std::min(FM.HalsteadEffort       / MaxHalstead,   1.0) * 20.0;

  double BranchRatio = FM.InstructionCount > 0
      ? (double)FM.BranchCount / FM.InstructionCount : 0.0;
  double ScoreBranch = std::min(BranchRatio / MaxBranchRatio, 1.0) * 15.0;

  double ScoreNesting = std::min(FM.MaxNestingDepth / MaxNesting, 1.0) * 10.0;

  return ScoreCyclomatic + ScoreLoop + ScoreHalstead + ScoreBranch + ScoreNesting;
}

// 分析单个函数
FunctionMetrics ComplexityAnalysisPass::analyzeFunction(Function &F, LoopInfo &LI) {
  FunctionMetrics FM;
  FM.Name = F.getName().str();

  // 基础统计
  unsigned EdgeCount = 0;

  for (auto &BB : F) {
    FM.BasicBlockCount++;
    // CFG 边数
    EdgeCount += (unsigned)BB.getTerminator()->getNumSuccessors();

    for (auto &I : BB) {
      FM.InstructionCount++;

      unsigned OC = I.getOpcode();
      std::string OpName = I.getOpcodeName();
      FM.InstrDist[OpName]++;

      // 分支
      if (isa<BranchInst>(I) && cast<BranchInst>(I).isConditional())
        FM.BranchCount++;

      // 函数调用
      if (auto *CI = dyn_cast<CallInst>(&I)) {
        FM.CallCount++;
        if (Function *Callee = CI->getCalledFunction())
          if (!Callee->isIntrinsic())
            FM.Callees.push_back(Callee->getName().str());
      }
      if (isa<InvokeInst>(I)) FM.CallCount++;

      // Phi 节点
      if (isa<PHINode>(I)) FM.PhiNodeCount++;

      // 内存操作
      if (isa<LoadInst>(I) || isa<StoreInst>(I)) FM.MemoryOpCount++;

      // 算术
      if (I.isBinaryOp()) FM.ArithmeticOpCount++;

      // 比较
      if (isa<CmpInst>(I)) FM.CompareOpCount++;

      // 类型转换
      if (isa<CastInst>(I)) FM.CastOpCount++;
    }
  }

  // McCabe 圈复杂度 V(G) = E - N + 2 
  FM.CyclomaticComplexity = EdgeCount >= FM.BasicBlockCount
      ? EdgeCount - FM.BasicBlockCount + 2
      : 1;

  // 循环深度
  FM.MaxLoopDepth = 0;
  for (Loop *L : LI)
    FM.MaxLoopDepth = std::max(FM.MaxLoopDepth, computeMaxLoopDepth(L));

  // 嵌套深度
  FM.MaxNestingDepth = computeMaxNestingDepth(F);

  // Halstead
  computeHalstead(F, FM);

  // 综合评分
  FM.CompositeScore = computeCompositeScore(FM);

  // 去重Callees
  std::sort(FM.Callees.begin(), FM.Callees.end());
  FM.Callees.erase(std::unique(FM.Callees.begin(), FM.Callees.end()), FM.Callees.end());

  return FM;
}

// JSON 输出
static std::string escapeJSON(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

void ComplexityAnalysisPass::printJSON(const ModuleMetrics &MM, raw_ostream &OS) {
  OS << "{\n";
  OS << "  \"模块\": \"" << escapeJSON(MM.ModuleName) << "\",\n";
  OS << "  \"平均综合评分\": " << MM.AverageCompositeScore << ",\n";
  OS << "  \"最高综合评分\": " << MM.MaxCompositeScore << ",\n";
  OS << "  \"最复杂函数\": \"" << escapeJSON(MM.MostComplexFunction) << "\",\n";

  OS << "  \"函数调用图\": {\n";
  bool firstCG = true;
  for (auto &[Caller, Callees] : MM.CallGraph) {
    if (!firstCG) OS << ",\n";
    firstCG = false;
    OS << "    \"" << escapeJSON(Caller) << "\": [";
    for (size_t i = 0; i < Callees.size(); i++) {
      if (i) OS << ", ";
      OS << "\"" << escapeJSON(Callees[i]) << "\"";
    }
    OS << "]";
  }
  OS << "\n  },\n";

  OS << "  \"函数列表\": [\n";
  bool firstFn = true;
  for (auto &FM : MM.Functions) {
    if (!firstFn) OS << ",\n";
    firstFn = false;

    OS << "    {\n";
    OS << "      \"函数名\": \""        << escapeJSON(FM.Name) << "\",\n";
    OS << "      \"综合评分\": "        << FM.CompositeScore << ",\n";
    OS << "      \"圈复杂度\": "        << FM.CyclomaticComplexity << ",\n";
    OS << "      \"指令总数\": "        << FM.InstructionCount << ",\n";
    OS << "      \"基本块数\": "        << FM.BasicBlockCount << ",\n";
    OS << "      \"条件分支数\": "      << FM.BranchCount << ",\n";
    OS << "      \"函数调用数\": "      << FM.CallCount << ",\n";
    OS << "      \"最大循环深度\": "    << FM.MaxLoopDepth << ",\n";
    OS << "      \"最大嵌套深度\": "    << FM.MaxNestingDepth << ",\n";
    OS << "      \"Phi节点数\": "       << FM.PhiNodeCount << ",\n";
    OS << "      \"内存操作数\": "      << FM.MemoryOpCount << ",\n";
    OS << "      \"算术操作数\": "      << FM.ArithmeticOpCount << ",\n";
    OS << "      \"比较操作数\": "      << FM.CompareOpCount << ",\n";
    OS << "      \"类型转换数\": "      << FM.CastOpCount << ",\n";
    OS << "      \"Halstead体积\": "    << FM.HalsteadVolume << ",\n";
    OS << "      \"Halstead难度\": "    << FM.HalsteadDifficulty << ",\n";
    OS << "      \"Halstead工作量\": "  << FM.HalsteadEffort << ",\n";

    // 被调用函数列表
    OS << "      \"被调用函数\": [";
    for (size_t i = 0; i < FM.Callees.size(); i++) {
      if (i) OS << ", ";
      OS << "\"" << escapeJSON(FM.Callees[i]) << "\"";
    }
    OS << "],\n";

    // 指令分布（Top-10 按频次排序）
    std::vector<std::pair<unsigned, std::string>> Sorted;
    for (auto &[op, cnt] : FM.InstrDist) Sorted.push_back({cnt, op});
    std::sort(Sorted.rbegin(), Sorted.rend());

    OS << "      \"指令类型分布\": {";
    size_t Limit = std::min(Sorted.size(), (size_t)10);
    for (size_t i = 0; i < Limit; i++) {
      if (i) OS << ", ";
      OS << "\"" << escapeJSON(Sorted[i].second) << "\": " << Sorted[i].first;
    }
    OS << "}\n";

    OS << "    }";
  }
  OS << "\n  ]\n}\n";
}

// Pass 主入口
PreservedAnalyses ComplexityAnalysisPass::run(Module &M, ModuleAnalysisManager &MAM) {
  ModuleMetrics MM;
  MM.ModuleName = M.getName().str();

  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  for (Function &F : M) {
    if (F.isDeclaration() || F.empty()) continue;

    // 获取 LoopInfo
    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);

    FunctionMetrics FM = analyzeFunction(F, LI);
    MM.CallGraph[FM.Name] = FM.Callees;
    MM.Functions.push_back(FM);
  }

  // 计算模块级汇总
  if (!MM.Functions.empty()) {
    double Total = 0.0;
    for (auto &FM : MM.Functions) {
      Total += FM.CompositeScore;
      if (FM.CompositeScore > MM.MaxCompositeScore) {
        MM.MaxCompositeScore = FM.CompositeScore;
        MM.MostComplexFunction = FM.Name;
      }
    }
    MM.AverageCompositeScore = Total / MM.Functions.size();
  }

  // 输出 JSON：默认写到 <module_name>_complexity.json
  std::string OutFile = MM.ModuleName + "_complexity.json";
  // 如果模块名含路径，只取文件名部分
  size_t Slash = OutFile.find_last_of("/\\");
  if (Slash != std::string::npos) OutFile = OutFile.substr(Slash + 1);

  std::error_code EC;
  raw_fd_ostream OS(OutFile, EC, sys::fs::OF_Text);
  if (!EC) {
    printJSON(MM, OS);
    errs() << "[ComplexityAnalysis] 报告已写入: " << OutFile << "\n";
  } else {
    errs() << "[ComplexityAnalysis] 无法写入文件，改用 stderr:\n";
    printJSON(MM, errs());
  }

  // 本 Pass 只读分析，不修改 IR
  return PreservedAnalyses::all();
}

// PassPlugin 注册（LLVM 20 插件接口）
llvm::PassPluginLibraryInfo getComplexityAnalysisPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION,
    "ComplexityAnalysisPass",
    LLVM_VERSION_STRING,
    [](PassBuilder &PB) {
      // 注册为模块级 Pass，可通过 -passes= 调用
      PB.registerPipelineParsingCallback(
          [](StringRef Name, ModulePassManager &MPM,
             ArrayRef<PassBuilder::PipelineElement>) {
            if (Name == "complexity-analysis") {
              MPM.addPass(ComplexityAnalysisPass());
              return true;
            }
            return false;
          });
    }
  };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getComplexityAnalysisPassPluginInfo();
}
