//===-- X86SandboxPass.cpp - Native sandbox x86 emitter                   --==//
//
//===----------------------------------------------------------------------===//
/// The process of emitting SW-CFI instructions works as folows:
///  1) The X86::X86_sandboxcfi pseudo-instruction indicates that the following
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
///  2) The SW-CFI pattern is inserted in front of every indirect call/jmp.
///     The target address used by the SW-CFI pattern is read from the register
///     operand of the call/jmp instruction.
///  3) A new block is eventually created for the CFI trap branch shared by
///     all SW-CFI pattern instances in the function.
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
#include "llvm/CodeGen/ReachingDefAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"

#define VERIFY_SANDBOX

using namespace llvm;

#define PASS_KEY "x86-sandbox-pass"
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

bool isSandboxCFI() {
	return SandboxCFIMode == SandboxModeEnum::HWCFI || SandboxCFIMode == SandboxModeEnum::SWCFI;
}

bool isSandboxHWCFI() {
	return SandboxCFIMode == SandboxModeEnum::HWCFI;
}

bool isSandboxSWCFI() {
	return SandboxCFIMode == SandboxModeEnum::SWCFI;
}

namespace {

    class X86SandboxPass : public MachineFunctionPass {
        public:
            X86SandboxPass() : MachineFunctionPass(ID) {}
            StringRef getPassName() const override {
                return "X86 Sandbox Instructions Emitter";
            }
#ifdef VERIFY_SANDBOX
            void getAnalysisUsage(AnalysisUsage &AU) const override {
                AU.addRequired<ReachingDefAnalysis>();
                MachineFunctionPass::getAnalysisUsage(AU);
            }
#endif
            bool runOnMachineFunction(MachineFunction &MF) override;

            static char ID;
    };

} // end anonymous namespace

char X86SandboxPass::ID = 0;

static bool isIndirectCallOrBranch(MachineInstr &MI) {
    if (MI.getDesc().isCall()) {
        auto &&op = MI.getOperand(0);
        // The operand of an indirect call can only be the result of a X86_sandboxcfi,
        // and X86_sandboxcfi can only output to a register.
        // Therefore, all indirect calls/jumps should be register-based.
        if (op.isReg())
            return true;
        else if (op.isGlobal() || op.isSymbol() || op.isMCSymbol())
            return false;
        else
            errs() << "unexpected call target operand type " << op << '\n', std::abort();
    }
    if (MI.getDesc().isIndirectBranch()) {
        auto &&op = MI.getOperand(0);
        if (!op.isReg())
            errs() << "unexpected branch target operand type " << op << '\n', std::abort();
        return true;
    }
    return false;
}

#ifdef VERIFY_SANDBOX
// The result of an X86_sandboxcfi instruction could have been spilled and reloaded before reaching the call/jump.
// In such cases, ReachingDefAnalysis does not track the value any further and marks the reload as the definition.
// There seems to be no way to detect a spill reload 100% reliably, but a rough approximation should be enough for
// the purpose of an assertion. If the instruction looks like a spill reload, assume it is and pass.
static bool mayBeSpilledAddrReload(MachineInstr &MI) {
    if (MI.getOpcode() != X86::MOV64rm) // 64-bit mov from memory to register
        return false;
    auto mo = MI.memoperands();
    if (mo.size() != 1) // should be implied by the opcode but make sure
        return false;
    // if the memory operand was created for InlineSpiller::insertReload, it is a FixedStackPseudoSourceValue
    const PseudoSourceValue *PVal = mo[0]->getPseudoValue();
    return PVal && PVal->kind() == PseudoSourceValue::FixedStack;
}
#endif

bool X86SandboxPass::runOnMachineFunction(
        MachineFunction &MF) {
    LLVM_DEBUG(dbgs() << "***** " << getPassName() << " : " << MF.getName()
            << " *****\n");

    if (SandboxCFIMode == SandboxModeEnum::OFF)
        return false;

    const X86Subtarget *Subtarget = &MF.getSubtarget<X86Subtarget>();
    const X86InstrInfo *TII = Subtarget->getInstrInfo();
    const X86RegisterInfo &RI = TII->getRegisterInfo();
    MachineBasicBlock *trapMBB = NULL;

#ifdef VERIFY_SANDBOX
    auto &RDA = getAnalysis<ReachingDefAnalysis>();
#endif

    bool Modified = false;

    for (auto &MBB : MF) {

        for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
            MachineInstr &MI = *MBBI;
            DILocation *DL = MI.getDebugLoc();
            int Opc = MI.getOpcode();

            if (SandboxCFIMode == SandboxModeEnum::SWCFI) {

                if (isIndirectCallOrBranch(MI)) {
                    MachineOperand &Base = MI.getOperand(0);

#ifdef VERIFY_SANDBOX
                    SmallPtrSet<MachineInstr *, 1> defMIs;
                    RDA.getGlobalReachingDefs(&MI, Base.getReg().asMCReg(), defMIs);

                    for (auto *defMI : defMIs) {
                        if (defMI->getOpcode() != X86::X86_sandboxcfi && !mayBeSpilledAddrReload(*defMI)) {
                            errs() << "indirect call/jump instruction uses value not passed through X86_sandboxcfi\n";
                            std::abort();
                        }
                    }
#endif

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
                    BuildMI(MBB, MBBI, DL, TII->get(X86::JCC_1)).addMBB(trapMBB).addImm(X86::COND_NE);

                    Modified = true;
                }

                // Do not insert ENDBR64 after tail calls/jumps
                if (MI.getDesc().isCall() && !(Opc >= X86::TAILJMPd && Opc <= X86::TAILJMPr64_REX)) {
                    auto EI = MachineBasicBlock::iterator(MBBI);
                    EI++;
                    BuildMI(MBB, EI, DL, TII->get(X86::ENDBR64));

                    Modified = true;
                }
            }

            switch (Opc) {
                case X86::X86_sandboxcfi: {

                    // This pseudo-instruction serves to ensure that only register-based indirect call/jmp instructions are used (i.e. no memory-based)
                    // thanks to the pass-thru return value of this instruction used as the target address of the call/jmp.

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

INITIALIZE_PASS_BEGIN(X86SandboxPass, DEBUG_TYPE,
        "X86 Sandbox Instructions Emitter", false, false)
#ifdef VERIFY_SANDBOX
    INITIALIZE_PASS_DEPENDENCY(ReachingDefAnalysis);
#endif
INITIALIZE_PASS_END(X86SandboxPass, DEBUG_TYPE,
        "X86 Sandbox Instructions Emitter", false, false)

FunctionPass *llvm::createX86SandboxPass() {
    return new X86SandboxPass();
}
