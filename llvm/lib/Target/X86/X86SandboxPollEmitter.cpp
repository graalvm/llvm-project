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

    SandboxModeEnum sandboxMode = SandboxCFIMode;
    printf("X86SandboxPollEmitter: sandbox mode=%d\n", sandboxMode);

    const X86Subtarget *Subtarget = &MF.getSubtarget<X86Subtarget>();
    //const X86RegisterInfo *TRI = Subtarget->getRegisterInfo();
    const X86InstrInfo *TII = Subtarget->getInstrInfo();
    const X86RegisterInfo &RI = TII->getRegisterInfo();
    MachineBasicBlock *trapMBB = NULL;

    bool Modified = false;
    for (auto &MBB : MF) {
        for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
            MachineInstr &MI = *MBBI;
            DILocation *DL = MI.getDebugLoc();

            if (SandboxCFIMode == SandboxModeEnum::SWCFI && MI.getDesc().isCall()) {
                //printf("X86SandboxPollEmitter: X86::CALL64r\n");
                auto EI = MachineBasicBlock::iterator(MBBI);
                EI++;
                BuildMI(MBB, EI, DL, TII->get(X86::ENDBR64));
            }

            if (MI.getOpcode() != X86::X86_sandboxpoll && MI.getOpcode() != X86::X86_sandboxcfi)
                continue;

            switch (MI.getOpcode()) {
                case X86::X86_sandboxcfi: {
                    //printf("X86SandboxPollEmitter: X86_sandboxcfi ON\n");
                    
                    if (!trapMBB) {
                        trapMBB = MF.CreateMachineBasicBlock();
                        trapMBB->setIsEHPad(true); // prevents from getting "Undefined temporary symbol .LBB" 
                                               // error when compiling  no-return functions
                        //BuildMI(trapMBB, DL, TII->get(X86::NOOP)).addRegMask(RI.getNoPreservedMask());
                        BuildMI(trapMBB, DL, TII->get(X86::INT3));
                        MF.push_back(trapMBB);
                    }

                    MBB.addSuccessor(trapMBB);
                    //MF.push_back(trapMBB);

                    Register TargetReg = MI.getOperand(0).getReg(); 
                    BuildMI(MBB, MI, DL, TII->get(X86::ADD32ri), TargetReg)
                        .addReg(TargetReg)
                        .addImm(0x05e1f00d); // -ENDBR64
                    // X86::LFENCE
                    BuildMI(MBB, MI, DL, TII->get(X86::LFENCE));
                    BuildMI(MBB, MI, DL, TII->get(X86::JCC_1)).addMBB(trapMBB).addImm(X86::COND_NE);

                                              //Register TargetReg = MI.getOperand(0).getReg();
                                              //auto CheckI = BuildMI(MBB, MI, DL, TII->get(X86::CMP32ri))
                                              //    .addReg(TargetReg, RegState::Kill)
                                              //    .addImm(0x12345678);
                                              //BuildMI(MBB, MI, DL, TII->get(X86::JCC_1)).addImm(16).addImm(X86::COND_E);
                                              //BuildMI(MBB, MI, DL, TII->get(X86::INT3));

                    Modified = true;
                    break;
                }
                case X86::X86_sandboxpoll: {
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
