#pragma once
#include <cstdint>

// Shared 6502 opcode table used by both the CPU instruction tracer (NesCpu)
// and the static disassembler view (DebuggerUI).

enum AddrMode : uint8_t {
    IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABSX, ABSY, IND, INDX, INDY, REL
};

struct OpcodeInfo {
    char     name[4]; // mnemonic (3 chars + null)
    uint8_t  size;    // total instruction bytes (1-3)
    AddrMode mode;
};

// Unofficial opcodes are named after their common aliases.
// Unknown/truly-illegal slots use "???" with size=1 so the disassembler
// always advances by at least one byte and never loses sync.
inline constexpr OpcodeInfo kOpcodes[256] = {
//       0                   1                   2                   3
/* 00 */ {"BRK",1,IMP},  {"ORA",2,INDX}, {"???",1,IMP},  {"SLO",2,INDX},
/* 04 */ {"NOP",2,ZP},   {"ORA",2,ZP},   {"ASL",2,ZP},   {"SLO",2,ZP},
/* 08 */ {"PHP",1,IMP},  {"ORA",2,IMM},  {"ASL",1,ACC},  {"ANC",2,IMM},
/* 0C */ {"NOP",3,ABS},  {"ORA",3,ABS},  {"ASL",3,ABS},  {"SLO",3,ABS},
/* 10 */ {"BPL",2,REL},  {"ORA",2,INDY}, {"???",1,IMP},  {"SLO",2,INDY},
/* 14 */ {"NOP",2,ZPX},  {"ORA",2,ZPX},  {"ASL",2,ZPX},  {"SLO",2,ZPX},
/* 18 */ {"CLC",1,IMP},  {"ORA",3,ABSY}, {"NOP",1,IMP},  {"SLO",3,ABSY},
/* 1C */ {"NOP",3,ABSX}, {"ORA",3,ABSX}, {"ASL",3,ABSX}, {"SLO",3,ABSX},
/* 20 */ {"JSR",3,ABS},  {"AND",2,INDX}, {"???",1,IMP},  {"RLA",2,INDX},
/* 24 */ {"BIT",2,ZP},   {"AND",2,ZP},   {"ROL",2,ZP},   {"RLA",2,ZP},
/* 28 */ {"PLP",1,IMP},  {"AND",2,IMM},  {"ROL",1,ACC},  {"ANC",2,IMM},
/* 2C */ {"BIT",3,ABS},  {"AND",3,ABS},  {"ROL",3,ABS},  {"RLA",3,ABS},
/* 30 */ {"BMI",2,REL},  {"AND",2,INDY}, {"???",1,IMP},  {"RLA",2,INDY},
/* 34 */ {"NOP",2,ZPX},  {"AND",2,ZPX},  {"ROL",2,ZPX},  {"RLA",2,ZPX},
/* 38 */ {"SEC",1,IMP},  {"AND",3,ABSY}, {"NOP",1,IMP},  {"RLA",3,ABSY},
/* 3C */ {"NOP",3,ABSX}, {"AND",3,ABSX}, {"ROL",3,ABSX}, {"RLA",3,ABSX},
/* 40 */ {"RTI",1,IMP},  {"EOR",2,INDX}, {"???",1,IMP},  {"SRE",2,INDX},
/* 44 */ {"NOP",2,ZP},   {"EOR",2,ZP},   {"LSR",2,ZP},   {"SRE",2,ZP},
/* 48 */ {"PHA",1,IMP},  {"EOR",2,IMM},  {"LSR",1,ACC},  {"ALR",2,IMM},
/* 4C */ {"JMP",3,ABS},  {"EOR",3,ABS},  {"LSR",3,ABS},  {"SRE",3,ABS},
/* 50 */ {"BVC",2,REL},  {"EOR",2,INDY}, {"???",1,IMP},  {"SRE",2,INDY},
/* 54 */ {"NOP",2,ZPX},  {"EOR",2,ZPX},  {"LSR",2,ZPX},  {"SRE",2,ZPX},
/* 58 */ {"CLI",1,IMP},  {"EOR",3,ABSY}, {"NOP",1,IMP},  {"SRE",3,ABSY},
/* 5C */ {"NOP",3,ABSX}, {"EOR",3,ABSX}, {"LSR",3,ABSX}, {"SRE",3,ABSX},
/* 60 */ {"RTS",1,IMP},  {"ADC",2,INDX}, {"???",1,IMP},  {"RRA",2,INDX},
/* 64 */ {"NOP",2,ZP},   {"ADC",2,ZP},   {"ROR",2,ZP},   {"RRA",2,ZP},
/* 68 */ {"PLA",1,IMP},  {"ADC",2,IMM},  {"ROR",1,ACC},  {"ARR",2,IMM},
/* 6C */ {"JMP",3,IND},  {"ADC",3,ABS},  {"ROR",3,ABS},  {"RRA",3,ABS},
/* 70 */ {"BVS",2,REL},  {"ADC",2,INDY}, {"???",1,IMP},  {"RRA",2,INDY},
/* 74 */ {"NOP",2,ZPX},  {"ADC",2,ZPX},  {"ROR",2,ZPX},  {"RRA",2,ZPX},
/* 78 */ {"SEI",1,IMP},  {"ADC",3,ABSY}, {"NOP",1,IMP},  {"RRA",3,ABSY},
/* 7C */ {"NOP",3,ABSX}, {"ADC",3,ABSX}, {"ROR",3,ABSX}, {"RRA",3,ABSX},
/* 80 */ {"NOP",2,IMM},  {"STA",2,INDX}, {"NOP",2,IMM},  {"SAX",2,INDX},
/* 84 */ {"STY",2,ZP},   {"STA",2,ZP},   {"STX",2,ZP},   {"SAX",2,ZP},
/* 88 */ {"DEY",1,IMP},  {"NOP",2,IMM},  {"TXA",1,IMP},  {"???",2,IMM},
/* 8C */ {"STY",3,ABS},  {"STA",3,ABS},  {"STX",3,ABS},  {"SAX",3,ABS},
/* 90 */ {"BCC",2,REL},  {"STA",2,INDY}, {"???",1,IMP},  {"???",2,INDY},
/* 94 */ {"STY",2,ZPX},  {"STA",2,ZPX},  {"STX",2,ZPY},  {"SAX",2,ZPY},
/* 98 */ {"TYA",1,IMP},  {"STA",3,ABSY}, {"TXS",1,IMP},  {"???",3,ABSY},
/* 9C */ {"???",3,ABSX}, {"STA",3,ABSX}, {"???",3,ABSY}, {"???",3,ABSY},
/* A0 */ {"LDY",2,IMM},  {"LDA",2,INDX}, {"LDX",2,IMM},  {"LAX",2,INDX},
/* A4 */ {"LDY",2,ZP},   {"LDA",2,ZP},   {"LDX",2,ZP},   {"LAX",2,ZP},
/* A8 */ {"TAY",1,IMP},  {"LDA",2,IMM},  {"TAX",1,IMP},  {"LAX",2,IMM},
/* AC */ {"LDY",3,ABS},  {"LDA",3,ABS},  {"LDX",3,ABS},  {"LAX",3,ABS},
/* B0 */ {"BCS",2,REL},  {"LDA",2,INDY}, {"???",1,IMP},  {"LAX",2,INDY},
/* B4 */ {"LDY",2,ZPX},  {"LDA",2,ZPX},  {"LDX",2,ZPY},  {"LAX",2,ZPY},
/* B8 */ {"CLV",1,IMP},  {"LDA",3,ABSY}, {"TSX",1,IMP},  {"???",3,ABSY},
/* BC */ {"LDY",3,ABSX}, {"LDA",3,ABSX}, {"LDX",3,ABSY}, {"LAX",3,ABSY},
/* C0 */ {"CPY",2,IMM},  {"CMP",2,INDX}, {"NOP",2,IMM},  {"DCP",2,INDX},
/* C4 */ {"CPY",2,ZP},   {"CMP",2,ZP},   {"DEC",2,ZP},   {"DCP",2,ZP},
/* C8 */ {"INY",1,IMP},  {"CMP",2,IMM},  {"DEX",1,IMP},  {"AXS",2,IMM},
/* CC */ {"CPY",3,ABS},  {"CMP",3,ABS},  {"DEC",3,ABS},  {"DCP",3,ABS},
/* D0 */ {"BNE",2,REL},  {"CMP",2,INDY}, {"???",1,IMP},  {"DCP",2,INDY},
/* D4 */ {"NOP",2,ZPX},  {"CMP",2,ZPX},  {"DEC",2,ZPX},  {"DCP",2,ZPX},
/* D8 */ {"CLD",1,IMP},  {"CMP",3,ABSY}, {"NOP",1,IMP},  {"DCP",3,ABSY},
/* DC */ {"NOP",3,ABSX}, {"CMP",3,ABSX}, {"DEC",3,ABSX}, {"DCP",3,ABSX},
/* E0 */ {"CPX",2,IMM},  {"SBC",2,INDX}, {"NOP",2,IMM},  {"ISB",2,INDX},
/* E4 */ {"CPX",2,ZP},   {"SBC",2,ZP},   {"INC",2,ZP},   {"ISB",2,ZP},
/* E8 */ {"INX",1,IMP},  {"SBC",2,IMM},  {"NOP",1,IMP},  {"SBC",2,IMM},
/* EC */ {"CPX",3,ABS},  {"SBC",3,ABS},  {"INC",3,ABS},  {"ISB",3,ABS},
/* F0 */ {"BEQ",2,REL},  {"SBC",2,INDY}, {"???",1,IMP},  {"ISB",2,INDY},
/* F4 */ {"NOP",2,ZPX},  {"SBC",2,ZPX},  {"INC",2,ZPX},  {"ISB",2,ZPX},
/* F8 */ {"SED",1,IMP},  {"SBC",3,ABSY}, {"NOP",1,IMP},  {"ISB",3,ABSY},
/* FC */ {"NOP",3,ABSX}, {"SBC",3,ABSX}, {"INC",3,ABSX}, {"ISB",3,ABSX},
};
