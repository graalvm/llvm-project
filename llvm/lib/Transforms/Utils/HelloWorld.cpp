//===-- HelloWorld.cpp - Example Transformations --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/HelloWorld.h"
#include "llvm/Support/CommandLine.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"


using namespace llvm;

static cl::opt<bool> SayHello("say-hello", cl::init(false),
        cl::desc("Should I say hello?"));

static bool isIgnoredForPolling(Module &M) {
    return M.getName().equals("ldso/dynlink.c") || 
        M.getName().equals("src/env/__init_tls.c");
}

static bool isIgnoredForPolling(Function &F) {
    return false;
    //return F.getName().equals("init_bootstrap_libc") || 
    //        F.getName().equals("finish_init_bootstrap_libc");
}

PreservedAnalyses HelloWorldPass::run(Module &M,
        ModuleAnalysisManager &AM) {

    FunctionCallee sandbox_poll_instr = M.getOrInsertFunction("llvm.sandboxpoll", Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()));
    FunctionCallee sandbox_cfi_instr = M.getOrInsertFunction("llvm.sandboxcfi", Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()));
    
    if (!isIgnoredForPolling(M)) {
        errs() << "MyHello module: ";
        errs().write_escaped(M.getName()) << '\n';

        IRBuilder<> Builder(M.getContext());

        Type* Int8Type = IntegerType::getInt8Ty(M.getContext());
        Type* VoidPtrType = PointerType::getUnqual(Int8Type);
        M.getOrInsertGlobal("poll_page", VoidPtrType);
        GlobalVariable *poll_page_global = M.getNamedGlobal("poll_page");
        poll_page_global->setLinkage(GlobalValue::ExternalLinkage);
        poll_page_global->setThreadLocalMode(GlobalValue::GeneralDynamicTLSModel);
        poll_page_global->setAlignment(Align(8));

        //; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
        //declare nonnull ptr @llvm.threadlocal.address.p0(ptr nonnull) #2
        FunctionCallee tl_addr_instr = M.getOrInsertFunction("llvm.threadlocal.address.p0", VoidPtrType, VoidPtrType);
   } else {
        errs() << "Ignored MyHello module: ";
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

static void insertSandboxCFI(Function &F, IRBuilder<> &Builder, LoadInst *endbr_content) {
    Module *M = F.getParent();
    Function *sandbox_cfi_instr = M->getFunction("llvm.sandboxcfi");
    Builder.CreateCall(sandbox_cfi_instr, endbr_content);
}

static LoadInst *insertENDBR64Comparison(Function &F, IRBuilder<> &Builder, LoadInst *endbr_content) {
    Module *M = F.getParent();
    Type *int32Ty = Type::getInt32Ty(Builder.getContext());
    Type *int64Ty = Type::getInt64Ty(Builder.getContext());
    // %12 = icmp ne i32 %11, -98693133
    CmpInst::Predicate pr = CmpInst::Predicate::ICMP_NE;
    Value *endbr_instr_code = Constant::getIntegerValue(int32Ty, APInt(32, 0xfa1e0ff3));
    Value *cmp = Builder.CreateICmp(pr, endbr_content, endbr_instr_code, "P0");
    // %13 = zext i1 %12 to i32
    Value *zext = Builder.CreateZExtOrTrunc(cmp, int32Ty, "P1");
    // %14 = sext i32 %13 to i64
    Value *sext = Builder.CreateSExt(zext, int64Ty, "P2");
    // %15 = getelementptr inbounds i32, ptr inttoptr (i64 81985529216486895 to ptr)
    Value *c_plchld = Constant::getIntegerValue(int64Ty, APInt(64, 0xFEDCBA8976543210));
    Value *c = Builder.CreateIntToPtr(c_plchld, int64Ty->getPointerTo());
    Value *gep = Builder.CreateGEP(Type::getInt32Ty(Builder.getContext()), c, sext, "P3");
    // %16 = load i32, ptr %15, align 4
    LoadInst *c_load = Builder.CreateLoad(int32Ty, gep, "P4");
    return c_load;
}

PreservedAnalyses HelloWorldPass::run(Function &F,
        FunctionAnalysisManager &AM) {
    errs() << "MyHello function: ";
    errs().write_escaped(F.getName()) << '\n';

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
                //if (CB->getCalledFunction() == targetFunc)
                //  ++callCounter;
                if (CB->isIndirectCall()) {
                    errs() << "Indirect call detected: ";
                    errs() << *CB << ":" << *CB->getCalledOperand() << " function: 	" << CB->getCalledFunction() << '\n';

                    // %P0 = ptrtoint i32 (i32)* %11 to i64
                    // %P1 = and i64 %P0, u0xFEDCBA8976543210
                    // %P2 = add i64 %P1, u0x0123456789ABCDEF
                    // %P3 = inttoptr i64 %P2 to i32 (i32)*
                    // %13 = call i32 %P3(i32 noundef %12)

                    IRBuilder<> Builder(CB);
                    //Value *P0 = Builder.CreatePtrToInt(CB->getCalledOperand(), Type::getInt64Ty(Builder.getContext()), "P0");
                    //errs() << "P0: " << *P0 << "\n";
                    /** Note: the original constant was 0xFEDCBA9876543210, not 0xFEDCBA8976543210, i.e. the reverse of other one. 
                     * But it led to some unwanted optimizations when generating object files in certain situations, as it actually 
                     * was the negative of the other one enabling so some compile-time arithmetics.
                     */ 
                    //Value *P1 = Builder.CreateAnd(P0, Constant::getIntegerValue(Type::getInt64Ty(Builder.getContext()), APInt(64, 0xFEDCBA8976543210)), "P1");
                    //errs() << "P1: " << *P1 << "\n";
                    //Value *P2 = Builder.CreateAdd(P1, Constant::getIntegerValue(Type::getInt64Ty(Builder.getContext()), APInt(64, 0x0123456789ABCDEF)), "P2");
                    //errs() << "P2: " << *P2 << "\n";
                    //Value *P3 = Builder.CreateIntToPtr(P2, PointerType::getUnqual(CB->getFunctionType()), "P3");
                    //errs() << "P3: " << *P3 << "\n";
                    //CB->setCalledOperand(P3);
                    
                    LoadInst *endbr_content = Builder.CreateLoad(Type::getInt32Ty(F.getContext()), CB->getCalledOperand());    
                    insertSandboxCFI(F, Builder, endbr_content);
                    //LoadInst *c_read = insertENDBR64Comparison(F, Builder, endbr_content);
                    //insertSandboxPoll(F, Builder, c_read);
                    //errs() << "Modif. call: " << *CB << ", landing_pad_read=" << *landing_pad_read << "\n";
                }
            }

           if (auto *BR = dyn_cast<BranchInst>(&I)) {
                if (!ignoredForPolling) {
                    //if (!brCnt) {
                    IRBuilder<> Builder(BR);
                    LoadInst *poll_page_content = Builder.CreateLoad(Type::getInt32Ty(F.getContext()), poll_page_addr);
                    insertSandboxPoll(F, Builder, poll_page_content);
                    //}
                    errs() << "Branch: " << brCnt++ << *BR << "\n";
                } else {
                    errs() << "Ignoring branch: " << brCnt++ << *BR << "\n";
                } 
            }
        }
    }      
    return PreservedAnalyses::all();
}
