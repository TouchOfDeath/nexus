using System;
using System.IO;
using System.Text;
using System.Collections.Generic;

namespace NexusTools
{
    class NexDisasm
    {
        static readonly string[] Reg64     = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15" };
        static readonly string[] Reg32     = { "eax","ecx","edx","ebx","esp","ebp","esi","edi","r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d" };
        static readonly string[] Reg16     = { "ax","cx","dx","bx","sp","bp","si","di","r8w","r9w","r10w","r11w","r12w","r13w","r14w","r15w" };
        static readonly string[] Reg8      = { "al","cl","dl","bl","spl","bpl","sil","dil","r8b","r9b","r10b","r11b","r12b","r13b","r14b","r15b" };
        static readonly string[] Reg8Legacy = { "al","cl","dl","bl","ah","ch","dh","bh" };
        static readonly string[] XmmRegs  = { "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15" };
        static readonly string[] JccNames = { "jo","jno","jb","jae","je","jne","jbe","ja","js","jns","jp","jnp","jl","jge","jle","jg" };

        static Dictionary<ulong, string> Symbols = new Dictionary<ulong, string>();
        static bool NexusMode = false;

        static void Main(string[] args)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.WriteLine(" [NEXUS] NEXDISASM v3.0 - Native x86-64 Disassembler      ");
            Console.WriteLine("==========================================================");
            Console.ResetColor();

            if (args.Length == 0)
            {
                Console.WriteLine("Usage: nexdisasm.exe <path-to-binary.exe> [options]");
                Console.WriteLine("Options:");
                Console.WriteLine("  --rva <hex_rva>     Disassemble from RVA  (e.g. --rva 0x1000)");
                Console.WriteLine("  --va  <hex_va>      Disassemble from VA   (e.g. --va 0x401000)");
                Console.WriteLine("  --offset <hex_off>  Disassemble from file offset");
                Console.WriteLine("  -n <count>          Max instructions to decode (default: 2000)");
                Console.WriteLine("  --nexus             Enable NEXUS-specific symbol annotations");
                return;
            }

