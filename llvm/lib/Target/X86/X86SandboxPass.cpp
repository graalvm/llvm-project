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
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/MC/MCInstrInfo.h"

#define VERIFY_SANDBOX

using namespace llvm;

cl::opt<std::string> TraceX86Sandbox ("trace-x86-sandbox", cl::desc("Enable x86 sandbox pass tracing to stderr"), cl::init(""));

enum X86TraceModeEnum {
    NONE,
    ALL,
    FUNC,
};

static X86TraceModeEnum getTraceX86Sandbox(MachineFunction &F) {
    if (!TraceX86Sandbox.empty()) {
        StringRef Predicate = TraceX86Sandbox;
        if (Predicate == "*") {
          return X86TraceModeEnum::ALL;
        }
        if (F.getName().contains(Predicate)) {
          return X86TraceModeEnum::FUNC;
        }
    }
    return X86TraceModeEnum::NONE;
}

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

            bool doInitialization(Module &M) override {
                bool baseResult = MachineFunctionPass::doInitialization(M);
                // when lto is enabled this pass also runs via a linker invocation (see llvm/lib/LTO/LTOBackend.cpp)
                // in that case, if one forgets to pass the sandbox-cfi-mode option to the linker: -Wl,-mllvm,-sandbox-cfi-mode=swcfi
                // the lto produced code does not benefit from this pass. We initialize the static SandboxCFIMode to
                // the module atribute set in the clang/lib/CodeGen/BackendUtil.cpp. If set, this overrides the value passed to the
                // linker independently. Otherwise the parsed option value is used.
                if (M.getModuleFlag("SandboxModeSWCFI")) {
                    SandboxCFIMode = SandboxModeEnum::SWCFI;
                } else if (M.getModuleFlag("SandboxModeHWCFI")) {
                    SandboxCFIMode = SandboxModeEnum::HWCFI;
                }

                return baseResult;
            }

            static char ID;
    };

} // end anonymous namespace

char X86SandboxPass::ID = 0;

static bool isIndirectJumpTailCall(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  if (Opc == X86::TAILJMPm || Opc == X86::TAILJMPm64 || Opc == X86::TAILJMPm64_REX) {
        errs() << "------------------------------------[ X86SandboxPass ]------------------------------------\n";
        errs() << "One of X86::TAILJMPm, X86::TAILJMPm64, X86::TAILJMPm64_REX unsupported machine instructions encountered, opcode: " << Opc << " ()\n";
        errs() << "------------------------------------------------------------------------------------------\n";
        std::abort();
  }
  return Opc == X86::TAILJMPr || Opc == X86::TAILJMPr64 || Opc == X86::TAILJMPr64_REX;
}

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
    if (MI.getDesc().isIndirectBranch() || isIndirectJumpTailCall(MI)) {
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
    if (MI.getOpcode() != X86::MOV64rm) { // 64-bit mov from memory to register
        return false;
    }
    auto mo = MI.memoperands();
    if (mo.size() != 1) { // should be implied by the opcode but make sure
        return false;
    }
    // if the memory operand was created for InlineSpiller::insertReload, it is a FixedStackPseudoSourceValue
    const PseudoSourceValue *PVal = mo[0]->getPseudoValue();
    return PVal && PVal->kind() == PseudoSourceValue::FixedStack;
}
#endif

