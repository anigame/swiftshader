//===- subzero/src/IceAssemblerARM32.cpp - Assembler for ARM32 --*- C++ -*-===//
//
// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
//
// Modified by the Subzero authors.
//
//===----------------------------------------------------------------------===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the Assembler class for ARM32.
///
//===----------------------------------------------------------------------===//

#include "IceAssemblerARM32.h"
#include "IceCfgNode.h"
#include "IceUtils.h"

namespace {

using namespace Ice;
using namespace Ice::ARM32;

using WordType = uint32_t;
static constexpr IValueT kWordSize = sizeof(WordType);

// The following define individual bits.
static constexpr IValueT B0 = 1;
static constexpr IValueT B1 = 1 << 1;
static constexpr IValueT B2 = 1 << 2;
static constexpr IValueT B3 = 1 << 3;
static constexpr IValueT B4 = 1 << 4;
static constexpr IValueT B5 = 1 << 5;
static constexpr IValueT B6 = 1 << 6;
static constexpr IValueT B7 = 1 << 7;
static constexpr IValueT B12 = 1 << 12;
static constexpr IValueT B13 = 1 << 13;
static constexpr IValueT B14 = 1 << 14;
static constexpr IValueT B15 = 1 << 15;
static constexpr IValueT B16 = 1 << 16;
static constexpr IValueT B17 = 1 << 17;
static constexpr IValueT B18 = 1 << 18;
static constexpr IValueT B19 = 1 << 19;
static constexpr IValueT B20 = 1 << 20;
static constexpr IValueT B21 = 1 << 21;
static constexpr IValueT B22 = 1 << 22;
static constexpr IValueT B23 = 1 << 23;
static constexpr IValueT B24 = 1 << 24;
static constexpr IValueT B25 = 1 << 25;
static constexpr IValueT B26 = 1 << 26;
static constexpr IValueT B27 = 1 << 27;

// Constants used for the decoding or encoding of the individual fields of
// instructions. Based on ARM section A5.1.
static constexpr IValueT L = 1 << 20; // load (or store)
static constexpr IValueT W = 1 << 21; // writeback base register
                                      // (or leave unchanged)
static constexpr IValueT B = 1 << 22; // unsigned byte (or word)
static constexpr IValueT U = 1 << 23; // positive (or negative)
                                      // offset/index
static constexpr IValueT P = 1 << 24; // offset/pre-indexed
                                      // addressing (or
                                      // post-indexed addressing)

static constexpr IValueT kConditionShift = 28;
static constexpr IValueT kLinkShift = 24;
static constexpr IValueT kOpcodeShift = 21;
static constexpr IValueT kRdShift = 12;
static constexpr IValueT kRmShift = 0;
static constexpr IValueT kRnShift = 16;
static constexpr IValueT kRsShift = 8;
static constexpr IValueT kSShift = 20;
static constexpr IValueT kTypeShift = 25;

// Immediate instruction fields encoding.
static constexpr IValueT kImmed8Bits = 8;
static constexpr IValueT kImmed8Shift = 0;
static constexpr IValueT kRotateBits = 4;
static constexpr IValueT kRotateShift = 8;

// Shift instruction register fields encodings.
static constexpr IValueT kShiftImmShift = 7;
static constexpr IValueT kShiftImmBits = 5;
static constexpr IValueT kShiftShift = 5;
static constexpr IValueT kImmed12Bits = 12;
static constexpr IValueT kImm12Shift = 0;

// Rotation instructions (uxtb etc.).
static constexpr IValueT kRotationShift = 10;

// Div instruction register field encodings.
static constexpr IValueT kDivRdShift = 16;
static constexpr IValueT kDivRmShift = 8;
static constexpr IValueT kDivRnShift = 0;

// Type of instruction encoding (bits 25-27). See ARM section A5.1
static constexpr IValueT kInstTypeDataRegister = 0;  // i.e. 000
static constexpr IValueT kInstTypeDataRegShift = 0;  // i.e. 000
static constexpr IValueT kInstTypeDataImmediate = 1; // i.e. 001
static constexpr IValueT kInstTypeMemImmediate = 2;  // i.e. 010
static constexpr IValueT kInstTypeRegisterShift = 3; // i.e. 011

// Offset modifier to current PC for next instruction.  The offset is off by 8
// due to the way the ARM CPUs read PC.
static constexpr IOffsetT kPCReadOffset = 8;

// Mask to pull out PC offset from branch (b) instruction.
static constexpr int kBranchOffsetBits = 24;
static constexpr IOffsetT kBranchOffsetMask = 0x00ffffff;

IValueT encodeBool(bool B) { return B ? 1 : 0; }

IValueT encodeRotation(ARM32::AssemblerARM32::RotationValue Value) {
  return static_cast<IValueT>(Value);
}

IValueT encodeGPRRegister(RegARM32::GPRRegister Rn) {
  return static_cast<IValueT>(Rn);
}

RegARM32::GPRRegister decodeGPRRegister(IValueT R) {
  return static_cast<RegARM32::GPRRegister>(R);
}

bool isGPRRegisterDefined(IValueT R) {
  return R != encodeGPRRegister(RegARM32::Encoded_Not_GPR);
}

bool isConditionDefined(CondARM32::Cond Cond) {
  return Cond != CondARM32::kNone;
}

IValueT encodeCondition(CondARM32::Cond Cond) {
  return static_cast<IValueT>(Cond);
}

IValueT encodeShift(OperandARM32::ShiftKind Shift) {
  // Follows encoding in ARM section A8.4.1 "Constant shifts".
  switch (Shift) {
  case OperandARM32::kNoShift:
  case OperandARM32::LSL:
    return 0; // 0b00
  case OperandARM32::LSR:
    return 1; // 0b01
  case OperandARM32::ASR:
    return 2; // 0b10
  case OperandARM32::ROR:
  case OperandARM32::RRX:
    return 3; // 0b11
  }
  llvm::report_fatal_error("Unknown Shift value");
  return 0;
}

// Returns the bits in the corresponding masked value.
IValueT mask(IValueT Value, IValueT Shift, IValueT Bits) {
  return (Value >> Shift) & ((1 << Bits) - 1);
}

// Extract out a Bit in Value.
bool isBitSet(IValueT Bit, IValueT Value) { return (Value & Bit) == Bit; }

// Returns the GPR register at given Shift in Value.
RegARM32::GPRRegister getGPRReg(IValueT Shift, IValueT Value) {
  return decodeGPRRegister((Value >> Shift) & 0xF);
}

// The way an operand is encoded into a sequence of bits in functions
// encodeOperand and encodeAddress below.
enum EncodedOperand {
  // Unable to encode, value left undefined.
  CantEncode = 0,
  // Value is register found.
  EncodedAsRegister,
  // Value=rrrriiiiiiii where rrrr is the rotation, and iiiiiiii is the imm8
  // value.
  EncodedAsRotatedImm8,
  // Value=0000000pu0w0nnnn0000iiiiiiiiiiii where nnnn is the base register Rn,
  // p=1 if pre-indexed addressing, u=1 if offset positive, w=1 if writeback to
  // Rn should be used, and iiiiiiiiiiii defines the rotated Imm8 value.
  EncodedAsImmRegOffset,
  // Value=00000000pu0w0nnnn0000iiii0000jjjj where nnnn=Rn, iiiijjjj=Imm8, p=1
  // if pre-indexed addressing, u=1 if offset positive, and w=1 if writeback to
  // Rn.
  EncodedAsImmRegOffsetEnc3,
  // Value=0000000pu0w00nnnnttttiiiiiss0mmmm where nnnn is the base register Rn,
  // mmmm is the index register Rm, iiiii is the shift amount, ss is the shift
  // kind, p=1 if pre-indexed addressing, u=1 if offset positive, and w=1 if
  // writeback to Rn.
  EncodedAsShiftRotateImm5,
  // Value=000000000000000000000iiiii0000000 where iiii defines the Imm5 value
  // to shift.
  EncodedAsShiftImm5,
  // i.e. iiiiiss0mmmm where mmmm is the register to rotate, ss is the shift
  // kind, and iiiii is the shift amount.
  EncodedAsShiftedRegister,
  // Value is 32bit integer constant.
  EncodedAsConstI32
};

// Sets Encoding to a rotated Imm8 encoding of Value, if possible.
IValueT encodeRotatedImm8(IValueT RotateAmt, IValueT Immed8) {
  assert(RotateAmt < (1 << kRotateBits));
  assert(Immed8 < (1 << kImmed8Bits));
  return (RotateAmt << kRotateShift) | (Immed8 << kImmed8Shift);
}

// Encodes iiiiitt0mmmm for data-processing (2nd) operands where iiiii=Imm5,
// tt=Shift, and mmmm=Rm.
IValueT encodeShiftRotateImm5(IValueT Rm, OperandARM32::ShiftKind Shift,
                              IOffsetT imm5) {
  (void)kShiftImmBits;
  assert(imm5 < (1 << kShiftImmBits));
  return (imm5 << kShiftImmShift) | (encodeShift(Shift) << kShiftShift) | Rm;
}

// Encodes mmmmtt01ssss for data-processing operands where mmmm=Rm, ssss=Rs, and
// tt=Shift.
IValueT encodeShiftRotateReg(IValueT Rm, OperandARM32::ShiftKind Shift,
                             IValueT Rs) {
  return (Rs << kRsShift) | (encodeShift(Shift) << kShiftShift) | B4 |
         (Rm << kRmShift);
}

EncodedOperand encodeOperand(const Operand *Opnd, IValueT &Value) {
  Value = 0; // Make sure initialized.
  if (const auto *Var = llvm::dyn_cast<Variable>(Opnd)) {
    if (Var->hasReg()) {
      Value = Var->getRegNum();
      return EncodedAsRegister;
    }
    return CantEncode;
  }
  if (const auto *FlexImm = llvm::dyn_cast<OperandARM32FlexImm>(Opnd)) {
    const IValueT Immed8 = FlexImm->getImm();
    const IValueT Rotate = FlexImm->getRotateAmt();
    if (!((Rotate < (1 << kRotateBits)) && (Immed8 < (1 << kImmed8Bits))))
      return CantEncode;
    Value = (Rotate << kRotateShift) | (Immed8 << kImmed8Shift);
    return EncodedAsRotatedImm8;
  }
  if (const auto *Const = llvm::dyn_cast<ConstantInteger32>(Opnd)) {
    Value = Const->getValue();
    return EncodedAsConstI32;
  }
  if (const auto *FlexReg = llvm::dyn_cast<OperandARM32FlexReg>(Opnd)) {
    Operand *Amt = FlexReg->getShiftAmt();
    if (const auto *Imm5 = llvm::dyn_cast<OperandARM32ShAmtImm>(Amt)) {
      IValueT Rm;
      if (encodeOperand(FlexReg->getReg(), Rm) != EncodedAsRegister)
        return CantEncode;
      Value =
          encodeShiftRotateImm5(Rm, FlexReg->getShiftOp(), Imm5->getShAmtImm());
      return EncodedAsShiftedRegister;
    }
    // TODO(kschimpf): Handle case where Amt is a register?
  }
  if (const auto *ShImm = llvm::dyn_cast<OperandARM32ShAmtImm>(Opnd)) {
    const IValueT Immed5 = ShImm->getShAmtImm();
    assert(Immed5 < (1 << kShiftImmBits));
    Value = (Immed5 << kShiftImmShift);
    return EncodedAsShiftImm5;
  }
  return CantEncode;
}

IValueT encodeImmRegOffset(IValueT Reg, IOffsetT Offset,
                           OperandARM32Mem::AddrMode Mode) {
  IValueT Value = Mode | (Reg << kRnShift);
  if (Offset < 0) {
    Value = (Value ^ U) | -Offset; // Flip U to adjust sign.
  } else {
    Value |= Offset;
  }
  return Value;
}

// Encodes immediate register offset using encoding 3.
IValueT encodeImmRegOffsetEnc3(IValueT Rn, IOffsetT Imm8,
                               OperandARM32Mem::AddrMode Mode) {
  IValueT Value = Mode | (Rn << kRnShift);
  if (Imm8 < 0) {
    Imm8 = -Imm8;
    Value = (Value ^ U);
  }
  assert(Imm8 < (1 << 8));
  Value = Value | B22 | ((Imm8 & 0xf0) << 4) | (Imm8 & 0x0f);
  return Value;
}

// Defines alternate layouts of instruction operands, should the (common)
// default pattern not be used.
enum OpEncoding {
  // No alternate layout specified.
  DefaultOpEncoding,
  // Alternate encoding 3.
  OpEncoding3
};

// Encodes memory address Opnd, and encodes that information into Value, based
// on how ARM represents the address. Returns how the value was encoded.
EncodedOperand encodeAddress(const Operand *Opnd, IValueT &Value,
                             const AssemblerARM32::TargetInfo &TInfo,
                             OpEncoding AddressEncoding = DefaultOpEncoding) {
  Value = 0; // Make sure initialized.
  if (const auto *Var = llvm::dyn_cast<Variable>(Opnd)) {
    // Should be a stack variable, with an offset.
    if (Var->hasReg())
      return CantEncode;
    IOffsetT Offset = Var->getStackOffset();
    if (!Utils::IsAbsoluteUint(12, Offset))
      return CantEncode;
    int32_t BaseRegNum = Var->getBaseRegNum();
    if (BaseRegNum == Variable::NoRegister)
      BaseRegNum = TInfo.FrameOrStackReg;
    Value = encodeImmRegOffset(BaseRegNum, Offset, OperandARM32Mem::Offset);
    return EncodedAsImmRegOffset;
  }
  if (const auto *Mem = llvm::dyn_cast<OperandARM32Mem>(Opnd)) {
    Variable *Var = Mem->getBase();
    if (!Var->hasReg())
      return CantEncode;
    IValueT Rn = Var->getRegNum();
    if (Mem->isRegReg()) {
      const Variable *Index = Mem->getIndex();
      if (Var == nullptr)
        return CantEncode;
      Value = (Rn << kRnShift) | Mem->getAddrMode() |
              encodeShiftRotateImm5(Index->getRegNum(), Mem->getShiftOp(),
                                    Mem->getShiftAmt());
      return EncodedAsShiftRotateImm5;
    }
    // Encoded as immediate register offset.
    ConstantInteger32 *Offset = Mem->getOffset();
    switch (AddressEncoding) {
    case DefaultOpEncoding:
      Value = encodeImmRegOffset(Rn, Offset->getValue(), Mem->getAddrMode());
      return EncodedAsImmRegOffset;
    case OpEncoding3:
      Value =
          encodeImmRegOffsetEnc3(Rn, Offset->getValue(), Mem->getAddrMode());
      return EncodedAsImmRegOffsetEnc3;
    }
  }
  return CantEncode;
}

// Checks that Offset can fit in imm24 constant of branch (b) instruction.
bool canEncodeBranchOffset(IOffsetT Offset) {
  return Utils::IsAligned(Offset, 4) &&
         Utils::IsInt(kBranchOffsetBits, Offset >> 2);
}

IValueT encodeRegister(const Operand *OpReg, const char *RegName,
                       const char *InstName) {
  IValueT Reg = 0;
  if (encodeOperand(OpReg, Reg) != EncodedAsRegister)
    llvm::report_fatal_error(std::string(InstName) + ": Can't find register " +
                             RegName);
  return Reg;
}

void verifyRegDefined(IValueT Reg, const char *RegName, const char *InstName) {
  if (BuildDefs::minimal())
    return;
  if (!isGPRRegisterDefined(Reg))
    llvm::report_fatal_error(std::string(InstName) + ": Can't find " + RegName);
}

void verifyCondDefined(CondARM32::Cond Cond, const char *InstName) {
  if (BuildDefs::minimal())
    return;
  if (!isConditionDefined(Cond))
    llvm::report_fatal_error(std::string(InstName) + ": Condition not defined");
}

void verifyPOrNotW(IValueT Address, const char *InstName) {
  if (BuildDefs::minimal())
    return;
  if (!isBitSet(P, Address) && isBitSet(W, Address))
    llvm::report_fatal_error(std::string(InstName) +
                             ": P=0 when W=1 not allowed");
}

void verifyRegsNotEq(IValueT Reg1, const char *Reg1Name, IValueT Reg2,
                     const char *Reg2Name, const char *InstName) {
  if (BuildDefs::minimal())
    return;
  if (Reg1 == Reg2)
    llvm::report_fatal_error(std::string(InstName) + ": " + Reg1Name + "=" +
                             Reg2Name + " not allowed");
}

void verifyRegNotPc(IValueT Reg, const char *RegName, const char *InstName) {
  verifyRegsNotEq(Reg, RegName, RegARM32::Encoded_Reg_pc, "pc", InstName);
}

void verifyAddrRegNotPc(IValueT RegShift, IValueT Address, const char *RegName,
                        const char *InstName) {
  if (BuildDefs::minimal())
    return;
  if (getGPRReg(RegShift, Address) == RegARM32::Encoded_Reg_pc)
    llvm::report_fatal_error(std::string(InstName) + ": " + RegName +
                             "=pc not allowed");
}

void verifyRegNotPcWhenSetFlags(IValueT Reg, bool SetFlags,
                                const char *InstName) {
  if (BuildDefs::minimal())
    return;
  if (SetFlags && (Reg == RegARM32::Encoded_Reg_pc))
    llvm::report_fatal_error(std::string(InstName) + ": " +
                             RegARM32::RegNames[Reg] +
                             "=pc not allowed when CC=1");
}

} // end of anonymous namespace