            string filePath = args[0];
            if (!File.Exists(filePath))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] File not found: " + filePath);
                Console.ResetColor();
                return;
            }

            byte[] fileBytes = File.ReadAllBytes(filePath);
            int    startOffset = -1;
            int    maxInstr    = 2000;
            ulong  targetRVA   = 0;
            ulong  imageBase   = 0x400000;
            ulong  textRVA     = 0x1000;
            int    textOffset  = 0x200;
            int    textLength  = fileBytes.Length - 0x200;

            ParsePE(fileBytes, out imageBase, out textRVA, out textOffset, out textLength);

            for (int i = 1; i < args.Length; i++)
            {
                string a = args[i];
                if (a.Equals("--rva", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length)
                {
                    targetRVA   = (ulong)ParseNumber(args[++i]);
                    startOffset = RvaToOffset(targetRVA, textRVA, textOffset);
                }
                else if (a.Equals("--va", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length)
                {
                    ulong va  = (ulong)ParseNumber(args[++i]);
                    targetRVA = va - imageBase;
                    startOffset = RvaToOffset(targetRVA, textRVA, textOffset);
                }
                else if (a.Equals("--offset", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length)
                {
                    startOffset = ParseNumber(args[++i]);
                    targetRVA   = textRVA + (ulong)(startOffset - textOffset);
                }
                else if ((a.Equals("-n", StringComparison.OrdinalIgnoreCase) || a.Equals("--count", StringComparison.OrdinalIgnoreCase)) && i+1 < args.Length)
                {
                    maxInstr = ParseNumber(args[++i]);
                }
                else if (a.Equals("--nexus", StringComparison.OrdinalIgnoreCase))
                {
                    NexusMode = true;
                }
                else if (startOffset == -1 && !a.StartsWith("-"))
                {
                    startOffset = ParseNumber(a);
                    targetRVA   = textRVA + (ulong)(startOffset - textOffset);
                }
            }

            if (startOffset == -1) { startOffset = textOffset; targetRVA = textRVA; }

            int length = textLength;
            if (startOffset + length > fileBytes.Length) length = fileBytes.Length - startOffset;

            Console.WriteLine("[+] Image Base:   0x{0:X12}", imageBase);
            Console.WriteLine("[+] Start Offset: 0x{0:X6} (RVA: 0x{1:X8}, VA: 0x{2:X12})", startOffset, targetRVA, imageBase + targetRVA);
            Console.WriteLine("[+] Max Count:    {0} instructions", maxInstr);
            if (NexusMode) Console.WriteLine("[+] NEXUS mode:   ON (compiler-specific annotations enabled)");
            Console.WriteLine();
            Console.WriteLine("  {0,-10} {1,-18} {2,-22} {3}", "Offset", "RVA (Virtual)", "Bytes", "Assembly Instruction");
            Console.WriteLine("  -----------------------------------------------------------------------------------------");

            int ip    = startOffset;
            int end   = startOffset + length;
            int count = 0;

            while (ip < end && count < maxInstr)
            {
                int   instrStart = ip;
                ulong curRVA     = textRVA + (ulong)(instrStart - textOffset);

                if (Symbols.ContainsKey(curRVA))
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("\n  <{0}>:", Symbols[curRVA]);
                    Console.ResetColor();
                }

                string asm      = DecodeInstruction(fileBytes, ref ip, curRVA, imageBase);
                int    instrLen = ip - instrStart;

                StringBuilder hexBytes = new StringBuilder();
                for (int b = 0; b < instrLen; b++)
                    hexBytes.Append(fileBytes[instrStart + b].ToString("X2")).Append(" ");

                Console.WriteLine("  0x{0:X6}   0x{1:X12}   {2,-22} {3}", instrStart, imageBase + curRVA, hexBytes.ToString().Trim(), asm);
                count++;
            }

            // Trailing padding detection
            if (ip < end)
            {
                int padStart = ip;
                bool allPad  = true;
                for (int i = ip; i < end; i++)
                    if (fileBytes[i] != 0x00 && fileBytes[i] != 0xCC) { allPad = false; break; }

                if (allPad && end - padStart > 0)
                {
                    Console.ForegroundColor = ConsoleColor.DarkGray;
                    Console.WriteLine("\n  [+] Trailing padding: {0} bytes (0x00/0xCC fill) at 0x{1:X6}", end - padStart, padStart);
                    Console.ResetColor();
                }
            }

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("[+] Disassembly completed ({0} instructions decoded).", count);
            Console.ResetColor();
        }

        static void ParsePE(byte[] b, out ulong imageBase, out ulong textRVA, out int textOffset, out int textLength)
        {
            imageBase  = 0x400000;
            textRVA    = 0x1000;
            textOffset = 0x200;
            textLength = b.Length - 0x200;

            // Default NEXUS symbols (always present)
            Symbols[0x1000] = "entry_point";
            Symbols[0x1C00] = "builtin_read_int";
            Symbols[0x1C80] = "builtin_print_int";

            if (b.Length < 0x40 || b[0] != 'M' || b[1] != 'Z') return;
            int peOff = BitConverter.ToInt32(b, 0x3C);
            if (peOff + 24 > b.Length || b[peOff] != 'P' || b[peOff+1] != 'E') return;

            ushort numSec  = BitConverter.ToUInt16(b, peOff + 6);
            ushort optSize = BitConverter.ToUInt16(b, peOff + 20);
            int    optOff  = peOff + 24;
            bool   isPE64  = (BitConverter.ToUInt16(b, optOff) == 0x20B);
            imageBase      = isPE64 ? BitConverter.ToUInt64(b, optOff + 24) : BitConverter.ToUInt32(b, optOff + 28);
            int secTableOff = optOff + optSize;
            uint importRVA  = BitConverter.ToUInt32(b, optOff + (isPE64 ? 120 : 104));

            for (int i = 0; i < numSec; i++)
            {
                int    sOff  = secTableOff + (i * 40);
                string sName = Encoding.ASCII.GetString(b, sOff, 8).TrimEnd('\0');
                if (sName.Equals(".text", StringComparison.OrdinalIgnoreCase))
                {
                    textRVA    = BitConverter.ToUInt32(b, sOff + 12);
                    textLength = (int)BitConverter.ToUInt32(b, sOff + 16);
                    textOffset = (int)BitConverter.ToUInt32(b, sOff + 20);
                }
            }

            // Parse IAT
            if (importRVA != 0)
            {
                int impOff = (int)(importRVA - textRVA + (ulong)textOffset);
                if (impOff > 0 && impOff + 20 <= b.Length)
                {
                    uint firstThunk = BitConverter.ToUInt32(b, impOff + 16);
                    uint origThunk  = BitConverter.ToUInt32(b, impOff);
                    uint thunkRVA   = origThunk != 0 ? origThunk : firstThunk;
                    int  thunkOff   = (int)(thunkRVA - textRVA + (ulong)textOffset);

                    if (thunkOff > 0 && thunkOff + 8 <= b.Length)
                    {
                        int idx = 0;
                        while (thunkOff + 8 <= b.Length)
                        {
                            ulong val = BitConverter.ToUInt64(b, thunkOff);
                            if (val == 0) break;
                            if ((val & 0x8000000000000000UL) == 0)
                            {
                                int nameOff = (int)(val - textRVA + (ulong)textOffset);
                                if (nameOff > 0 && nameOff + 2 < b.Length)
                                {
                                    int end2 = nameOff + 2;
                                    while (end2 < b.Length && b[end2] != 0) end2++;
                                    string apiName = Encoding.ASCII.GetString(b, nameOff + 2, end2 - (nameOff + 2));
                                    Symbols[firstThunk + (ulong)(idx * 8)] = "IAT!" + apiName;
                                }
                            }
                            thunkOff += 8;
                            idx++;
                        }
                    }
                }
            }
        }

        static int RvaToOffset(ulong rva, ulong textRVA, int textOffset)
        {
            if (rva >= textRVA) return (int)(rva - textRVA) + textOffset;
            return (int)rva;
        }

        static int ParseNumber(string s)
        {
            if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                return Convert.ToInt32(s.Substring(2), 16);
            return Convert.ToInt32(s);
        }

        static string DecodeInstruction(byte[] b, ref int ip, ulong rva, ulong imageBase)
        {
            if (ip >= b.Length) return "???";

            string dummy = "";
            string reg   = "";
            string rm    = "";

            bool isRep = false, isF2 = false, isF3 = false, is66 = false;
            bool rexPresent = false, rexW = false, rexR = false, rexX = false, rexB = false;

            // Prefix loop
            while (ip < b.Length)
            {
                byte p = b[ip];
                if      (p == 0xF3) { isF3 = true; isRep = true; ip++; }
                else if (p == 0xF2) { isF2 = true; ip++; }
                else if (p == 0x66) { is66 = true; ip++; }
                else if (p >= 0x40 && p <= 0x4F)
                {
                    rexPresent = true;
                    rexW = (p & 0x08) != 0;
                    rexR = (p & 0x04) != 0;
                    rexX = (p & 0x02) != 0;
                    rexB = (p & 0x01) != 0;
                    ip++;
                    break;
                }
                else break;
            }

            if (ip >= b.Length) return "db 0x" + b[ip-1].ToString("X2");

            byte opfx = b[ip++];
            int  instrStart = ip - 1; // For relative branch math: rva already set correctly at call site

            // rep stosq / rep stosb / rep movsb
            if (isRep && !isF2)
            {
                if (opfx == 0xAB) return rexW ? "rep stosq" : "rep stosd";
                if (opfx == 0xAA) return "rep stosb";
                if (opfx == 0xA4) return "rep movsb";
            }

            // Single-byte specials
            if (opfx == 0x90) return "nop";
            if (opfx == 0xCC) return "int 3";
            if (opfx == 0x99) return rexW ? "cqo" : "cdq";
            if (opfx == 0x98) return rexW ? "cdqe" : "cbw";
            if (opfx == 0xC3) return "ret";
            if (opfx == 0xC9) return "leave";
            if (opfx == 0x9F) return "lahf";
            if (opfx == 0x9E) return "sahf";

            // RET imm16
            if (opfx == 0xC2) { ushort imm = ReadUInt16(b, ref ip); return "ret 0x" + imm.ToString("X4"); }

            // PUSH/POP reg
            if (opfx >= 0x50 && opfx <= 0x57) return "push " + Reg64[(opfx - 0x50) + (rexB ? 8 : 0)];
            if (opfx >= 0x58 && opfx <= 0x5F) return "pop "  + Reg64[(opfx - 0x58) + (rexB ? 8 : 0)];

            // PUSH imm
            if (opfx == 0x6A) return "push " + (sbyte)b[ip++];
            if (opfx == 0x68) return "push 0x" + ReadUInt32(b, ref ip).ToString("X8");

            // JMP rel8 / rel32
            if (opfx == 0xEB) { sbyte d = (sbyte)b[ip++]; ulong t = (ulong)((long)rva + 2 + d); return string.Format("jmp short 0x{0:X} ({1})", imageBase+t, d>=0?"+"+d:""+d); }
            if (opfx == 0xE9) { int   d = ReadInt32(b, ref ip);   ulong t = (ulong)((long)rva + 5 + d); return string.Format("jmp 0x{0:X} ({1})", imageBase+t, d>=0?"+"+d:""+d); }

            // CALL rel32
            if (opfx == 0xE8)
            {
                int   d   = ReadInt32(b, ref ip);
                ulong t   = (ulong)((long)rva + 5 + d);
                string sym = Symbols.ContainsKey(t) ? " <" + Symbols[t] + ">" : "";
                return string.Format("call 0x{0:X}{1}", imageBase+t, sym);
            }

            // Jcc rel8 (70..7F)
            if (opfx >= 0x70 && opfx <= 0x7F)
            {
                sbyte d = (sbyte)b[ip++];
                ulong t = (ulong)((long)rva + 2 + d);
                return string.Format("{0} short 0x{1:X}", JccNames[opfx-0x70], imageBase+t);
            }

            // MOV reg, imm64/32 (B8..BF)
            if (opfx >= 0xB8 && opfx <= 0xBF)
            {
                int rIdx = (opfx - 0xB8) + (rexB ? 8 : 0);
                if (rexW) { ulong i64 = ReadUInt64(b, ref ip); return string.Format("mov {0}, 0x{1:X16}", Reg64[rIdx], i64); }
                else      { uint  i32 = ReadUInt32(b, ref ip); return string.Format("mov {0}, 0x{1:X8}",  Reg32[rIdx], i32); }
            }

            // MOV reg8, imm8 (B0..B7)
            if (opfx >= 0xB0 && opfx <= 0xB7)
            {
                int    rIdx  = (opfx - 0xB0) + (rexB ? 8 : 0);
                byte   imm8  = b[ip++];
                string rName = rexPresent ? Reg8[rIdx] : Reg8Legacy[rIdx];
                return string.Format("mov {0}, 0x{1:X2}", rName, imm8);
            }

            // Immediate accumulator forms
            if (opfx == 0x05) { uint i = ReadUInt32(b, ref ip); return string.Format("add {0}, 0x{1:X}", rexW?"rax":"eax", i); }
            if (opfx == 0x0C) { byte i = b[ip++];              return string.Format("or  al, 0x{0:X2}", i); }
            if (opfx == 0x24) { byte i = b[ip++];              return string.Format("and al, 0x{0:X2}", i); }
            if (opfx == 0x2D) { uint i = ReadUInt32(b, ref ip); return string.Format("sub {0}, 0x{1:X}", rexW?"rax":"eax", i); }
            if (opfx == 0x35) { uint i = ReadUInt32(b, ref ip); return string.Format("xor {0}, 0x{1:X}", rexW?"rax":"eax", i); }
            if (opfx == 0x3D) { uint i = ReadUInt32(b, ref ip); return string.Format("cmp {0}, 0x{1:X}", rexW?"rax":"eax", i); }

            // IMUL r, r/m, imm32 (69) / imm8 (6B)
            if (opfx == 0x69)
            {
                DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false);
                int imm = ReadInt32(b, ref ip);
                return string.Format("imul {0}, {1}, 0x{2:X}", reg, rm, imm);
            }
            if (opfx == 0x6B)
            {
                DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false);
                sbyte imm8s = (sbyte)b[ip++];
                return string.Format("imul {0}, {1}, {2}", reg, rm, imm8s);
            }

            // Two-byte opcodes (0F ...)
            if (opfx == 0x0F)
            {
                byte op2 = b[ip++];

                if (op2 == 0x05) return "syscall";

                // Jcc rel32 (0F 80..8F)
                if (op2 >= 0x80 && op2 <= 0x8F)
                {
                    int   d = ReadInt32(b, ref ip);
                    ulong t = (ulong)((long)rva + 6 + d);
                    return string.Format("{0} 0x{1:X} ({2})", JccNames[op2-0x80], imageBase+t, d>=0?"+"+d:""+d);
                }

                // CMOVcc (0F 40..4F)
                if (op2 >= 0x40 && op2 <= 0x4F)
                {
                    DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false);
                    return string.Format("cmov{0} {1}, {2}", JccNames[op2-0x40], reg, rm);
                }

                // SETcc (0F 90..9F)
                if (op2 >= 0x90 && op2 <= 0x9F)
                {
                    DecodeModRM(b, ref ip, rexR, rexB, rexX, false, out dummy, out rm, true, false);
                    return string.Format("set{0} {1}", JccNames[op2-0x90], rm);
                }

                // IMUL r64, r/m64 (0F AF)
                if (op2 == 0xAF) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("imul {0}, {1}", reg, rm); }

                // MOVZX (0F B6 / 0F B7)
                if (op2 == 0xB6) { DecodeModRM(b, ref ip, rexR, rexB, rexX, false, out reg, out rm, true,  false); return string.Format("movzx {0}, {1}", rexW?Reg64[GetRegIndex(reg)]:reg, rm); }
                if (op2 == 0xB7) { DecodeModRM(b, ref ip, rexR, rexB, rexX, false, out reg, out rm, false, false); return string.Format("movzx {0}, {1}", rexW?Reg64[GetRegIndex(reg)]:reg, rm); }

                // MOVSX (0F BE byte, 0F BF word)
                if (op2 == 0xBE) { DecodeModRM(b, ref ip, rexR, rexB, rexX, false, out reg, out rm, true,  false); return string.Format("movsx {0}, {1}", rexW?Reg64[GetRegIndex(reg)]:Reg32[GetRegIndex(reg)], rm); }
                if (op2 == 0xBF) { DecodeModRM(b, ref ip, rexR, rexB, rexX, false, out reg, out rm, false, false); return string.Format("movsx {0}, {1}", rexW?Reg64[GetRegIndex(reg)]:Reg32[GetRegIndex(reg)], rm); }

                // BSWAP r64 (0F C8..CF)
                if (op2 >= 0xC8 && op2 <= 0xCF) return "bswap " + Reg64[(op2-0xC8) + (rexB?8:0)];

                // BT r/m64, r64 (0F A3)
                if (op2 == 0xA3) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("bt {0}, {1}", rm, reg); }

                // BSF / BSR (0F BC / 0F BD)
                if (op2 == 0xBC) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("bsf {0}, {1}", reg, rm); }
                if (op2 == 0xBD) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("bsr {0}, {1}", reg, rm); }

                // SSE MOVSD (F2 0F 10 / 11)
                if (isF2 && (op2 == 0x10 || op2 == 0x11))
                {
                    DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false);
                    return string.Format("movsd {0}, {1}", op2==0x10?reg:rm, op2==0x10?rm:reg);
                }

                return string.Format("0F {0:X2} (two-byte op)", op2);
            }

            // LEA (8D)
            if (opfx == 0x8D) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("lea {0}, {1}", reg, rm); }

            // MOVSXD (63)
            if (opfx == 0x63) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("movsxd {0}, {1}", reg, rm); }

            // MOV variants
            if (opfx == 0x89) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("mov {0}, {1}", rm,  reg); }
            if (opfx == 0x8B) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("mov {0}, {1}", reg, rm);  }
            if (opfx == 0x88) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, true,  true);  return string.Format("mov {0}, {1}", rm,  reg); }
            if (opfx == 0x8A) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, true,  true);  return string.Format("mov {0}, {1}", reg, rm);  }

            // ADD r/m, r (01) / ADD r, r/m (03)
            if (opfx == 0x01) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("add {0}, {1}", rm,  reg); }
            if (opfx == 0x03) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("add {0}, {1}", reg, rm);  }

            // OR r/m, r (09) / OR r, r/m (0B)
            if (opfx == 0x09) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("or  {0}, {1}", rm,  reg); }
            if (opfx == 0x0B) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("or  {0}, {1}", reg, rm);  }

            // AND r/m, r (21) / AND r, r/m (23)
            if (opfx == 0x21) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("and {0}, {1}", rm,  reg); }
            if (opfx == 0x23) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("and {0}, {1}", reg, rm);  }

            // SUB r/m, r (29) / SUB r, r/m (2B)
            if (opfx == 0x29) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("sub {0}, {1}", rm,  reg); }
            if (opfx == 0x2B) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("sub {0}, {1}", reg, rm);  }

            // CMP r/m, r (39) / CMP r, r/m (3B)
            if (opfx == 0x39) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("cmp {0}, {1}", rm,  reg); }
            if (opfx == 0x3B) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("cmp {0}, {1}", reg, rm);  }

            // TEST r/m, r (85)
            if (opfx == 0x85) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("test {0}, {1}", rm, reg); }

            // XOR r/m, r (31) / XOR r, r/m (33)
            if (opfx == 0x31) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("xor {0}, {1}", rm,  reg); }
            if (opfx == 0x33) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("xor {0}, {1}", reg, rm);  }

            // XCHG r/m64, r64 (87)
            if (opfx == 0x87) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out reg, out rm, false, false); return string.Format("xchg {0}, {1}", rm, reg); }

            // Shift by 1: D1 group (ROL, ROR, RCL, RCR, SHL, SHR, SAL, SAR)
            if (opfx == 0xD1)
            {
                byte modrm = b[ip]; int subOp = (modrm >> 3) & 7;
                string[] shiftNames = { "rol","ror","rcl","rcr","shl","shr","sal","sar" };
                DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out dummy, out rm, false, false);
                return string.Format("{0} {1}, 1", shiftNames[subOp], rm);
            }

            // Shift by CL: D3 group
            if (opfx == 0xD3)
            {
                byte modrm = b[ip]; int subOp = (modrm >> 3) & 7;
                string[] shiftNames = { "rol","ror","rcl","rcr","shl","shr","sal","sar" };
                DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out dummy, out rm, false, false);
                return string.Format("{0} {1}, cl", shiftNames[subOp], rm);
            }

            // Shift by imm8: C1 group
            if (opfx == 0xC1)
            {
                byte modrm = b[ip]; int subOp = (modrm >> 3) & 7;
                string[] shiftNames = { "rol","ror","rcl","rcr","shl","shr","sal","sar" };
                DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out dummy, out rm, false, false);
                byte imm8 = b[ip++];
                return string.Format("{0} {1}, {2}", shiftNames[subOp], rm, imm8);
            }

            // Group 1: 81 / 83
            if (opfx == 0x81 || opfx == 0x83)
            {
                byte modrm = b[ip]; int subOp = (modrm >> 3) & 7;
                string[] g1 = { "add","or","adc","sbb","and","sub","xor","cmp" };
                DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out dummy, out rm, false, false);
                if (opfx == 0x83) { sbyte s8 = (sbyte)b[ip++]; return string.Format("{0} {1}, 0x{2:X}", g1[subOp], rm, (int)s8); }
                else { uint i32 = ReadUInt32(b, ref ip); return string.Format("{0} {1}, 0x{2:X}", g1[subOp], rm, i32); }
            }

            // Group 3: F7 (TEST, NOT, NEG, MUL, IMUL, DIV, IDIV)
            if (opfx == 0xF7)
            {
                byte modrm = b[ip]; int subOp = (modrm >> 3) & 7;
                DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out dummy, out rm, false, false);
                if (subOp == 3) return "neg "  + rm;
                if (subOp == 7) return "idiv " + rm;
                if (subOp == 6) return "div "  + rm;
                if (subOp == 5) return "imul " + rm;
                if (subOp == 4) return "mul "  + rm;
                if (subOp == 2) return "not "  + rm;
                if (subOp == 0) { uint imm = ReadUInt32(b, ref ip); return string.Format("test {0}, 0x{1:X}", rm, imm); }
                return string.Format("F7 /{0} {1}", subOp, rm);
            }

            // Group 3 byte: F6
            if (opfx == 0xF6)
            {
                byte modrm = b[ip]; int subOp = (modrm >> 3) & 7;
                DecodeModRM(b, ref ip, rexR, rexB, rexX, false, out dummy, out rm, true, false);
                if (subOp == 3) return "neg byte "  + rm;
                if (subOp == 7) return "idiv byte " + rm;
                if (subOp == 6) return "div byte "  + rm;
                if (subOp == 4) return "mul byte "  + rm;
                if (subOp == 2) return "not byte "  + rm;
                if (subOp == 0) { byte imm8 = b[ip++]; return string.Format("test byte {0}, 0x{1:X2}", rm, imm8); }
                return string.Format("F6 /{0} {1}", subOp, rm);
            }

            // Group 5: FF (INC, DEC, CALL, JMP, PUSH) + FE (INC/DEC byte)
            if (opfx == 0xFE)
            {
                byte modrm = b[ip]; int subOp = (modrm >> 3) & 7;
                DecodeModRM(b, ref ip, rexR, rexB, rexX, false, out dummy, out rm, true, false);
                if (subOp == 0) return "inc byte " + rm;
                if (subOp == 1) return "dec byte " + rm;
                return string.Format("FE /{0} {1}", subOp, rm);
            }

            if (opfx == 0xFF)
            {
                byte modrm = b[ip]; int subOp = (modrm >> 3) & 7;
                DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out dummy, out rm, false, false);
                string sym = "";
                if (rm.StartsWith("[rip "))
                {
                    int plusIdx = rm.IndexOf('+');
                    if (plusIdx > 0)
                    {
                        string hexD = rm.Substring(plusIdx+1).TrimEnd(']').Trim();
                        if (hexD.StartsWith("0x")) hexD = hexD.Substring(2);
                        try { ulong disp = Convert.ToUInt64(hexD, 16); if (Symbols.ContainsKey(disp)) sym = " <" + Symbols[disp] + ">"; } catch { }
                    }
                }
                if (subOp == 2) return "call qword " + rm + sym;
                if (subOp == 4) return "jmp qword "  + rm + sym;
                if (subOp == 6) return "push qword " + rm;
                if (subOp == 0) return "inc " + rm;
                if (subOp == 1) return "dec " + rm;
                return string.Format("FF /{0} {1}", subOp, rm);
            }

            // MOV r/m, imm (C6 / C7)
            if (opfx == 0xC6) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out dummy, out rm, true, false);  byte i8 = b[ip++]; return string.Format("mov byte {0}, 0x{1:X2}", rm, i8); }
            if (opfx == 0xC7) { DecodeModRM(b, ref ip, rexR, rexB, rexX, rexW, out dummy, out rm, false, false); uint i32 = ReadUInt32(b, ref ip); return string.Format("mov {0} {1}, 0x{2:X}", rexW?"qword":"dword", rm, i32); }

            return string.Format("db 0x{0:X2}", opfx);
        }

        static int GetRegIndex(string name)
        {
            for (int i = 0; i < Reg64.Length; i++)
                if (Reg64[i] == name || Reg32[i] == name || Reg8[i] == name) return i;
            return 0;
        }

        static void DecodeModRM(byte[] b, ref int ip, bool rexR, bool rexB, bool rexX, bool rexW, out string regName, out string rmName, bool forceByteRm, bool forceByteReg)
        {
            byte modrm = b[ip++];
            int  mod   = (modrm >> 6) & 3;
            int  reg   = ((modrm >> 3) & 7) + (rexR ? 8 : 0);
            int  rm    = (modrm & 7)         + (rexB ? 8 : 0);

            regName = forceByteReg ? Reg8[reg] : (rexW ? Reg64[reg] : Reg32[reg]);

            if (mod == 3)
            {
                rmName = forceByteRm ? Reg8[rm] : (rexW ? Reg64[rm] : Reg32[rm]);
                return;
            }

            string baseStr  = "";
            string indexStr = "";

            if ((modrm & 7) == 4) // SIB
            {
                byte sib   = b[ip++];
                int  scale = 1 << ((sib >> 6) & 3);
                int  index = ((sib >> 3) & 7) + (rexX ? 8 : 0);
                int  baseReg = (sib & 7)      + (rexB ? 8 : 0);

                if (index != 4)
                    indexStr = string.Format(" + {0}{1}", Reg64[index], scale > 1 ? "*" + scale : "");

                if (mod == 0 && (sib & 7) == 5)
                {
                    int disp32 = ReadInt32(b, ref ip);
                    rmName = string.Format("[0x{0:X}{1}]", disp32, indexStr);
                    return;
                }
                baseStr = Reg64[baseReg];
            }
            else if (mod == 0 && (modrm & 7) == 5) // RIP-relative
            {
                int disp32 = ReadInt32(b, ref ip);
                rmName = string.Format("[rip {0} 0x{1:X}]", disp32 >= 0 ? "+" : "-", Math.Abs(disp32));
                return;
            }
            else
            {
                baseStr = Reg64[rm];
            }

            if (mod == 1)
            {
                sbyte d8 = (sbyte)b[ip++];
                rmName = string.Format("[{0}{1} {2} 0x{3:X}]", baseStr, indexStr, d8 >= 0 ? "+" : "-", Math.Abs(d8));
            }
            else if (mod == 2)
            {
                int d32 = ReadInt32(b, ref ip);
                rmName = string.Format("[{0}{1} {2} 0x{3:X}]", baseStr, indexStr, d32 >= 0 ? "+" : "-", Math.Abs(d32));
            }
            else
            {
                rmName = string.Format("[{0}{1}]", baseStr, indexStr);
            }
        }

        static ushort ReadUInt16(byte[] b, ref int ip) { ushort v = BitConverter.ToUInt16(b, ip); ip += 2; return v; }
        static uint   ReadUInt32(byte[] b, ref int ip) { uint   v = BitConverter.ToUInt32(b, ip); ip += 4; return v; }
        static int    ReadInt32 (byte[] b, ref int ip) { int    v = BitConverter.ToInt32 (b, ip); ip += 4; return v; }
        static ulong  ReadUInt64(byte[] b, ref int ip) { ulong  v = BitConverter.ToUInt64(b, ip); ip += 8; return v; }
    }
}
