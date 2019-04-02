/*
 Reinette II, the french Apple II emulator
 Last modified 19th of March 2019

 Copyright (c) 2018, 2019 Arthur Ferreira

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include <ncurses.h>

#define ROMSTART 0xD000
#define ROMSIZE 0x3000    // 12KB
#define RAMSIZE 0xC000    // 48KB

#define CARRY 0x01
#define ZERO 0x02
#define INTERRUPT 0x04
#define DECIMAL 0x08
#define BREAK 0x10
#define UNDEFINED 0x20
#define OVERFLOW 0x40
#define SIGN 0x80

uint8_t rom[ROMSIZE];
uint8_t ram[RAMSIZE];

struct Operand{
  bool setAcc;
  uint16_t value, address;
}ope;

struct Register{
  uint8_t A,X,Y,SR,SP;
  uint16_t PC;
}reg;

uint8_t key = 0;
bool videoNeedsRefresh = true;


// MEMORY AND I/O

static uint8_t readMem(uint16_t address){
  if (address <  RAMSIZE)       return(ram[address]);
  else if (address >= ROMSTART) return(rom[address - ROMSTART]);
  else if (address == 0xC000)   return(key);  // KBD
  else if (address == 0xC010){                // KBDSTRB
    key &= 0x7F;                              // unset bit 7
    return(key);
  }
  else return(0);                             // catch all
}

static void writeMem(uint16_t address, uint8_t value){
  if (address & 0x400) videoNeedsRefresh = true;  // a change in text page 1
  if (address < RAMSIZE) ram[address] = value;
  else if (address == 0xC010) key &= 0x7F;    // KBDSTRB, similar as in readMem
}


// RESET

static void reset(){
  reg.PC = readMem(0xFFFC) | (readMem(0xFFFD) << 8);
  reg.SP = 0xFF;
  reg.SR |= UNDEFINED;
  ope.setAcc = false;
  ope.value = 0;
  ope.address = 0;
}


// STACK, SIGN AND ZERO FLAGS ROUTINES

static void push(uint8_t value){
  writeMem(0x100 + reg.SP--, value);
}

uint8_t pull(){
  return(readMem(0x100 + ++reg.SP));
}

static void setSZ(uint8_t value){  //  update both the Sign & Zero FLAGS
  if (value) reg.SR &= ~ZERO;
  else reg.SR |= ZERO;
  if (value & 0x80) reg.SR |= SIGN;
  else reg.SR &= ~SIGN;
}


// ADDRESSING MODES

static void IMP(){  // IMPlicit
}

static void ACC(){  // ACCumulator
  ope.value = reg.A;
  ope.setAcc = true;
}

static void IMM(){  // IMMediate
  ope.address = reg.PC++;
  ope.value = readMem(ope.address);
}

static void ZPG(){  // Zero PaGe
  ope.address = readMem(reg.PC++);
  ope.value = readMem(ope.address);
}

static void ZPX(){  // Zero Page,X
  ope.address = (readMem(reg.PC++) + reg.X) & 0xFF;
  ope.value = readMem(ope.address);
}

static void ZPY(){  // Zero Page,Y
  ope.address = (readMem(reg.PC++) + reg.Y) & 0xFF;
  ope.value = readMem(ope.address);
}

static void REL(){  // RELative (for branch instructions)
  ope.address = readMem(reg.PC++);
  if (ope.address & 0x80) ope.address |= 0xFF00;  // branch backward
}

static void ABS(){  // ABSolute
  ope.address = readMem(reg.PC) | (readMem(reg.PC + 1) << 8);
  ope.value = readMem(ope.address);
  reg.PC += 2;
}

static void ABX(){  // ABsolute,X
  ope.address = (readMem(reg.PC) | (readMem(reg.PC + 1) << 8)) + reg.X;
  ope.value = readMem(ope.address);
  reg.PC += 2;
}

static void ABY(){  // ABsolute,Y
  ope.address = (readMem(reg.PC) | (readMem(reg.PC + 1) << 8)) + reg.Y;
  ope.value = readMem(ope.address);
  reg.PC += 2;
}

static void IND(){  // INDirect - JMP ($ABCD) with page-boundary wraparound bug
  uint16_t vector1 = readMem(reg.PC) | (readMem(reg.PC + 1) << 8);
  uint16_t vector2 = (vector1 & 0xFF00) | ((vector1 + 1) & 0x00FF);
  ope.address  = readMem(vector1) | (readMem(vector2) << 8);
  ope.value = readMem(ope.address);
  reg.PC += 2;
}

static void IDX(){  // InDexed indirect X
  uint16_t vector1 = ((readMem(reg.PC++) + reg.X) & 0xFF);
  ope.address = readMem(vector1 & 0x00FF)|(readMem((vector1+1) & 0x00FF) << 8);
  ope.value = readMem(ope.address);
}

static void IDY(){  // InDirect Indexed Y
  uint16_t vector1 = readMem(reg.PC++);
  uint16_t vector2 = (vector1 & 0xFF00) | ((vector1 + 1) & 0x00FF);
  ope.address = (readMem(vector1) | (readMem(vector2) << 8)) + reg.Y;
  ope.value = readMem(ope.address);
}


// INSTRUCTIONS

static void NOP(){  // NO Operation
}

static void BRK(){  // BReaK
  push(((++reg.PC) >> 8) & 0xFF);
  push(reg.PC & 0xFF);
  push(reg.SR | BREAK);
  reg.SR |= INTERRUPT;
  reg.PC = readMem(0xFFFE) | (readMem(0xFFFF) << 8);
}

static void CLD(){  // CLear Decimal
  reg.SR &= ~DECIMAL;
}

static void SED(){  // SEt Decimal
  reg.SR |= DECIMAL;
}

static void CLC(){  // CLear Carry
  reg.SR &= ~CARRY;
}

static void SEC(){  // SEt Carry
  reg.SR |= CARRY;
}

static void CLI(){  // CLear Interrupt
  reg.SR &= ~INTERRUPT;
}

static void SEI(){  // SEt Interrupt
  reg.SR |= INTERRUPT;
}

static void CLV(){  // CLear oVerflow
  reg.SR &= ~OVERFLOW;
}

static void LDA(){  // LoaD Accumulator
  setSZ(reg.A=ope.value);
}

static void LDX(){  // LoaD X
  setSZ(reg.X=ope.value);
}

static void LDY(){  // LoaD Y
  setSZ(reg.Y=ope.value);
}

static void STA(){  // STore Accumulator
  writeMem(ope.address, reg.A);
}

static void STX(){  // STore X
  writeMem(ope.address, reg.X);
}

static void STY(){  // STore Y
  writeMem(ope.address, reg.Y);
}

static void DEC(){  // DECrement
  writeMem(ope.address, --ope.value);
  setSZ(ope.value);
}

static void DEX(){  // DEcrement X
  setSZ(--reg.X);
}

static void DEY(){  // DEcrement Y
  setSZ(--reg.Y);
}

static void INC(){  // INCrement
  writeMem(ope.address, ++ope.value);
  setSZ(ope.value);
}

static void INX(){  // INcrement X
  setSZ(++reg.X);
}

static void INY(){  // INcrement Y
  setSZ(++reg.Y);
}

static void TAX(){  // Transfer Accumulator to X
  setSZ(reg.X=reg.A);
}

static void TAY(){  // Transfer Accumulator to Y
  setSZ(reg.Y=reg.A);
}

static void TXA(){  // Transfer X to Accumulator
  setSZ(reg.A=reg.X);
}

static void TYA(){  // Transfer Y to Accumulator
  setSZ(reg.A=reg.Y);
}

static void TSX(){  // Transfer Sp to X
  setSZ(reg.X=reg.SP);
}

static void TXS(){  // Transfer X to Sp
  reg.SP = reg.X;
}

static void BEQ(){  // Branch on EQual (zero set)
  if (reg.SR & ZERO) reg.PC += ope.address;
}

static void BNE(){  // Branch on Not Equal (zero clear)
  if (!(reg.SR & ZERO)) reg.PC += ope.address;
}

static void BMI(){  // Branch if MInus (ie when negative, when SIGN is set)
  if (reg.SR & SIGN) reg.PC += ope.address;
}

static void BPL(){  // Branch if PLus (ie when positive, when SIGN is clear)
  if (!(reg.SR & SIGN)) reg.PC += ope.address;
}

static void BVS(){  // Branch on oVerflow Set
  if (reg.SR & OVERFLOW) reg.PC += ope.address;
}

static void BVC(){  // Branch on oVerflow Clear
  if (!(reg.SR & OVERFLOW)) reg.PC += ope.address;
}

static void BCS(){  // Branch on Carry Set
  if (reg.SR & CARRY) reg.PC +=ope.address;
}

static void BCC(){  // Branch on Carry Clear
  if (!(reg.SR & CARRY)) reg.PC += ope.address;
}

static void PHA(){  // PusH A to the stack
  push(reg.A);
}

static void PLA(){  // PulL stack into A
  setSZ(reg.A=pull());
}

static void PHP(){  // PusH Programm (Status) register to the stack
  push(reg.SR | BREAK);
}

static void PLP(){  // PulL stack into Programm (SR) register
  reg.SR = pull() | UNDEFINED;
}

static void JMP(){  // JuMP
  reg.PC = ope.address;
}

static void JSR(){  // Jump Sub-Routine
  push((--reg.PC >> 8) & 0xFF);
  push(reg.PC & 0xFF);
  reg.PC = ope.address;
}

static void RTS(){  // ReTurn from Sub-routine
  reg.PC = (pull() | (pull() << 8)) + 1;
}

static void RTI(){  // ReTurn from Interrupt
  reg.SR = pull();
  reg.PC = pull() | (pull() << 8);
}

static void CMP(){  // Compare with A
  setSZ(reg.A - ope.value);
  if (reg.A >= ope.value) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
}

static void CPX(){  // Compare with X
  setSZ(reg.X - ope.value);
  if (reg.X >= ope.value) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
}

static void CPY(){  // Compare with Y
  setSZ(reg.Y - ope.value);
  if (reg.Y >= ope.value) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
}

static void AND(){  // AND with A
  setSZ(reg.A &= ope.value);
}

static void ORA(){  // OR with A
  setSZ(reg.A |= ope.value);
}

static void EOR(){  // Exclusive Or with A
  setSZ(reg.A ^= ope.value);
}

static void BIT(){  // BIT with A - http://www.6502.org/tutorials/vflag.html
  if (reg.A & ope.value) reg.SR &= ~ZERO;
  else reg.SR |= ZERO;
  reg.SR = (reg.SR & 0x3F) | (ope.value & 0xC0);  // update SIGN & OVERFLOW
}

static void makeUpdates(uint8_t val){
  if (ope.setAcc) {
    reg.A = val;
    ope.setAcc = false;
  }
  else writeMem(ope.address, val);
  setSZ(val);
}

static void ASL(){  // Arithmetic Shift Left
  uint16_t result = (ope.value << 1);
  if (result & 0xFF00) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  makeUpdates((uint8_t)(result & 0xFF));
}

static void LSR(){  // Logical Shift Right
  if (ope.value & 1) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  makeUpdates((uint8_t)((ope.value >> 1) & 0xFF));
}

static void ROL(){  // ROtate Left
  uint16_t result = ((ope.value << 1) | (reg.SR & CARRY));
  if (result & 0x100) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  makeUpdates((uint8_t)(result & 0xFF));
}

static void ROR(){  // ROtate Right
  uint16_t result = (ope.value >> 1) | ((reg.SR & CARRY) << 7);
  if (ope.value & 0x1) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  makeUpdates((uint8_t)(result & 0xFF));
}

static void ADC(){  // ADd with Carry
  uint16_t result = reg.A + ope.value + (reg.SR & CARRY);
  setSZ(result);
  if (((result)^(reg.A ))&((result)^(ope.value))&0x0080) reg.SR |= OVERFLOW;
  else reg.SR &= ~OVERFLOW;
  if (reg.SR&DECIMAL) result += ((((result+0x66)^reg.A^ope.value)>>3)&0x22)*3;
  if (result & 0xFF00) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  reg.A = (result & 0xFF);
}

static void SBC(){  // SuBtract with Carry
  ope.value ^= 0xFF;
  if (reg.SR & DECIMAL) ope.value -= 0x0066;
  uint16_t result = reg.A + ope.value + (reg.SR & CARRY);
  setSZ(result);
  if (((result)^(reg.A ))&((result)^(ope.value))&0x0080) reg.SR |= OVERFLOW;
  else reg.SR &= ~OVERFLOW;
  if (reg.SR&DECIMAL) result += ((((result+0x66)^reg.A^ope.value)>>3)&0x22)*3;
  if (result & 0xFF00) reg.SR |= CARRY;
  else reg.SR &= ~CARRY;
  reg.A = (result & 0xFF);
}

static void UND(){  // UNDefined (not a valid or supported 6502 opcode)
}


// JUMP TABLES

static void (*instruction[])(void) = {
 BRK, ORA, UND, UND, UND, ORA, ASL, UND, PHP, ORA, ASL, UND, UND, ORA, ASL, UND,
 BPL, ORA, UND, UND, UND, ORA, ASL, UND, CLC, ORA, UND, UND, UND, ORA, ASL, UND,
 JSR, AND, UND, UND, BIT, AND, ROL, UND, PLP, AND, ROL, UND, BIT, AND, ROL, UND,
 BMI, AND, UND, UND, UND, AND, ROL, UND, SEC, AND, UND, UND, UND, AND, ROL, UND,
 RTI, EOR, UND, UND, UND, EOR, LSR, UND, PHA, EOR, LSR, UND, JMP, EOR, LSR, UND,
 BVC, EOR, UND, UND, UND, EOR, LSR, UND, CLI, EOR, UND, UND, UND, EOR, LSR, UND,
 RTS, ADC, UND, UND, UND, ADC, ROR, UND, PLA, ADC, ROR, UND, JMP, ADC, ROR, UND,
 BVS, ADC, UND, UND, UND, ADC, ROR, UND, SEI, ADC, UND, UND, UND, ADC, ROR, UND,
 UND, STA, UND, UND, STY, STA, STX, UND, DEY, UND, TXA, UND, STY, STA, STX, UND,
 BCC, STA, UND, UND, STY, STA, STX, UND, TYA, STA, TXS, UND, UND, STA, UND, UND,
 LDY, LDA, LDX, UND, LDY, LDA, LDX, UND, TAY, LDA, TAX, UND, LDY, LDA, LDX, UND,
 BCS, LDA, UND, UND, LDY, LDA, LDX, UND, CLV, LDA, TSX, UND, LDY, LDA, LDX, UND,
 CPY, CMP, UND, UND, CPY, CMP, DEC, UND, INY, CMP, DEX, UND, CPY, CMP, DEC, UND,
 BNE, CMP, UND, UND, UND, CMP, DEC, UND, CLD, CMP, UND, UND, UND, CMP, DEC, UND,
 CPX, SBC, UND, UND, CPX, SBC, INC, UND, INX, SBC, NOP, UND, CPX, SBC, INC, UND,
 BEQ, SBC, UND, UND, UND, SBC, INC, UND, SED, SBC, UND, UND, UND, SBC, INC, UND
};

static void (*addressing[])(void) = {
 IMP, IDX, IMP, IMP, IMP, ZPG, ZPG, IMP, IMP, IMM, ACC, IMP, IMP, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 ABS, IDX, IMP, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMM, ACC, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 IMP, IDX, IMP, IMP, IMP, ZPG, ZPG, IMP, IMP, IMM, ACC, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 IMP, IDX, IMP, IMP, IMP, ZPG, ZPG, IMP, IMP, IMM, ACC, IMP, IND, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 IMP, IDX, IMP, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMP, IMP, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, ZPX, ZPX, ZPY, IMP, IMP, ABY, IMP, IMP, IMP, ABX, IMP, IMP,
 IMM, IDX, IMM, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMM, IMP, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, ZPX, ZPX, ZPY, IMP, IMP, ABY, IMP, IMP, ABX, ABX, ABY, IMP,
 IMM, IDX, IMP, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMM, IMP, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP,
 IMM, IDX, IMP, IMP, ZPG, ZPG, ZPG, IMP, IMP, IMM, IMP, IMP, ABS, ABS, ABS, IMP,
 REL, IDY, IMP, IMP, IMP, ZPX, ZPX, IMP, IMP, ABY, IMP, IMP, IMP, ABX, ABX, IMP
};


// PROGRAM ENTRY POINT

int main(int argc, char *argv[]) {

  static uint16_t offsetsForRows[24] = {  // helper for video generation
    0x400, 0x480, 0x500, 0x580, 0x600, 0x680, 0x700, 0x780,
    0x428, 0x4A8, 0x528, 0x5A8, 0x628, 0x6A8, 0x728, 0x7A8,
    0x450, 0x4D0, 0x550, 0x5D0, 0x650, 0x6D0, 0x750, 0x7D0
  };
  uint8_t opcode, glyph;
  int ch;

  // ncurses initialization
  initscr();
  raw();
  noecho();
  curs_set(0);
  qiflush();
  keypad   (stdscr, TRUE);
  nodelay  (stdscr, TRUE);
  scrollok (stdscr, TRUE);

  // load the original Apple][ ROM, including the Programmer's Aid at $D000
  FILE *f=fopen("appleII.rom","rb");
  if (f != NULL) fread(rom, sizeof(uint8_t), ROMSIZE, f);
  fclose(f);

  // processor reset
  reset();

  // main loop
  while(1){
    for (int i=0; i<100; i++){    // execute 100 instructions before a kbd scan
      opcode = readMem(reg.PC++); // FETCH and increment the Program Counter
      addressing[opcode]();       // DECODE operands against the addressing mode
      instruction[opcode]();      // EXECUTE the instruction
    }

    // slow down emulation
    napms(1);

    // keyboard controller
    if ((ch = getch()) != ERR){
      if (ch == KEY_F( 7)) reset();                      // F7, processor reset
      if (ch == KEY_F(12)) { endwin(); return(0); }      // F12, exit program
      switch(key=(uint8_t)ch){                           // key translations
        case 0x0A: key = 0x0D; break;                    // LF    to CR
        case 0x04: key = 0x08; break;                    // LEFT  to BS
        case 0x05: key = 0x15; break;                    // RIGHT to NAK
        case 0x07: key = 0x08; break;                    // BELL  to BS ?
      }
      if ((key>0x60) && (key<0x7B)) key&=0xDF;           // to upper case
      key |= 0x80;                                       // set bit 7
    }

    // video controller - page 1 text mode only
    if (videoNeedsRefresh){                              // if content changed
      move(0, 0);
      for (int row=0; row<24; row++){                    // for each row
        for (int col=0; col<40; col++){                  // for each column
          glyph = ram[offsetsForRows[row] + col];        // read video memory
          if (glyph == '`') glyph = '_';                 // change cursor shape
          if (glyph < 0x40) attrset(A_REVERSE);          // is REVERSE ?
          else if (glyph > 0x7F) attrset(A_NORMAL);      // is NORMAL ?
          else attrset(A_BLINK);                         // it's FLASHING !
          glyph &= 0x7F;                                 // unset bit 7
          if (glyph > 0x5F) glyph &= 0x3F;               // shifts to match
          if (glyph < 0x20) glyph |= 0x40;               // the ASCII codes
          addch(glyph);                                  // print the glyph
        }
        addch(0x0A);                                     // to next row
      }
      videoNeedsRefresh = false;
    }
  }
}