namespace Ice {
namespace ARM32 {

size_t MoveRelocatableFixup::emit(GlobalContext *Ctx,
                                  const Assembler &Asm) const {
  if (!BuildDefs::dump())
    return InstARM32::InstSize;
  Ostream &Str = Ctx->getStrEmit();
  IValueT Inst = Asm.load<IValueT>(position());
  Str << "\tmov" << (kind() == llvm::ELF::R_ARM_MOVW_ABS_NC ? "w" : "t") << "\t"
      << RegARM32::RegNames[(Inst >> kRdShift) & 0xF]
      << ", #:" << (kind() == llvm::ELF::R_ARM_MOVW_ABS_NC ? "lower" : "upper")
      << "16:" << symbol(Ctx) << "\t@ .word "
      << llvm::format_hex_no_prefix(Inst, 8) << "\n";
  return InstARM32::InstSize;
}

MoveRelocatableFixup *AssemblerARM32::createMoveFixup(bool IsMovW,
                                                      const Constant *Value) {
  MoveRelocatableFixup *F =
      new (allocate<MoveRelocatableFixup>()) MoveRelocatableFixup();
  F->set_kind(IsMovW ? llvm::ELF::R_ARM_MOVW_ABS_NC
                     : llvm::ELF::R_ARM_MOVT_ABS);
  F->set_value(Value);
  Buffer.installFixup(F);
  return F;
}

size_t BlRelocatableFixup::emit(GlobalContext *Ctx,
                                const Assembler &Asm) const {
  if (!BuildDefs::dump())
    return InstARM32::InstSize;
  Ostream &Str = Ctx->getStrEmit();
  IValueT Inst = Asm.load<IValueT>(position());
  Str << "\t"
      << "bl\t" << symbol(Ctx) << "\t@ .word "
      << llvm::format_hex_no_prefix(Inst, 8) << "\n";
  return InstARM32::InstSize;
}

void AssemblerARM32::padWithNop(intptr_t Padding) {
  constexpr intptr_t InstWidth = sizeof(IValueT);
  assert(Padding % InstWidth == 0 &&
         "Padding not multiple of instruction size");
  for (intptr_t i = 0; i < Padding; i += InstWidth)
    nop();
}

BlRelocatableFixup *
AssemblerARM32::createBlFixup(const ConstantRelocatable *BlTarget) {
  BlRelocatableFixup *F =
      new (allocate<BlRelocatableFixup>()) BlRelocatableFixup();
  F->set_kind(llvm::ELF::R_ARM_CALL);
  F->set_value(BlTarget);
  Buffer.installFixup(F);
  return F;
}

void AssemblerARM32::bindCfgNodeLabel(const CfgNode *Node) {
  GlobalContext *Ctx = Node->getCfg()->getContext();
  if (BuildDefs::dump() && !Ctx->getFlags().getDisableHybridAssembly()) {
    // Generate label name so that branches can find it.
    constexpr SizeT InstSize = 0;
    emitTextInst(Node->getAsmName() + ":", InstSize);
  }
  SizeT NodeNumber = Node->getIndex();
  assert(!getPreliminary());
  Label *L = getOrCreateCfgNodeLabel(NodeNumber);
  this->bind(L);
}

Label *AssemblerARM32::getOrCreateLabel(SizeT Number, LabelVector &Labels) {
  Label *L = nullptr;
  if (Number == Labels.size()) {
    L = new (this->allocate<Label>()) Label();
    Labels.push_back(L);
    return L;
  }
  if (Number > Labels.size()) {
    Labels.resize(Number + 1);
  }
  L = Labels[Number];
  if (!L) {
    L = new (this->allocate<Label>()) Label();
    Labels[Number] = L;
  }
  return L;
}

IValueT AssemblerARM32::encodeBranchOffset(IOffsetT Offset, IValueT Inst) {
  // Adjust offset to the way ARM CPUs read PC.
  Offset -= kPCReadOffset;

  bool IsGoodOffset = canEncodeBranchOffset(Offset);
  assert(IsGoodOffset);
  // Note: Following cast is for MINIMAL build.
  (void)IsGoodOffset;

  // Properly preserve only the bits supported in the instruction.
  Offset >>= 2;
  Offset &= kBranchOffsetMask;
  return (Inst & ~kBranchOffsetMask) | Offset;
}

// Pull out offset from branch Inst.
IOffsetT AssemblerARM32::decodeBranchOffset(IValueT Inst) {
  // Sign-extend, left-shift by 2, and adjust to the way ARM CPUs read PC.
  IOffsetT Offset = static_cast<IOffsetT>((Inst & kBranchOffsetMask) << 8);
  return (Offset >> 6) + kPCReadOffset;
}

void AssemblerARM32::bind(Label *L) {
  IOffsetT BoundPc = Buffer.size();
  assert(!L->isBound()); // Labels can only be bound once.
  while (L->isLinked()) {
    IOffsetT Position = L->getLinkPosition();
    IOffsetT Dest = BoundPc - Position;
    IValueT Inst = Buffer.load<IValueT>(Position);
    Buffer.store<IValueT>(Position, encodeBranchOffset(Dest, Inst));
    L->setPosition(decodeBranchOffset(Inst));
  }
  L->bindTo(BoundPc);
}

void AssemblerARM32::emitTextInst(const std::string &Text, SizeT InstSize) {
  AssemblerFixup *F = createTextFixup(Text, InstSize);
  emitFixup(F);
  for (SizeT I = 0; I < InstSize; ++I) {
    AssemblerBuffer::EnsureCapacity ensured(&Buffer);
    Buffer.emit<char>(0);
  }
}

void AssemblerARM32::emitType01(CondARM32::Cond Cond, IValueT InstType,
                                IValueT Opcode, bool SetFlags, IValueT Rn,
                                IValueT Rd, IValueT Imm12,
                                EmitChecks RuleChecks, const char *InstName) {
  switch (RuleChecks) {
  case NoChecks:
    break;
  case RdIsPcAndSetFlags:
    verifyRegNotPcWhenSetFlags(Rd, SetFlags, InstName);
    break;
  }
  verifyRegDefined(Rd, "Rd", InstName);
  verifyCondDefined(Cond, InstName);
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  const IValueT Encoding = (encodeCondition(Cond) << kConditionShift) |
                           (InstType << kTypeShift) | (Opcode << kOpcodeShift) |
                           (encodeBool(SetFlags) << kSShift) |
                           (Rn << kRnShift) | (Rd << kRdShift) | Imm12;
  emitInst(Encoding);
}

void AssemblerARM32::emitType01(CondARM32::Cond Cond, IValueT Opcode,
                                const Operand *OpRd, const Operand *OpRn,
                                const Operand *OpSrc1, bool SetFlags,
                                EmitChecks RuleChecks, const char *InstName) {
  IValueT Rd = encodeRegister(OpRd, "Rd", InstName);
  IValueT Rn = encodeRegister(OpRn, "Rn", InstName);
  emitType01(Cond, Opcode, Rd, Rn, OpSrc1, SetFlags, RuleChecks, InstName);
}

void AssemblerARM32::emitType01(CondARM32::Cond Cond, IValueT Opcode,
                                IValueT Rd, IValueT Rn, const Operand *OpSrc1,
                                bool SetFlags, EmitChecks RuleChecks,
                                const char *InstName) {

  IValueT Src1Value;
  // TODO(kschimpf) Other possible decodings of data operations.
  switch (encodeOperand(OpSrc1, Src1Value)) {
  default:
    // TODO(kschimpf): Figure out what additional cases need to be handled.
    return setNeedsTextFixup();
  case EncodedAsRegister: {
    // XXX (register)
    //   xxx{s}<c> <Rd>, <Rn>, <Rm>{, <shiff>}
    //
    // cccc0000100snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
    // mmmm=Rm, iiiii=Shift, tt=ShiftKind, and s=SetFlags.
    constexpr IValueT Imm5 = 0;
    Src1Value = encodeShiftRotateImm5(Src1Value, OperandARM32::kNoShift, Imm5);
    emitType01(Cond, kInstTypeDataRegister, Opcode, SetFlags, Rn, Rd, Src1Value,
               RuleChecks, InstName);
    return;
  }
  case EncodedAsShiftedRegister: {
    // Form is defined in case EncodedAsRegister. (i.e. XXX (register)).
    emitType01(Cond, kInstTypeDataRegister, Opcode, SetFlags, Rn, Rd, Src1Value,
               RuleChecks, InstName);
    return;
  }
  case EncodedAsConstI32: {
    // See if we can convert this to an XXX (immediate).
    IValueT RotateAmt;
    IValueT Imm8;
    if (!OperandARM32FlexImm::canHoldImm(Src1Value, &RotateAmt, &Imm8))
      llvm::report_fatal_error(std::string(InstName) +
                               ": Immediate rotated constant not valid");
    Src1Value = encodeRotatedImm8(RotateAmt, Imm8);
    // Intentionally fall to next case!
  }
  case EncodedAsRotatedImm8: {
    // XXX (Immediate)
    //   xxx{s}<c> <Rd>, <Rn>, #<RotatedImm8>
    //
    // cccc0010100snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
    // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
    emitType01(Cond, kInstTypeDataImmediate, Opcode, SetFlags, Rn, Rd,
               Src1Value, RuleChecks, InstName);
    return;
  }
  }
}

void AssemblerARM32::emitType05(CondARM32::Cond Cond, IOffsetT Offset,
                                bool Link, const char *InstName) {
  // cccc101liiiiiiiiiiiiiiiiiiiiiiii where cccc=Cond, l=Link, and
  // iiiiiiiiiiiiiiiiiiiiiiii=
  // EncodedBranchOffset(cccc101l000000000000000000000000, Offset);
  verifyCondDefined(Cond, InstName);
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  IValueT Encoding = static_cast<int32_t>(Cond) << kConditionShift |
                     5 << kTypeShift | (Link ? 1 : 0) << kLinkShift;
  Encoding = encodeBranchOffset(Offset, Encoding);
  emitInst(Encoding);
}

void AssemblerARM32::emitBranch(Label *L, CondARM32::Cond Cond, bool Link) {
  // TODO(kschimpf): Handle far jumps.
  constexpr const char *BranchName = "b";
  if (L->isBound()) {
    const int32_t Dest = L->getPosition() - Buffer.size();
    emitType05(Cond, Dest, Link, BranchName);
    return;
  }
  const IOffsetT Position = Buffer.size();
  // Use the offset field of the branch instruction for linking the sites.
  emitType05(Cond, L->getEncodedPosition(), Link, BranchName);
  L->linkTo(*this, Position);
}

void AssemblerARM32::emitCompareOp(CondARM32::Cond Cond, IValueT Opcode,
                                   const Operand *OpRn, const Operand *OpSrc1,
                                   const char *InstName) {
  // XXX (register)
  //   XXX<c> <Rn>, <Rm>{, <shift>}
  //
  // ccccyyyxxxx1nnnn0000iiiiitt0mmmm where cccc=Cond, nnnn=Rn, mmmm=Rm, iiiii
  // defines shift constant, tt=ShiftKind, yyy=kInstTypeDataRegister, and
  // xxxx=Opcode.
  //
  // XXX (immediate)
  //  XXX<c> <Rn>, #<RotatedImm8>
  //
  // ccccyyyxxxx1nnnn0000iiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // yyy=kInstTypeDataImmdiate, xxxx=Opcode, and iiiiiiiiiiii=Src1Value
  // defining RotatedImm8.
  constexpr bool SetFlags = true;
  constexpr IValueT Rd = RegARM32::Encoded_Reg_r0;
  IValueT Rn = encodeRegister(OpRn, "Rn", InstName);
  emitType01(Cond, Opcode, Rd, Rn, OpSrc1, SetFlags, NoChecks, InstName);
}

void AssemblerARM32::emitMemOp(CondARM32::Cond Cond, IValueT InstType,
                               bool IsLoad, bool IsByte, IValueT Rt,
                               IValueT Address, const char *InstName) {
  verifyRegDefined(Rt, "Rt", InstName);
  verifyCondDefined(Cond, InstName);
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  const IValueT Encoding = (encodeCondition(Cond) << kConditionShift) |
                           (InstType << kTypeShift) | (IsLoad ? L : 0) |
                           (IsByte ? B : 0) | (Rt << kRdShift) | Address;
  emitInst(Encoding);
}

void AssemblerARM32::emitMemOp(CondARM32::Cond Cond, bool IsLoad, bool IsByte,
                               IValueT Rt, const Operand *OpAddress,
                               const TargetInfo &TInfo, const char *InstName) {
  IValueT Address;
  switch (encodeAddress(OpAddress, Address, TInfo)) {
  default:
    llvm::report_fatal_error(std::string(InstName) +
                             ": Memory address not understood");
  case EncodedAsImmRegOffset: {
    // XXX{B} (immediate):
    //   xxx{b}<c> <Rt>, [<Rn>{, #+/-<imm12>}]      ; p=1, w=0
    //   xxx{b}<c> <Rt>, [<Rn>], #+/-<imm12>        ; p=1, w=1
    //   xxx{b}<c> <Rt>, [<Rn>, #+/-<imm12>]!       ; p=0, w=1
    //
    // cccc010pubwlnnnnttttiiiiiiiiiiii where cccc=Cond, tttt=Rt, nnnn=Rn,
    // iiiiiiiiiiii=imm12, b=IsByte, pu0w<<21 is a BlockAddr, l=IsLoad, and
    // pu0w0nnnn0000iiiiiiiiiiii=Address.
    RegARM32::GPRRegister Rn = getGPRReg(kRnShift, Address);

    // Check if conditions of rules violated.
    verifyRegNotPc(Rn, "Rn", InstName);
    verifyPOrNotW(Address, InstName);
    if (!IsByte && (Rn == RegARM32::Encoded_Reg_sp) && !isBitSet(P, Address) &&
        isBitSet(U, Address) && !isBitSet(W, Address) &&
        (mask(Address, kImm12Shift, kImmed12Bits) == 0x8 /* 000000000100 */))
      llvm::report_fatal_error(std::string(InstName) +
                               ": Use push/pop instead");

    emitMemOp(Cond, kInstTypeMemImmediate, IsLoad, IsByte, Rt, Address,
              InstName);
    return;
  }
  case EncodedAsShiftRotateImm5: {
    // XXX{B} (register)
    //   xxx{b}<c> <Rt>, [<Rn>, +/-<Rm>{, <shift>}]{!}
    //   xxx{b}<c> <Rt>, [<Rn>], +/-<Rm>{, <shift>}
    //
    // cccc011pubwlnnnnttttiiiiiss0mmmm where cccc=Cond, tttt=Rt,
    // b=IsByte, U=1 if +, pu0b is a BlockAddr, l=IsLoad, and
    // pu0w0nnnn0000iiiiiss0mmmm=Address.
    RegARM32::GPRRegister Rn = getGPRReg(kRnShift, Address);
    RegARM32::GPRRegister Rm = getGPRReg(kRmShift, Address);

    // Check if conditions of rules violated.
    verifyPOrNotW(Address, InstName);
    verifyRegNotPc(Rm, "Rm", InstName);
    if (IsByte)
      verifyRegNotPc(Rt, "Rt", InstName);
    if (isBitSet(W, Address)) {
      verifyRegNotPc(Rn, "Rn", InstName);
      verifyRegsNotEq(Rn, "Rn", Rt, "Rt", InstName);
    }
    emitMemOp(Cond, kInstTypeRegisterShift, IsLoad, IsByte, Rt, Address,
              InstName);
    return;
  }
  }
}

void AssemblerARM32::emitMemOpEnc3(CondARM32::Cond Cond, IValueT Opcode,
                                   IValueT Rt, const Operand *OpAddress,
                                   const TargetInfo &TInfo,
                                   const char *InstName) {
  IValueT Address;
  switch (encodeAddress(OpAddress, Address, TInfo, OpEncoding3)) {
  default:
    llvm::report_fatal_error(std::string(InstName) +
                             ": Memory address not understood");
  case EncodedAsImmRegOffsetEnc3: {
    // XXXH (immediate)
    //   xxxh<c> <Rt>, [<Rn>{, #+-<Imm8>}]
    //   xxxh<c> <Rt>, [<Rn>, #+/-<Imm8>]
    //   xxxh<c> <Rt>, [<Rn>, #+/-<Imm8>]!
    //
    // cccc000pu0wxnnnnttttiiiiyyyyjjjj where cccc=Cond, nnnn=Rn, tttt=Rt,
    // iiiijjjj=Imm8, pu0w<<21 is a BlockAddr, x000000000000yyyy0000=Opcode,
    // and pu0w0nnnn0000iiii0000jjjj=Address.
    verifyRegDefined(Rt, "Rt", InstName);
    verifyCondDefined(Cond, InstName);
    verifyPOrNotW(Address, InstName);
    verifyRegNotPc(Rt, "Rt", InstName);
    if (isBitSet(W, Address))
      verifyRegsNotEq(getGPRReg(kRnShift, Address), "Rn", Rt, "Rt", InstName);
    const IValueT Encoding = (encodeCondition(Cond) << kConditionShift) |
                             Opcode | (Rt << kRdShift) | Address;
    AssemblerBuffer::EnsureCapacity ensured(&Buffer);
    emitInst(Encoding);
    return;
  }
  case EncodedAsShiftRotateImm5: {
    // XXXH (register)
    //   xxxh<c> <Rt>, [<Rn>, +/-<Rm>]{!}
    //   xxxh<c> <Rt>, [<Rn>], +/-<Rm>
    //
    // cccc000pu0wxnnnntttt00001011mmmm where cccc=Cond, tttt=Rt, nnnn=Rn,
    // mmmm=Rm, pu0w<<21 is a BlockAddr, x000000000000yyyy0000=Opcode, and
    // pu0w0nnnn000000000000mmmm=Address.
    verifyRegDefined(Rt, "Rt", InstName);
    verifyCondDefined(Cond, InstName);
    verifyPOrNotW(Address, InstName);
    verifyRegNotPc(Rt, "Rt", InstName);
    verifyAddrRegNotPc(kRmShift, Address, "Rm", InstName);
    const RegARM32::GPRRegister Rn = getGPRReg(kRnShift, Address);
    if (isBitSet(W, Address)) {
      verifyRegNotPc(Rn, "Rn", InstName);
      verifyRegsNotEq(Rn, "Rn", Rt, "Rt", InstName);
    }
    if (mask(Address, kShiftImmShift, 5) != 0)
      // For encoding 3, no shift is allowed.
      llvm::report_fatal_error(std::string(InstName) +
                               ": Shift constant not allowed");
    const IValueT Encoding = (encodeCondition(Cond) << kConditionShift) |
                             Opcode | (Rt << kRdShift) | Address;
    AssemblerBuffer::EnsureCapacity ensured(&Buffer);
    emitInst(Encoding);
    return;
  }
  }
}

void AssemblerARM32::emitDivOp(CondARM32::Cond Cond, IValueT Opcode, IValueT Rd,
                               IValueT Rn, IValueT Rm, const char *InstName) {
  verifyRegDefined(Rd, "Rd", InstName);
  verifyRegDefined(Rn, "Rn", InstName);
  verifyRegDefined(Rm, "Rm", InstName);
  verifyCondDefined(Cond, InstName);
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  const IValueT Encoding = Opcode | (encodeCondition(Cond) << kConditionShift) |
                           (Rn << kDivRnShift) | (Rd << kDivRdShift) | B26 |
                           B25 | B24 | B20 | B15 | B14 | B13 | B12 | B4 |
                           (Rm << kDivRmShift);
  emitInst(Encoding);
}

void AssemblerARM32::emitMulOp(CondARM32::Cond Cond, IValueT Opcode, IValueT Rd,
                               IValueT Rn, IValueT Rm, IValueT Rs,
                               bool SetFlags, const char *InstName) {
  verifyRegDefined(Rd, "Rd", InstName);
  verifyRegDefined(Rn, "Rn", InstName);
  verifyRegDefined(Rm, "Rm", InstName);
  verifyRegDefined(Rs, "Rs", InstName);
  verifyCondDefined(Cond, InstName);
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  IValueT Encoding = Opcode | (encodeCondition(Cond) << kConditionShift) |
                     (encodeBool(SetFlags) << kSShift) | (Rn << kRnShift) |
                     (Rd << kRdShift) | (Rs << kRsShift) | B7 | B4 |
                     (Rm << kRmShift);
  emitInst(Encoding);
}

void AssemblerARM32::emitMultiMemOp(CondARM32::Cond Cond,
                                    BlockAddressMode AddressMode, bool IsLoad,
                                    IValueT BaseReg, IValueT Registers,
                                    const char *InstName) {
  constexpr IValueT NumGPRegisters = 16;
  verifyCondDefined(Cond, InstName);
  verifyRegDefined(BaseReg, "base", InstName);
  if (Registers >= (1 << NumGPRegisters))
    llvm::report_fatal_error(std::string(InstName) +
                             ": Register set too large");
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  IValueT Encoding = (encodeCondition(Cond) << kConditionShift) | B27 |
                     AddressMode | (IsLoad ? L : 0) | (BaseReg << kRnShift) |
                     Registers;
  emitInst(Encoding);
}

void AssemblerARM32::emitSignExtend(CondARM32::Cond Cond, IValueT Opcode,
                                    const Operand *OpRd, const Operand *OpSrc0,
                                    const char *InstName) {
  IValueT Rd = encodeRegister(OpRd, "Rd", InstName);
  IValueT Rm = encodeRegister(OpSrc0, "Rm", InstName);
  // Note: For the moment, we assume no rotation is specified.
  RotationValue Rotation = kRotateNone;
  constexpr IValueT Rn = RegARM32::Encoded_Reg_pc;
  switch (typeWidthInBytes(OpSrc0->getType())) {
  default:
    llvm::report_fatal_error(std::string(InstName) +
                             ": Type of Rm not understood");
    break;
  case 1: {
    // SXTB/UXTB - Arm sections A8.8.233 and A8.8.274, encoding A1:
    //   sxtb<c> <Rd>, <Rm>{, <rotate>}
    //   uxtb<c> <Rd>, <Rm>{, <rotate>}
    //
    // ccccxxxxxxxx1111ddddrr000111mmmm where cccc=Cond, xxxxxxxx<<20=Opcode,
    // dddd=Rd, mmmm=Rm, and rr defined (RotationValue) rotate.
    break;
  }
  case 2: {
    // SXTH/UXTH - ARM sections A8.8.235 and A8.8.276, encoding A1:
    //   uxth<c> <Rd>< <Rm>{, <rotate>}
    //
    // cccc01101111nnnnddddrr000111mmmm where cccc=Cond, dddd=Rd, mmmm=Rm, and
    // rr defined (RotationValue) rotate.
    Opcode |= B20;
    break;
  }
  }

  verifyCondDefined(Cond, InstName);
  IValueT Rot = encodeRotation(Rotation);
  if (!Utils::IsUint(2, Rot))
    llvm::report_fatal_error(std::string(InstName) +
                             ": Illegal rotation value");
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  IValueT Encoding = (encodeCondition(Cond) << kConditionShift) | Opcode |
                     (Rn << kRnShift) | (Rd << kRdShift) |
                     (Rot << kRotationShift) | B6 | B5 | B4 | (Rm << kRmShift);
  emitInst(Encoding);
}

void AssemblerARM32::adc(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // ADC (register) - ARM section 18.8.2, encoding A1:
  //   adc{s}<c> <Rd>, <Rn>, <Rm>{, <shift>}
  //
  // cccc0000101snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=Shift, tt=ShiftKind, and s=SetFlags.
  //
  // ADC (Immediate) - ARM section A8.8.1, encoding A1:
  //   adc{s}<c> <Rd>, <Rn>, #<RotatedImm8>
  //
  // cccc0010101snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  constexpr const char *AdcName = "adc";
  constexpr IValueT AdcOpcode = B2 | B0; // 0101
  emitType01(Cond, AdcOpcode, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             AdcName);
}

void AssemblerARM32::add(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // ADD (register) - ARM section A8.8.7, encoding A1:
  //   add{s}<c> <Rd>, <Rn>, <Rm>{, <shiff>}
  // ADD (Sp plus register) - ARM section A8.8.11, encoding A1:
  //   add{s}<c> sp, <Rn>, <Rm>{, <shiff>}
  //
  // cccc0000100snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=Shift, tt=ShiftKind, and s=SetFlags.
  //
  // ADD (Immediate) - ARM section A8.8.5, encoding A1:
  //   add{s}<c> <Rd>, <Rn>, #<RotatedImm8>
  // ADD (SP plus immediate) - ARM section A8.8.9, encoding A1.
  //   add{s}<c> <Rd>, sp, #<RotatedImm8>
  //
  // cccc0010100snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  constexpr const char *AddName = "add";
  constexpr IValueT Add = B2; // 0100
  emitType01(Cond, Add, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             AddName);
}

void AssemblerARM32::and_(const Operand *OpRd, const Operand *OpRn,
                          const Operand *OpSrc1, bool SetFlags,
                          CondARM32::Cond Cond) {
  // AND (register) - ARM section A8.8.14, encoding A1:
  //   and{s}<c> <Rd>, <Rn>{, <shift>}
  //
  // cccc0000000snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=Shift, tt=ShiftKind, and s=SetFlags.
  //
  // AND (Immediate) - ARM section A8.8.13, encoding A1:
  //   and{s}<c> <Rd>, <Rn>, #<RotatedImm8>
  //
  // cccc0010100snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  constexpr const char *AndName = "and";
  constexpr IValueT And = 0; // 0000
  emitType01(Cond, And, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             AndName);
}

void AssemblerARM32::b(Label *L, CondARM32::Cond Cond) {
  emitBranch(L, Cond, false);
}

void AssemblerARM32::bkpt(uint16_t Imm16) {
  // BKPT - ARM section A*.8.24 - encoding A1:
  //   bkpt #<Imm16>
  //
  // cccc00010010iiiiiiiiiiii0111iiii where cccc=AL and iiiiiiiiiiiiiiii=Imm16
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  const IValueT Encoding = (CondARM32::AL << kConditionShift) | B24 | B21 |
                           ((Imm16 >> 4) << 8) | B6 | B5 | B4 | (Imm16 & 0xf);
  emitInst(Encoding);
}

void AssemblerARM32::bic(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // BIC (register) - ARM section A8.8.22, encoding A1:
  //   bic{s}<c> <Rd>, <Rn>, <Rm>{, <shift>}
  //
  // cccc0001110snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=Shift, tt=ShiftKind, and s=SetFlags.
  //
  // BIC (immediate) - ARM section A8.8.21, encoding A1:
  //   bic{s}<c> <Rd>, <Rn>, #<RotatedImm8>
  //
  // cccc0011110snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rn, nnnn=Rn,
  // s=SetFlags, and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  constexpr const char *BicName = "bic";
  constexpr IValueT BicOpcode = B3 | B2 | B1; // i.e. 1110
  emitType01(Cond, BicOpcode, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             BicName);
}

void AssemblerARM32::bl(const ConstantRelocatable *Target) {
  // BL (immediate) - ARM section A8.8.25, encoding A1:
  //   bl<c> <label>
  //
  // cccc1011iiiiiiiiiiiiiiiiiiiiiiii where cccc=Cond (not currently allowed)
  // and iiiiiiiiiiiiiiiiiiiiiiii is the (encoded) Target to branch to.
  emitFixup(createBlFixup(Target));
  constexpr const char *BlName = "bl";
  constexpr CondARM32::Cond Cond = CondARM32::AL;
  constexpr IValueT Immed = 0;
  constexpr bool Link = true;
  emitType05(Cond, Immed, Link, BlName);
}

void AssemblerARM32::blx(const Operand *Target) {
  // BLX (register) - ARM section A8.8.26, encoding A1:
  //   blx<c> <Rm>
  //
  // cccc000100101111111111110011mmmm where cccc=Cond (not currently allowed)
  // and mmmm=Rm.
  constexpr const char *BlxName = "Blx";
  IValueT Rm = encodeRegister(Target, "Rm", BlxName);
  verifyRegNotPc(Rm, "Rm", BlxName);
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  constexpr CondARM32::Cond Cond = CondARM32::AL;
  int32_t Encoding = (encodeCondition(Cond) << kConditionShift) | B24 | B21 |
                     (0xfff << 8) | B5 | B4 | (Rm << kRmShift);
  emitInst(Encoding);
}

void AssemblerARM32::bx(RegARM32::GPRRegister Rm, CondARM32::Cond Cond) {
  // BX - ARM section A8.8.27, encoding A1:
  //   bx<c> <Rm>
  //
  // cccc000100101111111111110001mmmm where mmmm=rm and cccc=Cond.
  constexpr const char *BxName = "bx";
  verifyCondDefined(Cond, BxName);
  verifyRegDefined(Rm, "Rm", BxName);
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  const IValueT Encoding = (encodeCondition(Cond) << kConditionShift) | B24 |
                           B21 | (0xfff << 8) | B4 |
                           (encodeGPRRegister(Rm) << kRmShift);
  emitInst(Encoding);
}

void AssemblerARM32::cmn(const Operand *OpRn, const Operand *OpSrc1,
                         CondARM32::Cond Cond) {
  // CMN (immediate) - ARM section A8.8.34, encoding A1:
  //   cmn<c> <Rn>, #<RotatedImm8>
  //
  // cccc00110111nnnn0000iiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  //
  // CMN (register) - ARM section A8.8.35, encodeing A1:
  //   cmn<c> <Rn>, <Rm>{, <shift>}
  //
  // cccc00010111nnnn0000iiiiitt0mmmm where cccc=Cond, nnnn=Rn, mmmm=Rm,
  // iiiii=Shift, and tt=ShiftKind.
  constexpr const char *CmnName = "cmn";
  constexpr IValueT CmnOpcode = B3 | B1 | B0; // ie. 1011
  emitCompareOp(Cond, CmnOpcode, OpRn, OpSrc1, CmnName);
}

void AssemblerARM32::cmp(const Operand *OpRn, const Operand *OpSrc1,
                         CondARM32::Cond Cond) {
  // CMP (register) - ARM section A8.8.38, encoding A1:
  //   cmp<c> <Rn>, <Rm>{, <shift>}
  //
  // cccc00010101nnnn0000iiiiitt0mmmm where cccc=Cond, nnnn=Rn, mmmm=Rm,
  // iiiii=Shift, and tt=ShiftKind.
  //
  // CMP (immediate) - ARM section A8.8.37
  //  cmp<c: <Rn>, #<RotatedImm8>
  //
  // cccc00110101nnnn0000iiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  constexpr const char *CmpName = "cmp";
  constexpr IValueT CmpOpcode = B3 | B1; // ie. 1010
  emitCompareOp(Cond, CmpOpcode, OpRn, OpSrc1, CmpName);
}

void AssemblerARM32::dmb(IValueT Option) {
  // DMB - ARM section A8.8.43, encoding A1:
  //   dmb <option>
  //
  // 1111010101111111111100000101xxxx where xxxx=Option.
  assert(Utils::IsUint(4, Option) && "Bad dmb option");
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  const IValueT Encoding =
      (encodeCondition(CondARM32::kNone) << kConditionShift) | B26 | B24 | B22 |
      B21 | B20 | B19 | B18 | B17 | B16 | B15 | B14 | B13 | B12 | B6 | B4 |
      Option;
  emitInst(Encoding);
}

void AssemblerARM32::eor(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // EOR (register) - ARM section A*.8.47, encoding A1:
  //   eor{s}<c> <Rd>, <Rn>, <Rm>{, <shift>}
  //
  // cccc0000001snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=Shift, tt=ShiftKind, and s=SetFlags.
  //
  // EOR (Immediate) - ARM section A8.*.46, encoding A1:
  //   eor{s}<c> <Rd>, <Rn>, #RotatedImm8
  //
  // cccc0010001snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  constexpr const char *EorName = "eor";
  constexpr IValueT EorOpcode = B0; // 0001
  emitType01(Cond, EorOpcode, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             EorName);
}

void AssemblerARM32::ldr(const Operand *OpRt, const Operand *OpAddress,
                         CondARM32::Cond Cond, const TargetInfo &TInfo) {
  constexpr const char *LdrName = "ldr";
  constexpr bool IsLoad = true;
  IValueT Rt = encodeRegister(OpRt, "Rt", LdrName);
  const Type Ty = OpRt->getType();
  switch (typeWidthInBytesLog2(Ty)) {
  case 3:
  // LDRD is not implemented because target lowering handles i64 and double by
  // using two (32-bit) load instructions. Note: Intenionally drop to default
  // case.
  default:
    llvm::report_fatal_error(std::string("ldr : Type ") + typeString(Ty) +
                             " not implementable\n");
  case 0: {
    // Handles i1 and i8 loads.
    //
    // LDRB (immediate) - ARM section A8.8.68, encoding A1:
    //   ldrb<c> <Rt>, [<Rn>{, #+/-<imm12>}]     ; p=1, w=0
    //   ldrb<c> <Rt>, [<Rn>], #+/-<imm12>       ; p=1, w=1
    //   ldrb<c> <Rt>, [<Rn>, #+/-<imm12>]!      ; p=0, w=1
    //
    // cccc010pu1w1nnnnttttiiiiiiiiiiii where cccc=Cond, tttt=Rt, nnnn=Rn,
    // iiiiiiiiiiii=imm12, u=1 if +, pu0w is a BlockAddr, and
    // pu0w0nnnn0000iiiiiiiiiiii=Address.
    //
    // LDRB (register) - ARM section A8.8.66, encoding A1:
    //   ldrb<c> <Rt>, [<Rn>, +/-<Rm>{, <shift>}]{!}
    //   ldrb<c> <Rt>, [<Rn>], +/-<Rm>{, <shift>}
    //
    // cccc011pu1w1nnnnttttiiiiiss0mmmm where cccc=Cond, tttt=Rt, U=1 if +, pu0b
    // is a BlockAddr, and pu0w0nnnn0000iiiiiss0mmmm=Address.
    constexpr bool IsByte = true;
    emitMemOp(Cond, IsLoad, IsByte, Rt, OpAddress, TInfo, LdrName);
    return;
  }
  case 1: {
    // Handles i16 loads.
    //
    // LDRH (immediate) - ARM section A8.8.80, encoding A1:
    //   ldrh<c> <Rt>, [<Rn>{, #+/-<Imm8>}]
    //   ldrh<c> <Rt>, [<Rn>], #+/-<Imm8>
    //   ldrh<c> <Rt>, [<Rn>, #+/-<Imm8>]!
    //
    // cccc000pu1w1nnnnttttiiii1011iiii where cccc=Cond, tttt=Rt, nnnn=Rn,
    // iiiiiiii=Imm8, u=1 if +, pu0w is a BlockAddr, and
    // pu0w0nnnn0000iiiiiiiiiiii=Address.
    constexpr const char *Ldrh = "ldrh";
    emitMemOpEnc3(Cond, L | B7 | B5 | B4, Rt, OpAddress, TInfo, Ldrh);
    return;
  }
  case 2: {
    // Note: Handles i32 and float loads. Target lowering handles i64 and
    // double by using two (32 bit) load instructions.
    //
    // LDR (immediate) - ARM section A8.8.63, encoding A1:
    //   ldr<c> <Rt>, [<Rn>{, #+/-<imm12>}]      ; p=1, w=0
    //   ldr<c> <Rt>, [<Rn>], #+/-<imm12>        ; p=1, w=1
    //   ldr<c> <Rt>, [<Rn>, #+/-<imm12>]!       ; p=0, w=1
    //
    // cccc010pu0w1nnnnttttiiiiiiiiiiii where cccc=Cond, tttt=Rt, nnnn=Rn,
    // iiiiiiiiiiii=imm12, u=1 if +, pu0w is a BlockAddr, and
    //
    // LDR (register) - ARM section A8.8.70, encoding A1:
    //   ldrb<c> <Rt>, [<Rn>, +/-<Rm>{, <shift>}]{!}
    //   ldrb<c> <Rt>, [<Rn>], +-<Rm>{, <shift>}
    //
    // cccc011pu0w1nnnnttttiiiiiss0mmmm where cccc=Cond, tttt=Rt, U=1 if +, pu0b
    // is a BlockAddr, and pu0w0nnnn0000iiiiiss0mmmm=Address.
    constexpr bool IsByte = false;
    emitMemOp(Cond, IsLoad, IsByte, Rt, OpAddress, TInfo, LdrName);
    return;
  }
  }
}

void AssemblerARM32::emitShift(const CondARM32::Cond Cond,
                               const OperandARM32::ShiftKind Shift,
                               const Operand *OpRd, const Operand *OpRm,
                               const Operand *OpSrc1, const bool SetFlags,
                               const char *InstName) {
  constexpr IValueT ShiftOpcode = B3 | B2 | B0; // 1101
  IValueT Rd = encodeRegister(OpRd, "Rd", InstName);
  IValueT Rm = encodeRegister(OpRm, "Rm", InstName);
  IValueT Value;
  switch (encodeOperand(OpSrc1, Value)) {
  default:
    llvm::report_fatal_error(std::string(InstName) +
                             ": Last operand not understood");
  case EncodedAsShiftImm5: {
    // XXX (immediate)
    //   xxx{s}<c> <Rd>, <Rm>, #imm5
    //
    // cccc0001101s0000ddddiiiii000mmmm where cccc=Cond, s=SetFlags, dddd=Rd,
    // iiiii=imm5, and mmmm=Rm.
    constexpr IValueT Rn = 0; // Rn field is not used.
    Value = Value | (Rm << kRmShift) | (Shift << kShiftShift);
    emitType01(Cond, kInstTypeDataRegShift, ShiftOpcode, SetFlags, Rn, Rd,
               Value, RdIsPcAndSetFlags, InstName);
    return;
  }
  case EncodedAsRegister: {
    // XXX (register)
    //   xxx{S}<c> <Rd>, <Rm>, <Rs>
    //
    // cccc0001101s0000ddddssss0001mmmm where cccc=Cond, s=SetFlags, dddd=Rd,
    // mmmm=Rm, and ssss=Rs.
    constexpr IValueT Rn = 0; // Rn field is not used.
    IValueT Rs = encodeRegister(OpSrc1, "Rs", InstName);
    verifyRegNotPc(Rd, "Rd", InstName);
    verifyRegNotPc(Rm, "Rm", InstName);
    verifyRegNotPc(Rs, "Rs", InstName);
    emitType01(Cond, kInstTypeDataRegShift, ShiftOpcode, SetFlags, Rn, Rd,
               encodeShiftRotateReg(Rm, Shift, Rs), NoChecks, InstName);
    return;
  }
  }
}

void AssemblerARM32::asr(const Operand *OpRd, const Operand *OpRm,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  constexpr const char *AsrName = "asr";
  emitShift(Cond, OperandARM32::ASR, OpRd, OpRm, OpSrc1, SetFlags, AsrName);
}

void AssemblerARM32::lsl(const Operand *OpRd, const Operand *OpRm,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  constexpr const char *LslName = "lsl";
  emitShift(Cond, OperandARM32::LSL, OpRd, OpRm, OpSrc1, SetFlags, LslName);
}

void AssemblerARM32::lsr(const Operand *OpRd, const Operand *OpRm,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  constexpr const char *LsrName = "lsr";
  emitShift(Cond, OperandARM32::LSR, OpRd, OpRm, OpSrc1, SetFlags, LsrName);
}

void AssemblerARM32::mov(const Operand *OpRd, const Operand *OpSrc,
                         CondARM32::Cond Cond) {
  // MOV (register) - ARM section A8.8.104, encoding A1:
  //   mov{S}<c> <Rd>, <Rn>
  //
  // cccc0001101s0000dddd00000000mmmm where cccc=Cond, s=SetFlags, dddd=Rd,
  // and nnnn=Rn.
  //
  // MOV (immediate) - ARM section A8.8.102, encoding A1:
  //   mov{S}<c> <Rd>, #<RotatedImm8>
  //
  // cccc0011101s0000ddddiiiiiiiiiiii where cccc=Cond, s=SetFlags, dddd=Rd,
  // and iiiiiiiiiiii=RotatedImm8=Src.  Note: We don't use movs in this
  // assembler.
  constexpr const char *MovName = "mov";
  IValueT Rd = encodeRegister(OpRd, "Rd", MovName);
  constexpr bool SetFlags = false;
  constexpr IValueT Rn = 0;
  constexpr IValueT MovOpcode = B3 | B2 | B0; // 1101.
  emitType01(Cond, MovOpcode, Rd, Rn, OpSrc, SetFlags, RdIsPcAndSetFlags,
             MovName);
}

void AssemblerARM32::emitMovwt(CondARM32::Cond Cond, bool IsMovW,
                               const Operand *OpRd, const Operand *OpSrc,
                               const char *MovName) {
  IValueT Opcode = B25 | B24 | (IsMovW ? 0 : B22);
  IValueT Rd = encodeRegister(OpRd, "Rd", MovName);
  IValueT Imm16;
  if (const auto *Src = llvm::dyn_cast<ConstantRelocatable>(OpSrc)) {
    emitFixup(createMoveFixup(IsMovW, Src));
    // Use 0 for the lower 16 bits of the relocatable, and add a fixup to
    // install the correct bits.
    Imm16 = 0;
  } else if (encodeOperand(OpSrc, Imm16) != EncodedAsConstI32) {
    llvm::report_fatal_error(std::string(MovName) + ": Not i32 constant");
  }
  verifyCondDefined(Cond, MovName);
  if (!Utils::IsAbsoluteUint(16, Imm16))
    llvm::report_fatal_error(std::string(MovName) + ": Constant not i16");
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  const IValueT Encoding = encodeCondition(Cond) << kConditionShift | Opcode |
                           ((Imm16 >> 12) << 16) | Rd << kRdShift |
                           (Imm16 & 0xfff);
  emitInst(Encoding);
}

void AssemblerARM32::movw(const Operand *OpRd, const Operand *OpSrc,
                          CondARM32::Cond Cond) {
  // MOV (immediate) - ARM section A8.8.102, encoding A2:
  //  movw<c> <Rd>, #<imm16>
  //
  // cccc00110000iiiiddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, and
  // iiiiiiiiiiiiiiii=imm16.
  constexpr const char *MovwName = "movw";
  constexpr bool IsMovW = true;
  emitMovwt(Cond, IsMovW, OpRd, OpSrc, MovwName);
}

void AssemblerARM32::movt(const Operand *OpRd, const Operand *OpSrc,
                          CondARM32::Cond Cond) {
  // MOVT - ARM section A8.8.106, encoding A1:
  //  movt<c> <Rd>, #<imm16>
  //
  // cccc00110100iiiiddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, and
  // iiiiiiiiiiiiiiii=imm16.
  constexpr const char *MovtName = "movt";
  constexpr bool IsMovW = false;
  emitMovwt(Cond, IsMovW, OpRd, OpSrc, MovtName);
}

void AssemblerARM32::mvn(const Operand *OpRd, const Operand *OpSrc,
                         CondARM32::Cond Cond) {
  // MVN (immediate) - ARM section A8.8.115, encoding A1:
  //   mvn{s}<c> <Rd>, #<const>
  //
  // cccc0011111s0000ddddiiiiiiiiiiii where cccc=Cond, s=SetFlags=0, dddd=Rd,
  // and iiiiiiiiiiii=const
  //
  // MVN (register) - ARM section A8.8.116, encoding A1:
  //   mvn{s}<c> <Rd>, <Rm>{, <shift>
  //
  // cccc0001111s0000ddddiiiiitt0mmmm where cccc=Cond, s=SetFlags=0, dddd=Rd,
  // mmmm=Rm, iiii defines shift constant, and tt=ShiftKind.
  constexpr const char *MvnName = "mvn";
  IValueT Rd = encodeRegister(OpRd, "Rd", MvnName);
  constexpr IValueT MvnOpcode = B3 | B2 | B1 | B0; // i.e. 1111
  constexpr IValueT Rn = 0;
  constexpr bool SetFlags = false;
  emitType01(Cond, MvnOpcode, Rd, Rn, OpSrc, SetFlags, RdIsPcAndSetFlags,
             MvnName);
}

void AssemblerARM32::nop() {
  // NOP - Section A8.8.119, encoding A1:
  //  nop<c>
  //
  // cccc0011001000001111000000000000 where cccc=Cond.
  AssemblerBuffer::EnsureCapacity ensured(&Buffer);
  constexpr CondARM32::Cond Cond = CondARM32::AL;
  const IValueT Encoding = (encodeCondition(Cond) << kConditionShift) | B25 |
                           B24 | B21 | B15 | B14 | B13 | B12;
  emitInst(Encoding);
}

void AssemblerARM32::sbc(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // SBC (register) - ARM section 18.8.162, encoding A1:
  //   sbc{s}<c> <Rd>, <Rn>, <Rm>{, <shift>}
  //
  // cccc0000110snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=Shift, tt=ShiftKind, and s=SetFlags.
  //
  // SBC (Immediate) - ARM section A8.8.161, encoding A1:
  //   sbc{s}<c> <Rd>, <Rn>, #<RotatedImm8>
  //
  // cccc0010110snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  constexpr const char *SbcName = "sbc";
  constexpr IValueT SbcOpcode = B2 | B1; // 0110
  emitType01(Cond, SbcOpcode, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             SbcName);
}

void AssemblerARM32::sdiv(const Operand *OpRd, const Operand *OpRn,
                          const Operand *OpSrc1, CondARM32::Cond Cond) {
  // SDIV - ARM section A8.8.165, encoding A1.
  //   sdiv<c> <Rd>, <Rn>, <Rm>
  //
  // cccc01110001dddd1111mmmm0001nnnn where cccc=Cond, dddd=Rd, nnnn=Rn, and
  // mmmm=Rm.
  constexpr const char *SdivName = "sdiv";
  IValueT Rd = encodeRegister(OpRd, "Rd", SdivName);
  IValueT Rn = encodeRegister(OpRn, "Rn", SdivName);
  IValueT Rm = encodeRegister(OpSrc1, "Rm", SdivName);
  verifyRegNotPc(Rd, "Rd", SdivName);
  verifyRegNotPc(Rn, "Rn", SdivName);
  verifyRegNotPc(Rm, "Rm", SdivName);
  // Assembler registers rd, rn, rm are encoded as rn, rm, rs.
  constexpr IValueT SdivOpcode = 0;
  emitDivOp(Cond, SdivOpcode, Rd, Rn, Rm, SdivName);
}

void AssemblerARM32::str(const Operand *OpRt, const Operand *OpAddress,
                         CondARM32::Cond Cond, const TargetInfo &TInfo) {
  constexpr const char *StrName = "str";
  constexpr bool IsLoad = false;
  IValueT Rt = encodeRegister(OpRt, "Rt", StrName);
  const Type Ty = OpRt->getType();
  switch (typeWidthInBytesLog2(Ty)) {
  case 3:
  // STRD is not implemented because target lowering handles i64 and double by
  // using two (32-bit) store instructions.  Note: Intenionally drop to
  // default case.
  default:
    llvm::report_fatal_error(std::string(StrName) + ": Type " + typeString(Ty) +
                             " not implemented");
  case 0: {
    // Handles i1 and i8 stores.
    //
    // STRB (immediate) - ARM section A8.8.207, encoding A1:
    //   strb<c> <Rt>, [<Rn>{, #+/-<imm12>}]     ; p=1, w=0
    //   strb<c> <Rt>, [<Rn>], #+/-<imm12>       ; p=1, w=1
    //   strb<c> <Rt>, [<Rn>, #+/-<imm12>]!      ; p=0, w=1
    //
    // cccc010pu1w0nnnnttttiiiiiiiiiiii where cccc=Cond, tttt=Rt, nnnn=Rn,
    // iiiiiiiiiiii=imm12, u=1 if +.
    constexpr bool IsByte = true;
    emitMemOp(Cond, IsLoad, IsByte, Rt, OpAddress, TInfo, StrName);
    return;
  }
  case 1: {
    // Handles i16 stores.
    //
    // STRH (immediate) - ARM section A8.*.217, encoding A1:
    //   strh<c> <Rt>, [<Rn>{, #+/-<Imm8>}]
    //   strh<c> <Rt>, [<Rn>], #+/-<Imm8>
    //   strh<c> <Rt>, [<Rn>, #+/-<Imm8>]!
    //
    // cccc000pu1w0nnnnttttiiii1011iiii where cccc=Cond, tttt=Rt, nnnn=Rn,
    // iiiiiiii=Imm8, u=1 if +, pu0w is a BlockAddr, and
    // pu0w0nnnn0000iiiiiiiiiiii=Address.
    constexpr const char *Strh = "strh";
    emitMemOpEnc3(Cond, B7 | B5 | B4, Rt, OpAddress, TInfo, Strh);
    return;
  }
  case 2: {
    // Note: Handles i32 and float stores. Target lowering handles i64 and
    // double by using two (32 bit) store instructions.
    //
    // STR (immediate) - ARM section A8.8.207, encoding A1:
    //   str<c> <Rt>, [<Rn>{, #+/-<imm12>}]     ; p=1, w=0
    //   str<c> <Rt>, [<Rn>], #+/-<imm12>       ; p=1, w=1
    //   str<c> <Rt>, [<Rn>, #+/-<imm12>]!      ; p=0, w=1
    //
    // cccc010pu1w0nnnnttttiiiiiiiiiiii where cccc=Cond, tttt=Rt, nnnn=Rn,
    // iiiiiiiiiiii=imm12, u=1 if +.
    constexpr bool IsByte = false;
    emitMemOp(Cond, IsLoad, IsByte, Rt, OpAddress, TInfo, StrName);
    return;
  }
  }
}

void AssemblerARM32::orr(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // ORR (register) - ARM Section A8.8.123, encoding A1:
  //   orr{s}<c> <Rd>, <Rn>, <Rm>
  //
  // cccc0001100snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=shift, tt=ShiftKind,, and s=SetFlags.
  //
  // ORR (register) - ARM Section A8.8.123, encoding A1:
  //   orr{s}<c> <Rd>, <Rn>,  #<RotatedImm8>
  //
  // cccc0001100snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8.
  constexpr const char *OrrName = "orr";
  constexpr IValueT OrrOpcode = B3 | B2; // i.e. 1100
  emitType01(Cond, OrrOpcode, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             OrrName);
}

void AssemblerARM32::pop(const Operand *OpRt, CondARM32::Cond Cond) {
  // POP - ARM section A8.8.132, encoding A2:
  //   pop<c> {Rt}
  //
  // cccc010010011101dddd000000000100 where dddd=Rt and cccc=Cond.
  constexpr const char *Pop = "pop";
  IValueT Rt = encodeRegister(OpRt, "Rt", Pop);
  verifyRegsNotEq(Rt, "Rt", RegARM32::Encoded_Reg_sp, "sp", Pop);
  // Same as load instruction.
  constexpr bool IsLoad = true;
  constexpr bool IsByte = false;
  IValueT Address = encodeImmRegOffset(RegARM32::Encoded_Reg_sp, kWordSize,
                                       OperandARM32Mem::PostIndex);
  emitMemOp(Cond, kInstTypeMemImmediate, IsLoad, IsByte, Rt, Address, Pop);
}

void AssemblerARM32::popList(const IValueT Registers, CondARM32::Cond Cond) {
  // POP - ARM section A8.*.131, encoding A1:
  //   pop<c> <registers>
  //
  // cccc100010111101rrrrrrrrrrrrrrrr where cccc=Cond and
  // rrrrrrrrrrrrrrrr=Registers (one bit for each GP register).
  constexpr const char *PopListName = "pop {}";
  constexpr bool IsLoad = true;
  emitMultiMemOp(Cond, IA_W, IsLoad, RegARM32::Encoded_Reg_sp, Registers,
                 PopListName);
}

void AssemblerARM32::push(const Operand *OpRt, CondARM32::Cond Cond) {
  // PUSH - ARM section A8.8.133, encoding A2:
  //   push<c> {Rt}
  //
  // cccc010100101101dddd000000000100 where dddd=Rt and cccc=Cond.
  constexpr const char *Push = "push";
  IValueT Rt = encodeRegister(OpRt, "Rt", Push);
  verifyRegsNotEq(Rt, "Rt", RegARM32::Encoded_Reg_sp, "sp", Push);
  // Same as store instruction.
  constexpr bool isLoad = false;
  constexpr bool isByte = false;
  IValueT Address = encodeImmRegOffset(RegARM32::Encoded_Reg_sp, -kWordSize,
                                       OperandARM32Mem::PreIndex);
  emitMemOp(Cond, kInstTypeMemImmediate, isLoad, isByte, Rt, Address, Push);
}

void AssemblerARM32::pushList(const IValueT Registers, CondARM32::Cond Cond) {
  // PUSH - ARM section A8.8.133, encoding A1:
  //   push<c> <Registers>
  //
  // cccc100100101101rrrrrrrrrrrrrrrr where cccc=Cond and
  // rrrrrrrrrrrrrrrr=Registers (one bit for each GP register).
  constexpr const char *PushListName = "push {}";
  constexpr bool IsLoad = false;
  emitMultiMemOp(Cond, DB_W, IsLoad, RegARM32::Encoded_Reg_sp, Registers,
                 PushListName);
}

void AssemblerARM32::mla(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpRm, const Operand *OpRa,
                         CondARM32::Cond Cond) {
  // MLA - ARM section A8.8.114, encoding A1.
  //   mla{s}<c> <Rd>, <Rn>, <Rm>, <Ra>
  //
  // cccc0000001sddddaaaammmm1001nnnn where cccc=Cond, s=SetFlags, dddd=Rd,
  // aaaa=Ra, mmmm=Rm, and nnnn=Rn.
  constexpr const char *MlaName = "mla";
  IValueT Rd = encodeRegister(OpRd, "Rd", MlaName);
  IValueT Rn = encodeRegister(OpRn, "Rn", MlaName);
  IValueT Rm = encodeRegister(OpRm, "Rm", MlaName);
  IValueT Ra = encodeRegister(OpRa, "Ra", MlaName);
  verifyRegNotPc(Rd, "Rd", MlaName);
  verifyRegNotPc(Rn, "Rn", MlaName);
  verifyRegNotPc(Rm, "Rm", MlaName);
  verifyRegNotPc(Ra, "Ra", MlaName);
  constexpr IValueT MlaOpcode = B21;
  constexpr bool SetFlags = false;
  // Assembler registers rd, rn, rm, ra are encoded as rn, rm, rs, rd.
  emitMulOp(Cond, MlaOpcode, Ra, Rd, Rn, Rm, SetFlags, MlaName);
}

void AssemblerARM32::mul(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // MUL - ARM section A8.8.114, encoding A1.
  //   mul{s}<c> <Rd>, <Rn>, <Rm>
  //
  // cccc0000000sdddd0000mmmm1001nnnn where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, and s=SetFlags.
  constexpr const char *MulName = "mul";
  IValueT Rd = encodeRegister(OpRd, "Rd", MulName);
  IValueT Rn = encodeRegister(OpRn, "Rn", MulName);
  IValueT Rm = encodeRegister(OpSrc1, "Rm", MulName);
  verifyRegNotPc(Rd, "Rd", MulName);
  verifyRegNotPc(Rn, "Rn", MulName);
  verifyRegNotPc(Rm, "Rm", MulName);
  // Assembler registers rd, rn, rm are encoded as rn, rm, rs.
  constexpr IValueT MulOpcode = 0;
  emitMulOp(Cond, MulOpcode, RegARM32::Encoded_Reg_r0, Rd, Rn, Rm, SetFlags,
            MulName);
}

void AssemblerARM32::rsb(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // RSB (immediate) - ARM section A8.8.152, encoding A1.
  //   rsb{s}<c> <Rd>, <Rn>, #<RotatedImm8>
  //
  // cccc0010011snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=setFlags and iiiiiiiiiiii defines the RotatedImm8 value.
  //
  // RSB (register) - ARM section A8.8.163, encoding A1.
  //   rsb{s}<c> <Rd>, <Rn>, <Rm>{, <Shift>}
  //
  // cccc0000011snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=shift, tt==ShiftKind, and s=SetFlags.
  constexpr const char *RsbName = "rsb";
  constexpr IValueT RsbOpcode = B1 | B0; // 0011
  emitType01(Cond, RsbOpcode, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             RsbName);
}

void AssemblerARM32::sxt(const Operand *OpRd, const Operand *OpSrc0,
                         CondARM32::Cond Cond) {
  constexpr const char *SxtName = "sxt";
  constexpr IValueT SxtOpcode = B26 | B25 | B23 | B21;
  emitSignExtend(Cond, SxtOpcode, OpRd, OpSrc0, SxtName);
}

void AssemblerARM32::sub(const Operand *OpRd, const Operand *OpRn,
                         const Operand *OpSrc1, bool SetFlags,
                         CondARM32::Cond Cond) {
  // SUB (register) - ARM section A8.8.223, encoding A1:
  //   sub{s}<c> <Rd>, <Rn>, <Rm>{, <shift>}
  // SUB (SP minus register): See ARM section 8.8.226, encoding A1:
  //   sub{s}<c> <Rd>, sp, <Rm>{, <Shift>}
  //
  // cccc0000010snnnnddddiiiiitt0mmmm where cccc=Cond, dddd=Rd, nnnn=Rn,
  // mmmm=Rm, iiiii=shift, tt=ShiftKind, and s=SetFlags.
  //
  // Sub (Immediate) - ARM section A8.8.222, encoding A1:
  //    sub{s}<c> <Rd>, <Rn>, #<RotatedImm8>
  // Sub (Sp minus immediate) - ARM section A8.*.225, encoding A1:
  //    sub{s}<c> sp, <Rn>, #<RotatedImm8>
  //
  // cccc0010010snnnnddddiiiiiiiiiiii where cccc=Cond, dddd=Rd, nnnn=Rn,
  // s=SetFlags and iiiiiiiiiiii=Src1Value defining RotatedImm8
  constexpr const char *SubName = "sub";
  constexpr IValueT SubOpcode = B1; // 0010
  emitType01(Cond, SubOpcode, OpRd, OpRn, OpSrc1, SetFlags, RdIsPcAndSetFlags,
             SubName);
}

void AssemblerARM32::tst(const Operand *OpRn, const Operand *OpSrc1,
                         CondARM32::Cond Cond) {
  // TST (register) - ARM section A8.8.241, encoding A1:
  //   tst<c> <Rn>, <Rm>(, <shift>}
  //
  // cccc00010001nnnn0000iiiiitt0mmmm where cccc=Cond, nnnn=Rn, mmmm=Rm,
  // iiiii=Shift, and tt=ShiftKind.
  //
  // TST (immediate) - ARM section A8.8.240, encoding A1:
  //   tst<c> <Rn>, #<RotatedImm8>
  //
  // cccc00110001nnnn0000iiiiiiiiiiii where cccc=Cond, nnnn=Rn, and
  // iiiiiiiiiiii defines RotatedImm8.
  constexpr const char *TstName = "tst";
  constexpr IValueT TstOpcode = B3; // ie. 1000
  emitCompareOp(Cond, TstOpcode, OpRn, OpSrc1, TstName);
}

void AssemblerARM32::udiv(const Operand *OpRd, const Operand *OpRn,
                          const Operand *OpSrc1, CondARM32::Cond Cond) {
  // UDIV - ARM section A8.8.248, encoding A1.
  //   udiv<c> <Rd>, <Rn>, <Rm>
  //
  // cccc01110011dddd1111mmmm0001nnnn where cccc=Cond, dddd=Rd, nnnn=Rn, and
  // mmmm=Rm.
  constexpr const char *UdivName = "udiv";
  IValueT Rd = encodeRegister(OpRd, "Rd", UdivName);
  IValueT Rn = encodeRegister(OpRn, "Rn", UdivName);
  IValueT Rm = encodeRegister(OpSrc1, "Rm", UdivName);
  verifyRegNotPc(Rd, "Rd", UdivName);
  verifyRegNotPc(Rn, "Rn", UdivName);
  verifyRegNotPc(Rm, "Rm", UdivName);
  // Assembler registers rd, rn, rm are encoded as rn, rm, rs.
  constexpr IValueT UdivOpcode = B21;
  emitDivOp(Cond, UdivOpcode, Rd, Rn, Rm, UdivName);
}

void AssemblerARM32::umull(const Operand *OpRdLo, const Operand *OpRdHi,
                           const Operand *OpRn, const Operand *OpRm,
                           CondARM32::Cond Cond) {
  // UMULL - ARM section A8.8.257, encoding A1:
  //   umull<c> <RdLo>, <RdHi>, <Rn>, <Rm>
  //
  // cccc0000100shhhhllllmmmm1001nnnn where hhhh=RdHi, llll=RdLo, nnnn=Rn,
  // mmmm=Rm, and s=SetFlags
  constexpr const char *UmullName = "umull";
  IValueT RdLo = encodeRegister(OpRdLo, "RdLo", UmullName);
  IValueT RdHi = encodeRegister(OpRdHi, "RdHi", UmullName);
  IValueT Rn = encodeRegister(OpRn, "Rn", UmullName);
  IValueT Rm = encodeRegister(OpRm, "Rm", UmullName);
  verifyRegNotPc(RdLo, "RdLo", UmullName);
  verifyRegNotPc(RdHi, "RdHi", UmullName);
  verifyRegNotPc(Rn, "Rn", UmullName);
  verifyRegNotPc(Rm, "Rm", UmullName);
  verifyRegsNotEq(RdHi, "RdHi", RdLo, "RdLo", UmullName);
  constexpr IValueT UmullOpcode = B23;
  constexpr bool SetFlags = false;
  emitMulOp(Cond, UmullOpcode, RdLo, RdHi, Rn, Rm, SetFlags, UmullName);
}

void AssemblerARM32::uxt(const Operand *OpRd, const Operand *OpSrc0,
                         CondARM32::Cond Cond) {
  constexpr const char *UxtName = "uxt";
  constexpr IValueT UxtOpcode = B26 | B25 | B23 | B22 | B21;
  emitSignExtend(Cond, UxtOpcode, OpRd, OpSrc0, UxtName);
}

} // end of namespace ARM32
} // end of namespace Ice
