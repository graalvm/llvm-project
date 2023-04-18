//===-- X86SandboxPollEmitter.cpp - Sandbpox polling instructions emitting for x86 --==//
//
//===----------------------------------------------------------------------===//
///
/// Description: TODO
///
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include <bitset>

using namespace llvm;

#define PASS_KEY "x86-sandbox-poll-emitter"
#define DEBUG_TYPE PASS_KEY

namespace {

class X86SandboxPollEmitterPass : public MachineFunctionPass {
public:
  X86SandboxPollEmitterPass() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override {
    return "X86 Sandbox Poll Instructions Emitter";
  }
  bool runOnMachineFunction(MachineFunction &MF) override;

  static char ID;
};

} // end anonymous namespace

char X86SandboxPollEmitterPass::ID = 0;

bool X86SandboxPollEmitterPass::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "***** " << getPassName() << " : " << MF.getName()
                    << " *****\n");
  const X86Subtarget *Subtarget = &MF.getSubtarget<X86Subtarget>();
  //const X86RegisterInfo *TRI = Subtarget->getRegisterInfo();
  const X86InstrInfo *TII = Subtarget->getInstrInfo();
  const X86RegisterInfo &RI = TII->getRegisterInfo();

  bool Modified = false;
  for (auto &MBB : MF) {
    for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
      if (MBBI->getOpcode() != X86::X86_sandboxpoll)
        continue;
      
      MachineInstr &MI = *MBBI;
      DILocation *DL = MI.getDebugLoc();
      BuildMI(MBB, MI, DL, TII->get(X86::NOOP)).addRegMask(RI.getNoPreservedMask());

      Modified = true;
      break;
    }
  }

  return Modified;
}

INITIALIZE_PASS(X86SandboxPollEmitterPass, PASS_KEY,
                "X86 Sandbox Poll Instructions Emitter", false, false)

FunctionPass *llvm::createX86SandboxPollEmitterPass() {
  return new X86SandboxPollEmitterPass();
}