static void insertSWCFIpattern(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, const DebugLoc &DL, const TargetInstrInfo *TII, unsigned BranchTargetReg, MachineBasicBlock *trapMBB) {
    bool r11IsBase = BranchTargetReg == X86::R11;
    Register CompareReg = r11IsBase ? X86::R12 : X86::R11;

    if (r11IsBase) {
        // R12 must be saved as it is not a temporary register as opposed to R11
        BuildMI(MBB, MBBI, DL, TII->get(X86::PUSH64r)).addReg(X86::R12);
    }

    BuildMI(MBB, MBBI, DL, TII->get(X86::MOV64rr)).addReg(CompareReg).addReg(BranchTargetReg);
    BuildMI(MBB, MBBI, DL, TII->get(X86::MOV32rm), CompareReg)
        .addReg(CompareReg)     // Base
        .addImm(32)            // Scale
        .addReg(0)             // Index
        .addImm(0)             // Displacement
        .addReg(0)             // Segment
        ;

    BuildMI(MBB, MBBI, DL, TII->get(X86::ADD32ri), CompareReg)
        .addReg(CompareReg)
        .addImm(0x05e1f00d); // -ENDBR64

    if (r11IsBase) {
        // Restore R12
        // POP affects no flags, so the JE should work
        BuildMI(MBB, MBBI, DL, TII->get(X86::POP64r)).addReg(X86::R12);
    }

    MBB.addSuccessor(trapMBB);
    BuildMI(MBB, MBBI, DL, TII->get(X86::JCC_1)).addMBB(trapMBB).addImm(X86::COND_NE);
}

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

    X86TraceModeEnum TraceMode = getTraceX86Sandbox(MF);

    SmallVector<MachineInstr *, 16> Rets;

    // Create a block for the CFI trap branch shared by all SW-CFI patterns in the function
    if (SandboxCFIMode == SandboxModeEnum::SWCFI) {
        trapMBB = MF.CreateMachineBasicBlock();
        trapMBB->setIsEHPad(true); // This prevents from getting "Undefined temporary symbol .LBB"
        // error when compiling  no-return functions
        BuildMI(trapMBB, DebugLoc(), TII->get(X86::INT3));
        MF.push_back(trapMBB);
    }

    for (auto &MBB : MF) {

        if (TraceMode != X86TraceModeEnum::NONE) {
          errs() << "[X86] [SandboxPass] :" << MF.getName() << "\n";
        }

        for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
            DILocation *DL = MBBI->getDebugLoc();

            if (TraceMode == X86TraceModeEnum::FUNC) {
              errs() << "[X86] [" << MF.getName() << "] ";
              MBBI->print(errs());
            }

            if (isIndirectCallOrBranch(*MBBI)) {
                MachineOperand &Base = MBBI->getOperand(0);

                if (SandboxCFIMode == SandboxModeEnum::SWCFI) {
                    if (TraceMode != X86TraceModeEnum::NONE) {
                      errs() << "[X86] [SandboxPass] : adding SW CFI\n";
                    }

#ifdef VERIFY_SANDBOX
                    SmallPtrSet<MachineInstr *, 1> defMIs;
                    RDA.getGlobalReachingDefs(&*MBBI, Base.getReg().asMCReg(), defMIs);

                    for (auto *defMI : defMIs) {
                        if (defMI->getOpcode() != X86::X86_sandboxcfi && !mayBeSpilledAddrReload(*defMI)) {
                            errs() << "------------------------------------[ X86SandboxPass ]------------------------------------\n";
                            errs() << MF.getName() << "\n";
                            errs() << "indirect call/jump instruction uses value not passed through X86_sandboxcfi\n";
                            MBBI->getDebugLoc().print(errs());
                            errs() << "\n";
                            defMI->print(errs());
                            errs() << "------------------------------------------------------------------------------------------\n";
                            std::abort();
                        }
                    }
#endif

                    insertSWCFIpattern(MBB, MBBI, DL, TII, Base.getReg().id(), trapMBB);
                }

                if (SandboxCFIMode == SandboxModeEnum::HWCFI) {
                    if (TraceMode != X86TraceModeEnum::NONE) {
                      errs() << "[X86] [SandboxPass] : adding HW CFI\n";
                    }
                    // use test instruction to add a data dependency on the branch target to force MPK evaluation
                    BuildMI(MBB, MBBI, DL, TII->get(X86::TEST64mr)).addReg(Base.getReg()).addImm(64).addReg(0).addImm(0).addReg(0).addReg(Base.getReg());
                }
            }

            switch (MBBI->getOpcode()) {
                case X86::X86_sandboxcfi: {

                    // This pseudo-instruction serves to ensure that only register-based indirect call/jmp instructions are used (i.e. no memory-based)
                    // thanks to the pass-thru return value of this instruction used as the target address of the call/jmp.

                    // Copy the target address register to the return register if the two differ
                    Register TargetPtrReg = MBBI->getOperand(1).getReg();
                    Register ReturnReg = MBBI->getOperand(0).getReg();
                    if (TargetPtrReg != ReturnReg) {
                        BuildMI(MBB, MBBI, DL, TII->get(X86::MOV64rr), ReturnReg).addReg(TargetPtrReg);
                    }

                    break;
                }
                case X86::X86_sandboxpoll: {
                    // The polling mechanism is not used yet in GraalOS, so this branch should not be reached
                    BuildMI(MBB, MBBI, DL, TII->get(X86::NOOP)).addRegMask(RI.getNoPreservedMask());
                    break;
                }
                case X86::RET64: {
                    BuildMI(MBB, MBBI, DL, TII->get(X86::POP64r)).addReg(X86::R11);
                    if (SandboxCFIMode == SandboxModeEnum::HWCFI) {
                        BuildMI(MBB, MBBI, DL, TII->get(X86::TEST64mr))
                            .addReg(X86::R11)    // Base register for memory address
                            .addImm(64)      // Scale for index*scale addressing
                            .addReg(0)   // Index register
                            .addImm(0)       // Displacement
                            .addReg(0)     // Segment register (usually 0)
                            .addReg(X86::R11);    // Register operand to test against
                    }
                    if (SandboxCFIMode == SandboxModeEnum::SWCFI) {
                        insertSWCFIpattern(MBB, MBBI, DL, TII, X86::R11, trapMBB);
                    }
                    BuildMI(MBB, MBBI, DL, TII->get(X86::JMP64r)).addReg(X86::R11);
                    // collect returns
                    Rets.push_back(&*MBBI);
                    break;
                }
                default:
                    break;
            }
        }
    }

    for (MachineInstr *Ret : Rets)
        Ret->eraseFromParent();

    return true;
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
