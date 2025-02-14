//===-- NativeSandbox.cpp -------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/NativeSandbox.h"
#include "llvm/Support/CommandLine.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Module.h"


using namespace llvm;

cl::opt<bool> ThreadWatchDog ("sandbox-thread-watchdog", cl::desc("Enable native sandbox thread watchdog"), cl::init(false));

static bool isIgnoredForPolling(Module &M) {
    return !ThreadWatchDog || M.getName() == "ldso/dynlink.c" ||
        M.getName() == "src/env/__init_tls.c";
}

static bool isIgnoredForPolling(Function &F) {
    return !ThreadWatchDog;
}

cl::opt<std::string> TraceNativeSandbox ("trace-native-sandbox", cl::desc("Enable native sandbox pass tracing to stdout"), cl::init(""));

static bool isTraceNativeSandbox(Function &F) {
    if (!TraceNativeSandbox.empty()) {
        StringRef predicate = TraceNativeSandbox;
        return predicate == "*" || predicate.starts_with(F.getName());
    }
    return false;
}

PreservedAnalyses NativeSandboxPass::run(Module &M,
        ModuleAnalysisManager &AM) {

    Type* Int8Type = IntegerType::getInt8Ty(M.getContext());
    Type* VoidPtrType = PointerType::getUnqual(Int8Type);

    FunctionCallee sandbox_poll_instr = M.getOrInsertFunction("llvm.sandboxpoll", Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()));
    FunctionCallee sandbox_cfi_instr = M.getOrInsertFunction("llvm.sandboxcfi.p0.p0", VoidPtrType, VoidPtrType);

    // [rust] for cases where (some) functions go through GOT
    // this can end up generating rip relative addressing for indirect calls
    if (M.getRtLibUseGOT()) {
        M.setModuleFlag(llvm::Module::ModFlagBehavior::Max, "RtLibUseGOT", static_cast<Metadata*>(0));
    }

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

static CallInst* insertSandboxCFI(Function &F, IRBuilder<> &Builder, Value *endbrPtr) {
    if (isTraceNativeSandbox(F)) 
        outs() << "[Native] [SandboxPass] :" << F.getName() << "\n";
    Module *M = F.getParent();
    Function *sandbox_cfi_instr = M->getFunction("llvm.sandboxcfi.p0.p0");
    return Builder.CreateCall(sandbox_cfi_instr, { endbrPtr });
}

static bool callNeedsSwcfi(CallBase *CB, Function &F) {
    if (CB->isIndirectCall()) {
        return true;
    }

    if (auto *C = dyn_cast<Constant>(CB->getCalledOperand())) {
        if (C->isManifestConstant()) {
            // direct call to constant absolute address
            // - probably an attempt to manually jump into vsyscall or non-relocatable code
            // - not of much use, but let's still generate valid SWCFI code (note that the purpose of this is just
            //   to satisfy the SWCFI requirements statically - this will fail at runtime - at least for vsyscall)
            F.getContext().diagnose(DiagnosticInfoUnsupported(F, "call to hardcoded address", CB->getDebugLoc(), DS_Warning));
            return true;
        }
        if (auto *C2 = dyn_cast<Function>(CB->getCalledOperand())) {
            // Calls to non-lazily bound external functions need SW-CFI too, as they are rendered as calls, where
            // the target address is read from a RIP pointer (usually read from GOT)
            return C2->getAttributes().hasFnAttr(llvm::Attribute::NonLazyBind);
        } 
        return false;
    }
    
    return false;
}

PreservedAnalyses NativeSandboxPass::run(Function &F,
        FunctionAnalysisManager &AM) {
    LoadInst *poll_page_addr = NULL;
    bool ignoredForPolling = isIgnoredForPolling(*F.getParent()) || isIgnoredForPolling(F);

    // force add the extern return thunk (rustc does not add it for the main stub function)
    F.addFnAttr(llvm::Attribute::FnRetThunkExtern);

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
                if (callNeedsSwcfi(CB, F)) {
                    IRBuilder<> Builder(CB);

                    CallInst *sandbox_cfi_instr_call = insertSandboxCFI(F, Builder, CB->getCalledOperand());
                    CB->setCalledOperand(sandbox_cfi_instr_call);
                }
            }

            if (auto *SWI = dyn_cast<SwitchInst>(&I)) {
                // [rust] [GR-58158] there is no current way to inject the SandboxCFI for a switch instruction. However, 
                // switch instructions are not turned into jump tables durign lowering if the "-fno-jump-tables" flag 
                // is passed (clang) or "-Zno-jump-tables" (rustc). The rustc experimental flag is not 100% supported yet, 
                // therefore we set the function attribute manually until jump tables are supported in graalos.
                F.addFnAttr("no-jump-tables", "true");
            }

            if (auto *IB = dyn_cast<IndirectBrInst>(&I)) {
                // indirect branch instruction - probably computed goto

                IRBuilder<> Builder(IB);

                CallInst *sandbox_cfi_instr_call = insertSandboxCFI(F, Builder, IB->getAddress());
                IB->setAddress(sandbox_cfi_instr_call);

                if (!ignoredForPolling) {
                    IRBuilder<> Builder(IB);
                    LoadInst *poll_page_content = Builder.CreateLoad(Type::getInt32Ty(F.getContext()), poll_page_addr);
                    insertSandboxPoll(F, Builder, poll_page_content);
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

