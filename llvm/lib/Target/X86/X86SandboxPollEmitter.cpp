//===-- X86SandboxPollEmitter.cpp - Native sandbox x86 emitter            --==//
//
//===----------------------------------------------------------------------===//
/// The process of emitting SW-CFI instructions works as folows:
///  1) The emission is triggered by intercepting X86::X86_sandboxcfi pseudo-
///     instruction. The pseudo-instruction indicates that the following 
///     indirect call/jmp instruction must be secured with SW-CFI. Also, it
///     ensures that the indirect call/jmp instructions are register-based only,
///     (i.e. no memory-based) thanks to the pass-thru return value of this 
///     instruction used as the target address of the call/jmp. So instead of
///
///        callq *6(%ebx,%ecx,4)
///
///     there will be something like:
///
///        mov 6(%ebx,%ecx,4), %rax
///        <SW-CFI pattern using %rax as input>
///        callq *%rax
///
///  2) The target address used by the SW-CFI pattern is read from the register 
///     operand of the call/jmp instruction.
///  3) A new block is eventually created for the CFI trap branch shared by
///     all SW-CFI pattern instances in the function.
///  4) ExpectCFIIndirectCall is set to true to switch the loop to the call/jmp
///     instruction intercepting mode, which allows for delaying the emission of
///     the SW-CFI pattern up until the very moment when the indirect call/jmp 
///     is actually encountered.
///
///  Besides the above-mentioned main process path, there is also a path handling
///  the case when there is an unconditional jump between the X86::X86_sandboxcfi
///  pseudoinstruction and the indirect call/jmp. In such a case, the
///  ExpectCFIIndirectCall flag must be temporarily suppressed and resumed later
///  when the jump's target block is reached.
///
///  This pass also takes care of inserting ENDBR64 after all calls, unless the call
///  is a tail one.
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
#include <set>

using namespace llvm;

#define PASS_KEY "x86-sandbox-poll-emitter"
#define DEBUG_TYPE PASS_KEY

enum SandboxModeEnum {
    OFF,
    SWCFI,
    HWCFI,
};

