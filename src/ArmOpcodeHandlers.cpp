#include <bitset>

#include "ARM7TDMI.h"
#include "Bus.h"

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::multiplyHandler(
    uint32_t instruction, ARM7TDMI *cpu) {
    uint8_t opcode = getOpcode(instruction);
    // rd is different for multiply
    uint8_t rd = (instruction & 0x000F0000) >> 16;
    uint8_t rm = getRm(instruction);
    uint8_t rs = getRs(instruction);
    assert(rd != rm &&
           (rd != PC_REGISTER && rm != PC_REGISTER && rs != PC_REGISTER));
    assert((instruction & 0x000000F0) == 0x00000090);
    uint64_t result;
    BitPreservedInt64 longResult;

    switch (opcode) {
        case 0b0000: {  // MUL
            result =
                (uint64_t)cpu->getRegister(rm) * (uint64_t)cpu->getRegister(rs);
            cpu->setRegister(rd, (uint32_t)result);
            break;
        }
        case 0b0001: {  // MLA
            // rn is different for multiply
            uint8_t rn = (instruction & 0x0000F000) >> 12;
            assert(rn != PC_REGISTER);
            result = (uint64_t)cpu->getRegister(rm) *
                         (uint64_t)cpu->getRegister(rs) +
                     (uint64_t)cpu->getRegister(rn);
            cpu->setRegister(rd, (uint32_t)result);
            break;
        }
        case 0b0100: {  // UMULL{cond}{S} RdLo,RdHi,Rm,Rs ;RdHiLo=Rm*Rs
            uint8_t rdhi = rd;
            uint8_t rdlo = (instruction & 0x0000F000) >> 12;
            longResult._unsigned =
                (uint64_t)cpu->getRegister(rm) * (uint64_t)cpu->getRegister(rs);
            // high destination reg
            cpu->setRegister(rdhi, (uint32_t)(longResult._unsigned >> 32));
            cpu->setRegister(rdlo, (uint32_t)longResult._unsigned);
            break;
        }
        case 0b0101: {  // UMLAL {cond}{S} RdLo,RdHi,Rm,Rs ;RdHiLo=Rm*Rs+RdHiLo
            uint8_t rdhi = rd;
            uint8_t rdlo = (instruction & 0x0000F000) >> 12;
            longResult._unsigned =
                (uint64_t)cpu->getRegister(rm) *
                    (uint64_t)cpu->getRegister(rs) +
                ((((uint64_t)(cpu->getRegister(rdhi))) << 32) |
                 ((uint64_t)(cpu->getRegister(rdlo))));
            // high destination reg
            cpu->setRegister(rdhi, (uint32_t)(longResult._unsigned >> 32));
            cpu->setRegister(rdlo, (uint32_t)longResult._unsigned);
            break;
        }
        case 0b0110: {  // SMULL {cond}{S} RdLo,RdHi,Rm,Rs ;RdHiLo=Rm*Rs
            uint8_t rdhi = rd;
            uint8_t rdlo = (instruction & 0x0000F000) >> 12;
            BitPreservedInt32 rmVal;
            BitPreservedInt32 rsVal;
            rmVal._unsigned = cpu->getRegister(rm);
            rsVal._unsigned = cpu->getRegister(rs);
            longResult._signed =
                (int64_t)rmVal._signed * (int64_t)rsVal._signed;
            // high destination reg
            cpu->setRegister(rdhi, (uint32_t)(longResult._unsigned >> 32));
            cpu->setRegister(rdlo, (uint32_t)longResult._unsigned);
            break;
        }
        case 0b0111: {  // SMLAL{cond}{S} RdLo,RdHi,Rm,Rs ;RdHiLo=Rm*Rs+RdHiLo
            uint8_t rdhi = rd;
            uint8_t rdlo = (instruction & 0x0000F000) >> 12;
            BitPreservedInt32 rmVal;
            BitPreservedInt32 rsVal;
            rmVal._unsigned = cpu->getRegister(rm);
            rsVal._unsigned = cpu->getRegister(rs);
            BitPreservedInt64 accum;
            accum._unsigned = ((((uint64_t)(cpu->getRegister(rdhi))) << 32) |
                               ((uint64_t)(cpu->getRegister(rdlo))));
            longResult._signed =
                (int64_t)rmVal._signed * (int64_t)rsVal._signed + accum._signed;
            // high destination reg
            cpu->setRegister(rdhi, (uint32_t)(longResult._unsigned >> 32));
            cpu->setRegister(rdlo, (uint32_t)longResult._unsigned);
            break;
        }
    }

    if (sFlagSet(instruction)) {
        if (!(opcode & 0b0100)) {  // regular mult opcode,
            cpu->cpsr.Z = aluSetsZeroBit((uint32_t)result);
            cpu->cpsr.N = aluSetsSignBit((uint32_t)result);
        } else {
            cpu->cpsr.Z = (longResult._unsigned == 0);
            cpu->cpsr.N = (longResult._unsigned >> 63);
        }
        cpu->cpsr.C = 0;
    }
    return {};
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ PSR Transfer (MRS, MSR) Operations
 * ~~~~~~~~~~~~~~~~~~~~*/

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::psrHandler(uint32_t instruction,
                                                         ARM7TDMI *cpu) {
    assert(!(instruction & 0x0C000000));
    assert(!sFlagSet(instruction));
    // bit 25: I - Immediate Operand Flag
    // (0=Register, 1=Immediate) (Zero for MRS)
    bool immediate = (instruction & 0x02000000);
    // bit 22: Psr - Source/Destination PSR
    // (0=CPSR, 1=SPSR_<current mode>)
    bool psrSource = (instruction & 0x00400000);

    // bit 21: special opcode for PSR
    switch ((instruction & 0x00200000) >> 21) {
        case 0: {  // MRS{cond} Rd,Psr ; Rd = Psr
            assert(!immediate);
            assert(getRn(instruction) == 0xF);
            assert(!(instruction & 0x00000FFF));
            uint8_t rd = getRd(instruction);
            if (psrSource) {
                cpu->setRegister(rd, psrToInt(cpu->cpsr));
            } else {
                cpu->setRegister(rd, psrToInt(*(cpu->getCurrentModeSpsr())));
            }
            break;
        }
        case 1: {  // MSR{cond} Psr{_field},Op  ;Psr[field] = Op=
            assert((instruction & 0x0000F000) == 0x0000F000);
            uint8_t fscx = (instruction & 0x000F0000) >> 16;
            ProgramStatusRegister *psr =
                (psrSource ? &(cpu->cpsr) : cpu->getCurrentModeSpsr());
            if (immediate) {
                uint32_t immValue = (uint32_t)(instruction & 0x000000FF);
                uint8_t shift = (instruction & 0x00000F00) >> 7;
                cpu->transferToPsr(aluShiftRor(immValue, shift), fscx, psr);
            } else {  // register
                assert(!(instruction & 0x00000FF0));
                assert(getRm(instruction) != PC_REGISTER);
                cpu->transferToPsr(cpu->getRegister(getRm(instruction)), fscx,
                                   psr);
            }
            break;
        }
    }

    return {};
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ALU OPERATIONS
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// TODO: put assertions for every unexpected or unallowed circumstance (for
// deubgging)
// TODO: cycle calculation
/*
    0: AND{cond}{S} Rd,Rn,Op2    ;AND logical       Rd = Rn AND Op2
    1: EOR{cond}{S} Rd,Rn,Op2    ;XOR logical       Rd = Rn XOR Op2
    2: SUB{cond}{S} Rd,Rn,Op2 ;* ;subtract          Rd = Rn-Op2
    3: RSB{cond}{S} Rd,Rn,Op2 ;* ;subtract reversed Rd = Op2-Rn
    4: ADD{cond}{S} Rd,Rn,Op2 ;* ;add               Rd = Rn+Op2
    5: ADC{cond}{S} Rd,Rn,Op2 ;* ;add with carry    Rd = Rn+Op2+Cy
    6: SBC{cond}{S} Rd,Rn,Op2 ;* ;sub with carry    Rd = Rn-Op2+Cy-1
    7: RSC{cond}{S} Rd,Rn,Op2 ;* ;sub cy. reversed  Rd = Op2-Rn+Cy-1
    8: TST{cond}{P}    Rn,Op2    ;test            Void = Rn AND Op2
    9: TEQ{cond}{P}    Rn,Op2    ;test exclusive  Void = Rn XOR Op2
    A: CMP{cond}{P}    Rn,Op2 ;* ;compare         Void = Rn-Op2
    B: CMN{cond}{P}    Rn,Op2 ;* ;compare neg.    Void = Rn+Op2
    C: ORR{cond}{S} Rd,Rn,Op2    ;OR logical        Rd = Rn OR Op2
    D: MOV{cond}{S} Rd,Op2       ;move              Rd = Op2
    E: BIC{cond}{S} Rd,Rn,Op2    ;bit clear         Rd = Rn AND NOT Op2
    F: MVN{cond}{S} Rd,Op2       ;not               Rd = NOT Op2
*/

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::dataProcHandler(
    uint32_t instruction, ARM7TDMI *cpu) {
    // shift op2
    AluShiftResult shiftResult = cpu->aluShift(
        instruction, (instruction & 0x02000000), (instruction & 0x00000010));
    uint8_t rd = getRd(instruction);
    uint8_t rn = getRn(instruction);
    uint8_t opcode = getOpcode(instruction);
    bool carryBit = (cpu->cpsr).C;
    bool overflowBit = (cpu->cpsr).V;
    bool signBit = (cpu->cpsr).N;
    bool zeroBit = (cpu->cpsr).Z;

    uint32_t rnValue;
    // if rn == pc regiser, have to add to it to account for pipelining /
    // prefetching
    // TODO probably dont need this logic if pipelining is emulated
    if (rn != PC_REGISTER) {
        rnValue = cpu->getRegister(rn);
    } else if (!(instruction & 0x02000000) && (instruction & 0x00000010)) {
        rnValue = cpu->getRegister(rn) + 12;
    } else {
        rnValue = cpu->getRegister(rn) + 8;
    }
    uint32_t op2 = shiftResult.op2;

    uint32_t rnVal = cpu->getRegister(rn);

    switch (opcode) {
        case AND: {  // AND
            uint32_t result = rnVal & op2;
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case EOR: {  // EOR
            uint32_t result = rnVal ^ op2;
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case SUB: {  // SUB
            uint32_t result = rnVal - op2;
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(rnVal, op2);
            overflowBit = aluSubtractSetsOverflowBit(rnVal, op2, result);
            break;
        }
        case RSB: {  // RSB
            uint32_t result = op2 - rnVal;
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(op2, rnVal);
            overflowBit = aluSubtractSetsOverflowBit(op2, rnVal, result);
            break;
        }
        case ADD: {  // ADD
            uint32_t result = rnVal + op2;
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluAddSetsCarryBit(rnVal, op2);
            overflowBit = aluAddSetsOverflowBit(rnVal, op2, result);
            break;
        }
        case ADC: {  // ADC
            uint64_t result = (uint64_t)(rnVal + op2 + (cpu->cpsr).C);
            cpu->setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluAddWithCarrySetsCarryBit(result);
            overflowBit = aluAddWithCarrySetsOverflowBit(rnVal, op2,
                                                         (uint32_t)result, cpu);
            break;
        }
        case SBC: {  // SBC
            uint64_t result = rnVal + (~op2) + (cpu->cpsr).C;
            cpu->setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluSubWithCarrySetsCarryBit(result);
            overflowBit = aluSubWithCarrySetsOverflowBit(rnVal, op2,
                                                         (uint32_t)result, cpu);
            break;
        }
        case RSC: {  // RSC
            uint64_t result = op2 + (~rnVal) + (cpu->cpsr).C;
            cpu->setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluSubWithCarrySetsCarryBit(result);
            overflowBit = aluSubWithCarrySetsOverflowBit(op2, rnVal,
                                                         (uint32_t)result, cpu);
            break;
        }
        case TST: {  // TST
            uint32_t result = rnVal & op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case TEQ: {  // TEQ
            uint32_t result = rnVal ^ op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case CMP: {  // CMP
            uint32_t result = rnVal - op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(rnVal, op2);
            overflowBit = aluSubtractSetsOverflowBit(rnVal, op2, result);
            break;
        }
        case CMN: {  // CMN
            uint32_t result = rnVal + op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluAddSetsCarryBit(rnVal, op2);
            overflowBit = aluAddSetsOverflowBit(rnVal, op2, result);
            break;
        }
        case ORR: {  // ORR
            uint32_t result = rnVal | op2;
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case MOV: {  // MOV
            uint32_t result = op2;
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case BIC: {  // BIC
            uint32_t result = rnVal & (~op2);
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
        case MVN: {  // MVN
            uint32_t result = ~op2;
            cpu->setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            break;
        }
    }

    if (rd != PC_REGISTER && sFlagSet(instruction)) {
        cpu->cpsr.C = carryBit;
        cpu->cpsr.Z = zeroBit;
        cpu->cpsr.N = signBit;
        cpu->cpsr.V = overflowBit;
    } else if (rd == PC_REGISTER && sFlagSet(instruction)) {
        cpu->cpsr = *(cpu->getCurrentModeSpsr());
    } else {
    }  // flags not affected, not allowed in CMP

    return {};
}

/* ~~~~~~~~ SINGLE DATA TRANSFER ~~~~~~~~~*/

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::singleDataTransHandler(
    uint32_t instruction, ARM7TDMI *cpu) {
    // TODO:  implement the following restriction <expression>  ;an immediate
    // used as address
    // ;*** restriction: must be located in range PC+/-4095+8, if so,
    // ;*** assembler will calculate offset and use PC (R15) as base.

    assert((instruction & 0x0C000000) == (instruction & 0x04000000));
    uint8_t rd = getRd(instruction);
    uint32_t rdVal =
        (rd == 15) ? cpu->getRegister(rd) + 12 : cpu->getRegister(rd);
    uint8_t rn = getRn(instruction);
    uint32_t rnVal =
        (rn == 15) ? cpu->getRegister(rn) + 8 : cpu->getRegister(rn);

    uint32_t offset;
    // I - Immediate Offset Flag (0=Immediate, 1=Shifted Register)
    if ((instruction & 0x02000000)) {
        // Register shifted by Immediate as Offset
        assert(!(instruction & 0x00000010));  // bit 4 Must be 0 (Reserved, see
                                              // The Undefined Instruction)
        uint8_t rm = getRm(instruction);
        assert(rm != 15);
        uint8_t shiftAmount = (instruction & 0x00000F80) >> 7;
        switch ((instruction & 0x00000060) >> 5 /*shift type*/) {
            case 0: {  // LSL
                offset = shiftAmount != 0
                             ? aluShiftLsl(cpu->getRegister(rm), shiftAmount)
                             : cpu->getRegister(rm);
                break;
            }
            case 1: {  // LSR
                offset = shiftAmount != 0
                             ? aluShiftLsr(cpu->getRegister(rm), shiftAmount)
                             : 0;
                break;
            }
            case 2: {  // ASR
                offset = shiftAmount != 0
                             ? aluShiftAsr(cpu->getRegister(rm), shiftAmount)
                             : aluShiftAsr(cpu->getRegister(rm), 32);
                break;
            }
            case 4: {  // ROR
                offset =
                    shiftAmount != 0
                        ? aluShiftRor(cpu->getRegister(rm), shiftAmount % 32)
                        : aluShiftRrx(cpu->getRegister(rm), 1, cpu);
                break;
            }
        }
    } else {  // immediate as offset (12 bit offset)
        offset = instruction & 0x00000FFF;
    }

    uint32_t address = rnVal;
    // U - Up/Down Bit (0=down; subtract offset
    // from base, 1=up; add to base)
    bool u = dataTransGetU(instruction);

    // P - Pre/Post (0=post; add offset after
    // transfer, 1=pre; before trans.)
    bool p = dataTransGetP(instruction);

    if (p) {  // add offset before transfer
        address = u ? address + offset : address - offset;
        if (dataTransGetW(instruction)) {
            // write address back into base register
            cpu->setRegister(rn, address);
        }
    }

    bool b = dataTransGetB(instruction);  // B - Byte/Word bit (0=transfer
                                          // 32bit/word, 1=transfer 8bit/byte)
    // TODO implement t bit, force non-privilege access
    // L - Load/Store bit (0=Store to memory, 1=Load from memory)
    if (dataTransGetL(instruction)) {
        // LDR{cond}{B}{T} Rd,<Address> ;Rd=[Rn+/-<offset>]
        if (b) {  // transfer 8 bits
            cpu->setRegister(rd, (uint32_t)(cpu->bus->read8(address)));
        } else {  // transfer 32 bits
            if ((address & 0x00000003) != 0 && (address & 0x00000001) == 0) {
                // aligned to half-word but not word
                uint32_t low =
                    (uint32_t)(cpu->bus->read16(address & 0xFFFFFFFE));
                uint32_t hi =
                    (uint32_t)(cpu->bus->read16((address - 2) & 0xFFFFFFFE));
                uint32_t full = ((hi << 16) | low);
                cpu->setRegister(
                    rd, aluShiftRor(cpu->bus->read32(full & 0xFFFFFFFC),
                                    (full & 3) * 8));
            } else {
                // aligned to word
                // Reads from forcibly aligned address "addr AND (NOT 3)",
                // and does then rotate the data as "ROR (addr AND 3)*8". T
                cpu->setRegister(
                    rd, aluShiftRor(cpu->bus->read32(address & 0xFFFFFFFC),
                                    (address & 3) * 8));
            }
        }
    } else {
        // STR{cond}{B}{T} Rd,<Address>   ;[Rn+/-<offset>]=Rd
        if (b) {  // transfer 8 bits
            cpu->bus->write8(address, (uint8_t)(rdVal));
        } else {  // transfer 32 bits
            cpu->bus->write32(address & 0xFFFFFFFC, (rdVal));
        }
    }

    if (!p) {
        // add offset after transfer and always write back
        address = u ? address + offset : address - offset;
        cpu->setRegister(rn, address);
    }
    return {};
}

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::halfWordDataTransHandler(
    uint32_t instruction, ARM7TDMI *cpu) {
    assert((instruction & 0x0E000000) == 0);
    uint8_t rd = getRd(instruction);
    uint32_t rdVal =
        (rd == 15) ? cpu->getRegister(rd) + 12 : cpu->getRegister(rd);
    uint8_t rn = getRn(instruction);
    uint32_t rnVal =
        (rn == 15) ? cpu->getRegister(rn) + 8 : cpu->getRegister(rn);

    uint32_t offset = 0;

    bool l = dataTransGetL(instruction);

    if (instruction & 0x00400000) {
        // immediate as offset
        offset = (((instruction & 0x00000F00) >> 4) | getRm(instruction));

    } else {
        // register as offset
        assert(!(instruction & 0x00000F00));
        assert(getRm(instruction) != 15);
        offset = cpu->getRegister(getRm(instruction));
    }
    assert(instruction & 0x00000080);
    assert(instruction & 0x00000010);

    uint32_t address = rnVal;
    uint8_t p = dataTransGetP(instruction);
    if (p) {
        // pre-indexing offset
        address =
            dataTransGetU(instruction) ? address + offset : address - offset;
        if (dataTransGetW(instruction)) {
            // write address back into base register
            cpu->setRegister(rn, address);
        }
    } else {
        // post-indexing offset
        assert(dataTransGetW(instruction) == 0);
    }

    uint8_t opcode = (instruction & 0x00000060) >> 5;
    switch (opcode) {
        case 0: {
            // Reserved for SWP instruction
            assert(false);
        }
        case 1: {     // STRH or LDRH (depending on l)
            if (l) {  // LDR{cond}H  Rd,<Address>  ;Load Unsigned halfword
                      // (zero-extended)
                cpu->setRegister(
                    rd, aluShiftRor(
                            (uint32_t)(cpu->bus->read16(address & 0xFFFFFFFE)),
                            (address & 1) * 8));
            } else {  // STR{cond}H  Rd,<Address>  ;Store halfword   [a]=Rd
                cpu->bus->write16(address & 0xFFFFFFFE, (uint16_t)rdVal);
            }
        }
        case 2: {  // LDR{cond}SB Rd,<Address>  ;Load Signed byte (sign
                   // extended)
            // TODO: better way to do this?
            assert(l);
            uint32_t val = (uint32_t)(cpu->bus->read8(address));
            if (val & 0x00000080) {
                cpu->setRegister(rd, 0xFFFFFF00 | val);
            } else {
                cpu->setRegister(rd, val);
            }
        }
        case 3: {  // LDR{cond}SH Rd,<Address>  ;Load Signed halfword (sign
                   // extended)
                   // LDRSH Rd,[odd]  -->  LDRSB Rd,[odd] ;sign-expand BYTE value
            assert(l);
            uint32_t val = (uint32_t)(cpu->bus->read8(address));
            if (val & 0x00000080) {
                cpu->setRegister(rd, 0xFFFFFF00 | val);
            } else {
                cpu->setRegister(rd, val);
            }
        }
    }

    if (!p) {
        // add offset after transfer and always write back
        address =
            dataTransGetU(instruction) ? address + offset : address - offset;
        cpu->setRegister(rn, address);
    }
}

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::singleDataSwapHandler(
    // TODO: figure out memory alignment logic (for all data transfer ops)
    // (verify against existing CPU implementations)
    uint32_t instruction, ARM7TDMI *cpu) {
    assert((instruction & 0x0F800000) == 0x01000000);
    assert(!(instruction & 0x00300000));
    assert((instruction & 0x00000FF0) == 0x00000090);
    bool b = dataTransGetB(instruction);
    uint8_t rn = getRn(instruction);
    uint8_t rd = getRd(instruction);
    uint8_t rm = getRm(instruction);
    assert((rn != 15) && (rd != 15) && (rm != 15));

    // SWP{cond}{B} Rd,Rm,[Rn]     ;Rd=[Rn], [Rn]=Rm
    if (b) {
        // SWPB swap byte
        uint32_t rnVal = cpu->getRegister(rn);
        uint8_t data = cpu->bus->read8(rnVal);
        cpu->setRegister(rd, (uint32_t)data);
        // TODO: check, is it a zero-extended 32-bit write or an 8-bit write?
        uint8_t rmVal = (uint8_t)(cpu->getRegister(rm));
        cpu->bus->write8(rnVal, rmVal);
    } else {
        // SWPB swap word
        // The SWP opcode works like a combination of LDR and STR, that means, 
        // it does read-rotated, but does write-unrotated.
        uint32_t rnVal = cpu->getRegister(rn);
        uint32_t data = cpu->bus->read32(aluShiftRor(rnVal & 0xFFFFFFFC, (rnVal & 3) * 8));
        cpu->setRegister(rd, data);
        uint32_t rmVal = cpu->getRegister(rm);
        cpu->bus->write32(rnVal & 0xFFFFFFFC, rmVal);
    }
}

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::blockDataTransHandler(
    uint32_t instruction, ARM7TDMI *cpu) {
    // TODO: data aborts (if even applicable to GBA?)
    assert((instruction & 0x0E000000) == 0x08000000);
    // base register
    uint8_t rn = getRn(instruction);
    // align memory address;
    uint32_t rnVal = cpu->getRegister(rn);
    assert(rn != 15);
    bool p = dataTransGetP(instruction);
    bool u = dataTransGetU(instruction);
    bool l = dataTransGetL(instruction);
    bool w = dataTransGetW(instruction);
    // special case for block transfer, s = what is usually b
    bool s = dataTransGetB(instruction);
    if (s) assert(cpu->cpsr.Mode != USER);
    uint16_t regList = (uint16_t)instruction;
    uint32_t addressRnStoredAt = 0;  // see below

    if (u) {
        // up, add offset to base
        if (p) {
            // pre-increment addressing
            rnVal += 4;
        }
        for (int reg = 0; reg < 16; reg++) {
            if (regList & 1) {
                if (l) {
                    // LDM{cond}{amod} Rn{!},<Rlist>{^}  ;Load  (Pop)
                    uint32_t data = cpu->bus->read32(rnVal & 0xFFFFFFFC);
                    (!s) ? cpu->setRegister(reg, data)
                         : cpu->setUserRegister(reg, data);
                } else {
                    // STM{cond}{amod} Rn{!},<Rlist>{^}  ;Store (Push)
                    if (reg == rn) {
                        // hacky!
                        addressRnStoredAt = rnVal;
                    }
                    uint32_t data = (!s) ? cpu->getRegister(reg)
                                         : cpu->getUserRegister(reg);
                    // TODO: take this out when implemeinting pipelining
                    if (reg == 15) data += 12;
                    cpu->bus->write32(rnVal & 0xFFFFFFFC, data);
                }
                rnVal += 4;
            }
            regList >>= 1;
        }
        if (p) {
            // adjust the final rnVal so it is correct
            rnVal -= 4;
        }

    } else {
        // down, subtract offset from base
        if (p) {
            // pre-increment addressing
            rnVal -= 4;
        }
        for (int reg = 15; reg >= 0; reg--) {
            if (regList & 0x8000) {
                if (l) {
                    // LDM{cond}{amod} Rn{!},<Rlist>{^}  ;Load  (Pop)
                    uint32_t data = cpu->bus->read32(rnVal & 0xFFFFFFFC);
                    (!s) ? cpu->setRegister(reg, data)
                         : cpu->setUserRegister(reg, data);
                } else {
                    // STM{cond}{amod} Rn{!},<Rlist>{^}  ;Store (Push)
                    if (reg == rn) {
                        // hacky!
                        addressRnStoredAt = rnVal;
                    }

                    uint32_t data = (!s) ? cpu->getRegister(reg)
                                         : cpu->getUserRegister(reg);
                    if (reg == 15) data += 12;
                    cpu->bus->write32(rnVal & 0xFFFFFFFC, data);
                }
                rnVal -= 4;
            }
            regList <<= 1;
        }
        if (p) {
            // adjust the final rnVal so it is correct
            rnVal += 4;
        }
    }

    if (w) {
        if ((((uint16_t)instruction << (15 - rn)) > 0x8000) && !l) {
            // A STM which includes storing the base, with the base
            // as the first register to be stored, will therefore
            // store the unchanged value, whereas with the base second
            // or later in the transfer order, will store the modified value.
            assert(addressRnStoredAt != 0);
            cpu->bus->write32(addressRnStoredAt & 0xFFFFFFFC, rnVal);
        }
        cpu->setRegister(rn, rnVal);
    }

    if (s && l && (instruction & 0x00008000)) {
        // f instruction is LDM and R15 is in the list: (Mode Changes)
        // While R15 loaded, additionally: CPSR=SPSR_<current mode>
        cpu->cpsr = *(cpu->getCurrentModeSpsr());
    }
}

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::branchHandler(
    uint32_t instruction, ARM7TDMI *cpu) {
    assert((instruction & 0x0E000000) == 0x0A000000);

    uint32_t offset = ((instruction & 0x00FFFFFF) << 2);
    // TODO might be able to remove +8 when after implekemting pipelining
    uint32_t branchAddress = cpu->getRegister(PC_REGISTER) + offset + 8;
    branchAddress &= 0xFFFFFFFC;
    switch ((instruction & 0x01000000) >> 24) {
        case 0: {
            // B
            break;
        }
        case 1: {
            // BL LR=PC+4
            cpu->setRegister(14, cpu->getRegister(PC_REGISTER) + 4);
            break;
        }
    }

    cpu->setRegister(PC_REGISTER, branchAddress);
}

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::branchAndExchangeHandler(
    uint32_t instruction, ARM7TDMI *cpu) {
    assert(((instruction & 0x0FFFFF00) >> 8) == 0b00010010111111111111);
    // for this op rn is where rm usually is
    uint8_t rn = getRm(instruction);
    assert(rn != PC_REGISTER);
    uint32_t rnVal = cpu->getRegister(rn);

    switch ((instruction & 0x000000F0) >> 4) {
        case 0x1: {
            // BX PC=Rn, T=Rn.0
        }
        case 0x3: {
            // BLX PC=Rn, T=Rn.0, LR=PC+4
            cpu->setRegister(14, cpu->getRegister(PC_REGISTER) + 4);
        }
    }

    /*
        For ARM code, the low bits of the target address should be usually zero,
        otherwise, R15 is forcibly aligned by clearing the lower two bits.
        For THUMB code, the low bit of the target address may/should/must be
       set, the bit is (or is not) interpreted as thumb-bit (depending on the
       opcode), and R15 is then forcibly aligned by clearing the lower bit. In
       short, R15 will be always forcibly aligned, so mis-aligned branches won't
       have effect on subsequent opcodes that use R15, or [R15+disp] as operand.
    */

    bool t = rnVal & 0x1;
    cpu->cpsr.T = t;
    if (t) {
        rnVal &= 0xFFFFFFFE;
    } else {
        rnVal &= 0xFFFFFFFC;
    }
    cpu->setRegister(PC_REGISTER, rnVal);
}

/* ~~~~~~~~~~~~~~~ Undefined Operation ~~~~~~~~~~~~~~~~~~~~*/

ARM7TDMI::Cycles ARM7TDMI::ArmOpcodeHandlers::undefinedOpHandler(
    uint32_t instruction, ARM7TDMI *cpu) {
    DEBUG("UNDEFINED OPCODE! " << std::bitset<32>(instruction).to_string()
                               << std::endl);
    return {};
}