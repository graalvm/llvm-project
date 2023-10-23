//===-- NativeSandbox.cpp -------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/NativeSandbox.h"
#include "llvm/Support/CommandLine.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"


using namespace llvm;

cl::opt<bool> ThreadWatchDog ("sandbox-thread-watchdog", cl::desc("Enable native sandbox thread watchdog"), cl::init(false));

static bool isIgnoredForPolling(Module &M) {
    return !ThreadWatchDog || M.getName().equals("ldso/dynlink.c") || 
        M.getName().equals("src/env/__init_tls.c");
}

static bool isIgnoredForPolling(Function &F) {
    return !ThreadWatchDog;
}

PreservedAnalyses NativeSandboxPass::run(Module &M,
        ModuleAnalysisManager &AM) {

    Type* Int8Type = IntegerType::getInt8Ty(M.getContext());
    Type* VoidPtrType = PointerType::getUnqual(Int8Type);

    FunctionCallee sandbox_poll_instr = M.getOrInsertFunction("llvm.sandboxpoll", Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()));
    FunctionCallee sandbox_cfi_instr = M.getOrInsertFunction("llvm.sandboxcfi", VoidPtrType, Type::getInt32Ty(M.getContext()), VoidPtrType);
    
    if (!isIgnoredForPolling(M)) {
        IRBuilder<> Builder(M.getContext());

        M.getOrInsertGlobal("poll_page", VoidPtrType);
        GlobalVariable *poll_page_global = M.getNamedGlobal("poll_page");
        poll_page_global->setLinkage(GlobalValue::ExternalLinkage);
        poll_page_global->setThreadLocalMode(GlobalValue::GeneralDynamicTLSModel);
        poll_page_global->setAlignment(Align(8));

        //; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
        //declare nonnull ptr @llvm.threadlocal.address.p0(ptr nonnull) #2
        FunctionCallee tl_addr_instr = M.getOrInsertFunction("llvm.threadlocal.address.p0", VoidPtrType, VoidPtrType);
   } else {
        errs().write_escaped(M.getName()) << '\n';
    }

    return PreservedAnalyses::all();
} 

static LoadInst * loadPollPageAddr(Function &F, IRBuilder<> &poll_builder) {
    Module *M = F.getParent();
    GlobalVariable *poll_page_global = M->getNamedGlobal("poll_page");
    // %1 = call ptr @llvm.threadlocal.address.p0(ptr @tl)
    Function *tl_addr_instr = M->getFunction("llvm.threadlocal.address.p0");
    CallInst *poll_page_tl = poll_builder.CreateCall(tl_addr_instr, poll_page_global);
    //%2 = load ptr, ptr %1, align 8
    Type *int32Ty = Type::getInt32Ty(F.getContext());
    LoadInst *poll_page_addr = poll_builder.CreateLoad(int32Ty->getPointerTo(), poll_page_tl);
    return poll_page_addr;
}

static void insertSandboxPoll(Function &F, IRBuilder<> &Builder, LoadInst *poll_page_content) {
    Module *M = F.getParent();
    Function *sandbox_poll_instr = M->getFunction("llvm.sandboxpoll");
    Builder.CreateCall(sandbox_poll_instr, poll_page_content);
}

static CallInst* insertSandboxCFI(Function &F, IRBuilder<> &Builder, LoadInst *endbr_content, Value *endbrPtr) {
    Module *M = F.getParent();
    Function *sandbox_cfi_instr = M->getFunction("llvm.sandboxcfi");
    return Builder.CreateCall(sandbox_cfi_instr, { endbr_content, endbrPtr });
}

PreservedAnalyses NativeSandboxPass::run(Function &F,
        FunctionAnalysisManager &AM) {
    LoadInst *poll_page_addr = NULL;
    int brCnt = 0;
    bool ignoredForPolling = isIgnoredForPolling(*F.getParent()) || isIgnoredForPolling(F);

    for (BasicBlock &B : F) {

        if (!ignoredForPolling && !poll_page_addr) {
            IRBuilder<> poll_builder(&B);
            poll_builder.SetInsertPoint(&B, B.begin());
            poll_page_addr = loadPollPageAddr(F, poll_builder);
        } 

        for (Instruction &I: B) {
            if (auto *CB = dyn_cast<CallBase>(&I)) {
                // We know we've encountered some kind of call instruction (call,
                // invoke, or callbr), so we need to determine if it's a call to
                // the function pointed to by m_func or not.
                if (CB->isIndirectCall()) {
                    IRBuilder<> Builder(CB);
                   
                    LoadInst *endbr_content = Builder.CreateLoad(Type::getInt32Ty(F.getContext()), CB->getCalledOperand());    
                    CallInst *sandbox_cfi_instr_call = insertSandboxCFI(F, Builder, endbr_content, CB->getCalledOperand());
                    CB->setCalledOperand(sandbox_cfi_instr_call);
                }
            }

           if (auto *BR = dyn_cast<BranchInst>(&I)) {
                if (!ignoredForPolling) {
                    IRBuilder<> Builder(BR);
                    LoadInst *poll_page_content = Builder.CreateLoad(Type::getInt32Ty(F.getContext()), poll_page_addr);
                    insertSandboxPoll(F, Builder, poll_page_content);
                } 
            }
        }
    }      
    return PreservedAnalyses::all();
}

