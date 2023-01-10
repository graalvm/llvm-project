//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/LegacyPassManager.h"

using namespace llvm;

#define DEBUG_TYPE "hello"

STATISTIC(HelloCounter, "Counts number of functions greeted");

namespace {
  // Hello - The first implementation, without getAnalysisUsage.
  struct Hello : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    Hello() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      ++HelloCounter;
      errs() << "MyHello: ";
      errs().write_escaped(F.getName()) << '\n';
      
      for (BasicBlock &B : F) {
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
        }
      }
      
      return false;
    }
  };
}

char Hello::ID = 0;
static RegisterPass<Hello> X("hello", "Hello World Pass");

static void registerHelloPass(const PassManagerBuilder &,
                                llvm::legacy::PassManagerBase &PM) {
    PM.add(new Hello());
    errs() << "Hello pass registered as a standard pass" << "\n";    
}

static RegisterStandardPasses
    RegisterHelloPass(PassManagerBuilder::EP_OptimizerLast,
                          registerHelloPass);

namespace {
  // Hello2 - The second implementation with getAnalysisUsage implemented.
  struct Hello2 : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    Hello2() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      ++HelloCounter;
      errs() << "MyHello2: ";
      errs().write_escaped(F.getName()) << '\n';
      return false;
    }

    // We don't modify the program, so we preserve all analyses.
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }
  };
}

char Hello2::ID = 0;
static RegisterPass<Hello2>
Y("hello2", "Hello World Pass (with getAnalysisUsage implemented)");