static cl::opt<SandboxModeEnum> SandboxCFIMode("sandbox-cfi-mode",
        cl::desc("Specifies the sandbox CFI mode."),
        cl::values(
            clEnumValN(OFF, "off" ,"No CFI"),
            clEnumValN(SWCFI, "swcfi" ,"Software CFI"),
            clEnumValN(HWCFI, "hwcfi" ,"Hardware CFI")
            ),
        cl::init(SandboxModeEnum::OFF));

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

    if (SandboxCFIMode == SandboxModeEnum::OFF)
        return false;

    const X86Subtarget *Subtarget = &MF.getSubtarget<X86Subtarget>();
    const X86InstrInfo *TII = Subtarget->getInstrInfo();
    const X86RegisterInfo &RI = TII->getRegisterInfo();
    MachineBasicBlock *trapMBB = NULL;

    bool Modified = false;
    bool ExpectCFIIndirectCall = false;
    std::set<int> suppressedICExpectations;

    for (auto &MBB : MF) {

        // Resume ExpectCFIIndirectCall, if any
        std::set<int>::iterator supIt;
        supIt = suppressedICExpectations.find(MBB.getNumber());
        if (supIt != suppressedICExpectations.end()) {
            suppressedICExpectations.erase(supIt);
            ExpectCFIIndirectCall = true;
        }

        for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
            MachineInstr &MI = *MBBI;
            DILocation *DL = MI.getDebugLoc();
            int Opc = MI.getOpcode();

            if (SandboxCFIMode == SandboxModeEnum::SWCFI) {
                if (ExpectCFIIndirectCall && MI.getDesc().isUnconditionalBranch()) {
                    // Suppress temporarily ExpectCFIIndirectCall
                    ExpectCFIIndirectCall = false;
                    suppressedICExpectations.insert(MI.getOperand(0).getMBB()->getNumber());
                } else if (ExpectCFIIndirectCall && (MI.getDesc().isCall() || MI.getDesc().isIndirectBranch())) {
                    ExpectCFIIndirectCall = false;
                    if (MI.getOperand(0).isReg()) {
                        // TODO: Fail if the call instruction is not a register-based
                        //
                        MachineOperand &Base = MI.getOperand(0);

                        bool r11IsBase = Base.getReg().id() == X86::R11;
                        Register TargetReg = r11IsBase ? X86::R12 : X86::R11;

                        if (r11IsBase) {
                            // R12 must be saved as it is not a temporary register as opposed to R11
                            BuildMI(MBB, MBBI, DL, TII->get(X86::PUSH64r)).addReg(X86::R12);
                        }

                        BuildMI(MBB, MI, DL, TII->get(X86::MOV64rr)).addReg(TargetReg).addReg(Base.getReg());
                        BuildMI(MBB, MI, DL, TII->get(X86::MOV32rm), TargetReg)
                            .addReg(TargetReg)     // Base
                            .addImm(32)            // Scale
                            .addReg(0)             // Index
                            .addImm(0)             // Displacement
                            .addReg(0)             // Segment
                            ;

                        BuildMI(MBB, MBBI, DL, TII->get(X86::ADD32ri), TargetReg)
                            .addReg(TargetReg)
                            .addImm(0x05e1f00d); // -ENDBR64

                        if (r11IsBase) {
                            // Restore R12
                            // POP affects no flags, so the JE should work
                            BuildMI(MBB, MBBI, DL, TII->get(X86::POP64r)).addReg(X86::R12);
                        }

                        BuildMI(MBB, MBBI, DL, TII->get(X86::JCC_1)).addMBB(trapMBB).addImm(X86::COND_NE);

                        ExpectCFIIndirectCall = false;
                    }
                }
        
                // Do not insert ENDBR64 after tail calls/jumps
                if (MI.getDesc().isCall() && !(Opc >= X86::TAILJMPd && Opc <= X86::TAILJMPr64_REX)) {
                    auto EI = MachineBasicBlock::iterator(MBBI);
                    EI++;
                    BuildMI(MBB, EI, DL, TII->get(X86::ENDBR64));
                }
            }

            if (MI.getOpcode() != X86::X86_sandboxpoll && MI.getOpcode() != X86::X86_sandboxcfi)
                continue;

            switch (Opc) {
                case X86::X86_sandboxcfi: {
                    
                    // This pseudo-instruction serves to
                    //   * indicate that the following indirect call/jmp instruction must be secured with SW-CFI
                    //   * ensure that only register-based indirect call/jmp instructions are used (i.e. no memory-based) thanks to
                    //   the pass-thru return value of this instruction used as the target address of the call/jmp.

                    ExpectCFIIndirectCall = true; 

                    // Create lazily a block for the CFI trap branch shared by all SW-CFI pattern instances
                    // in the function
                    if (!trapMBB) {
                        trapMBB = MF.CreateMachineBasicBlock();
                        trapMBB->setIsEHPad(true); // This prevents from getting "Undefined temporary symbol .LBB" 
                        // error when compiling  no-return functions
                        BuildMI(trapMBB, DL, TII->get(X86::INT3));
                        MF.push_back(trapMBB);
                    }

                    MBB.addSuccessor(trapMBB);

                    // Copy the target address register to the return register if the two differ
                    Register TargetPtrReg = MI.getOperand(1).getReg();
                    Register ReturnReg = MI.getOperand(0).getReg();
                    if (TargetPtrReg != ReturnReg) {
                        BuildMI(MBB, MI, DL, TII->get(X86::MOV64rr), ReturnReg).addReg(TargetPtrReg);
                    }

                    Modified = true;
                    break;
                }
                case X86::X86_sandboxpoll: {
                    // The polling mechanism is not used yet in GraalOS, so this branch should not be reached
                    BuildMI(MBB, MI, DL, TII->get(X86::NOOP)).addRegMask(RI.getNoPreservedMask());
                    Modified = true;
                    break;
                }
                default:
                    break;
            }
        }
    }

    return Modified;
}

INITIALIZE_PASS(X86SandboxPollEmitterPass, PASS_KEY,
        "X86 Sandbox Poll Instructions Emitter", false, false)

FunctionPass *llvm::createX86SandboxPollEmitterPass() {
    return new X86SandboxPollEmitterPass();
}

