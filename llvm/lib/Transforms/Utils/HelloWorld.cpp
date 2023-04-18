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

PreservedAnalyses HelloWorldPass::run(Module &M,
                                      ModuleAnalysisManager &AM) {
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
    FunctionCallee tl_addr_intr = M.getOrInsertFunction("llvm.threadlocal.address.p0", VoidPtrType, VoidPtrType);
    FunctionCallee sandbox_poll_intr = M.getOrInsertFunction("llvm.sandboxpoll", Type::getVoidTy(M.getContext()), Type::getInt8Ty(M.getContext()));

    return PreservedAnalyses::all();
} 

static LoadInst * loadPollPageAddr(Function &F, IRBuilder<> &poll_builder) {
        Module *M = F.getParent();
        GlobalVariable *poll_page_global = M->getNamedGlobal("poll_page");
// %1 = call ptr @llvm.threadlocal.address.p0(ptr @tl)
        Function *tl_addr_intr = M->getFunction("llvm.threadlocal.address.p0");
        CallInst *poll_page_tl = poll_builder.CreateCall(tl_addr_intr, poll_page_global);
//%2 = load ptr, ptr %1, align 8
        Type *int8Ty = Type::getInt8Ty(F.getContext());
        LoadInst *poll_page_addr = poll_builder.CreateLoad(int8Ty->getPointerTo(), poll_page_tl);
        return poll_page_addr;
}


static void insertSandboxPoll(Function &F, IRBuilder<> &poll_builder, LoadInst *poll_page_content) {
        Module *M = F.getParent();
        Function *sandbox_poll_intr = M->getFunction("llvm.sandboxpoll");
        poll_builder.CreateCall(sandbox_poll_intr, poll_page_content);
}

PreservedAnalyses HelloWorldPass::run(Function &F,
                                      FunctionAnalysisManager &AM) {
      errs() << "MyHello function: ";
      errs().write_escaped(F.getName()) << '\n';

      LoadInst *poll_page_addr = NULL;
      int brCnt = 0;

      for (BasicBlock &B : F) {

        if (!poll_page_addr) {
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
  		        Value *P0 = Builder.CreatePtrToInt(CB->getCalledOperand(), Type::getInt64Ty(Builder.getContext()), "P0");
			    errs() << "P0: " << *P0 << "\n";
                        /** Note: the original constant was 0xFEDCBA9876543210, not 0xFEDCBA8976543210, i.e. the reverse of other one. 
                         * But it led to some unwanted optimizations when generating object files in certain situations, as it actually 
                         * was the negative of the other one enabling so some compile-time arithmetics.
                        */ 
  		        Value *P1 = Builder.CreateAnd(P0, Constant::getIntegerValue(Type::getInt64Ty(Builder.getContext()), APInt(64, 0xFEDCBA8976543210)), "P1");
			    errs() << "P1: " << *P1 << "\n";
  		        Value *P2 = Builder.CreateAdd(P1, Constant::getIntegerValue(Type::getInt64Ty(Builder.getContext()), APInt(64, 0x0123456789ABCDEF)), "P2");
			    errs() << "P2: " << *P2 << "\n";
  		        Value *P3 = Builder.CreateIntToPtr(P2, PointerType::getUnqual(CB->getFunctionType()), "P3");
			    errs() << "P3: " << *P3 << "\n";
			    CB->setCalledOperand(P3);
			    errs() << "Modif. call: " << *CB << "\n";

            }
          }
          
          if (auto *BR = dyn_cast<BranchInst>(&I)) {
              //if (!brCnt) {
                 IRBuilder<> Builder(BR);
                 LoadInst *poll_page_content = Builder.CreateLoad(Type::getInt8Ty(F.getContext()), poll_page_addr);
                 insertSandboxPoll(F, Builder, poll_page_content);
              //}
              errs() << "Branch: " << brCnt++ << *BR << "\n";
          }
        }
      }
      
    return PreservedAnalyses::all();
}
