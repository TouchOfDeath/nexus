using System;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace NexusTools
{
    class NexDebug
    {
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        static extern bool CreateProcess(string lpApplicationName, string lpCommandLine,
            IntPtr lpPA, IntPtr lpTA, bool bInherit, uint dwFlags, IntPtr lpEnv,
            string lpCurDir, [In] ref STARTUPINFO si, out PROCESS_INFORMATION pi);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool WaitForDebugEvent([Out] byte[] lpDebugEvent, uint dwMs);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool ContinueDebugEvent(uint dwPid, uint dwTid, uint dwStatus);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr OpenThread(uint dwAccess, bool bInherit, uint dwTid);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr h);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetThreadContext(IntPtr hThread, IntPtr lpCtx);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool ReadProcessMemory(IntPtr hProc, IntPtr lpBase, [Out] byte[] lpBuf, int sz, out IntPtr nRead);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateProcess(IntPtr hProc, uint uExit);

        const uint DEBUG_ONLY_THIS_PROCESS    = 0x00000002;
        const uint DBG_CONTINUE               = 0x00010002;
        const uint DBG_EXCEPTION_NOT_HANDLED  = 0x80010001;
        const uint EXCEPTION_DEBUG_EVENT      = 1;
        const uint EXIT_PROCESS_DEBUG_EVENT   = 5;
        const uint EXCEPTION_ACCESS_VIOLATION = 0xC0000005;
        const uint EXCEPTION_INT_DIV_ZERO     = 0xC0000094;
        const uint EXCEPTION_ILLEGAL_INSTR    = 0xC000001D;
        const uint EXCEPTION_STACK_OVERFLOW   = 0xC00000FD;
        const uint EXCEPTION_BREAKPOINT       = 0x80000003;
        const uint THREAD_ALL_ACCESS          = 0x1FFFFF;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
        struct STARTUPINFO
        {
            public uint cb, dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            public short wShowWindow, cbReserved2;
            public string lpReserved, lpDesktop, lpTitle;
            public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct PROCESS_INFORMATION
        {
            public IntPtr hProcess, hThread;
            public uint   dwProcessId, dwThreadId;
        }

        // Watch-memory request
        static ulong watchAddr = 0;
        static int   watchSize = 0;

        static int Main(string[] args)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.WriteLine(" [NEXUS] NEXDEBUG v3.0 - Native Win32 Crash Interceptor   ");
            Console.WriteLine("==========================================================");
            Console.ResetColor();

            if (args.Length == 0)
            {
                Console.WriteLine("Usage: nexdebug.exe <target.exe> [options] [-- extra_args]");
                Console.WriteLine("Options:");
                Console.WriteLine("  --timeout <ms>          Per-run timeout (default: 5000)");
                Console.WriteLine("  --watch <hex_addr> <n>  Dump n bytes from address after crash");
                return 0;
            }

            string        targetExe  = "";
            uint          timeoutMs  = 5000;
            StringBuilder cmdArgs    = new StringBuilder();

            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i];
                if (a.Equals("--timeout", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length)
                    timeoutMs = uint.Parse(args[++i]);
                else if (a.Equals("--watch", StringComparison.OrdinalIgnoreCase) && i+2 < args.Length)
                {
                    string hexA = args[++i]; if (hexA.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) hexA = hexA.Substring(2);
                    watchAddr = Convert.ToUInt64(hexA, 16);
                    watchSize = int.Parse(args[++i]);
                }
                else if (string.IsNullOrEmpty(targetExe))
                    targetExe = a;
                else
                    cmdArgs.Append(" \"").Append(a).Append("\"");
            }

            if (!File.Exists(targetExe))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] File not found: " + targetExe);
                Console.ResetColor();
                return 3;
            }

            string fullTarget = Path.GetFullPath(targetExe);
            string cmdLine    = "\"" + fullTarget + "\"" + cmdArgs;
            string curDir     = Path.GetDirectoryName(fullTarget);

            STARTUPINFO       si = new STARTUPINFO();
            si.cb = (uint)Marshal.SizeOf(si);
            PROCESS_INFORMATION pi;

            Console.WriteLine("[+] Target:  {0}", Path.GetFileName(targetExe));
            Console.WriteLine("[+] Timeout: {0} ms", timeoutMs);
            if (watchSize > 0)
                Console.WriteLine("[+] Watch:   0x{0:X16} ({1} bytes)", watchAddr, watchSize);

            bool created = CreateProcess(null, cmdLine, IntPtr.Zero, IntPtr.Zero, false,
                                         DEBUG_ONLY_THIS_PROCESS, IntPtr.Zero, curDir, ref si, out pi);
            if (!created)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] Failed to launch. Error: " + Marshal.GetLastWin32Error());
                Console.ResetColor();
                return 3;
            }

            byte[]    dbgEvent = new byte[256];
            bool      running  = true;
            bool      hitInitBp = false;
            Stopwatch timer    = Stopwatch.StartNew();
            int       exitCode = 0;

            while (running)
            {
                long elapsed = timer.ElapsedMilliseconds;
                if (elapsed >= timeoutMs)
                {
                    Console.WriteLine();
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("*****************************************************************");
                    Console.WriteLine(" [!] TIMEOUT: Execution exceeded {0} ms (possible infinite loop)", timeoutMs);
                    Console.WriteLine("*****************************************************************");
                    Console.ResetColor();
                    TerminateProcess(pi.hProcess, 0xC0000000);
                    exitCode = 2;
                    break;
                }

                uint slice = (uint)Math.Max(10, Math.Min(100, timeoutMs - elapsed));
                if (!WaitForDebugEvent(dbgEvent, slice)) continue;

                uint eventCode     = BitConverter.ToUInt32(dbgEvent, 0);
                uint pid           = BitConverter.ToUInt32(dbgEvent, 4);
                uint tid           = BitConverter.ToUInt32(dbgEvent, 8);
                uint continueStatus = DBG_CONTINUE;

                if (eventCode == EXIT_PROCESS_DEBUG_EVENT)
                {
                    uint code = BitConverter.ToUInt32(dbgEvent, 16);
                    Console.WriteLine();
                    if (code == 0)
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("[+] Process exited cleanly (exit 0) in {0} ms.", timer.ElapsedMilliseconds);
                    }
                    else
                    {
                        Console.ForegroundColor = ConsoleColor.Yellow;
                        Console.WriteLine("[!] Process exited with code 0x{0:X8} ({1}) in {2} ms.", code, (int)code, timer.ElapsedMilliseconds);
                    }
                    Console.ResetColor();
                    exitCode = (int)code;
                    running  = false;
                }
                else if (eventCode == EXCEPTION_DEBUG_EVENT)
                {
                    uint  exCode   = BitConverter.ToUInt32(dbgEvent, 16);
                    ulong exAddr   = BitConverter.ToUInt64(dbgEvent, 32);
                    uint  numParams = BitConverter.ToUInt32(dbgEvent, 40);

                    if (exCode == EXCEPTION_BREAKPOINT)
                    {
                        if (!hitInitBp) { hitInitBp = true; continueStatus = DBG_CONTINUE; }
                    }
                    else if (exCode == EXCEPTION_ACCESS_VIOLATION ||
                             exCode == EXCEPTION_INT_DIV_ZERO     ||
                             exCode == EXCEPTION_ILLEGAL_INSTR    ||
                             exCode == EXCEPTION_STACK_OVERFLOW)
                    {
                        Console.WriteLine();
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("*****************************************************************");
                        Console.WriteLine(" [***] FATAL CRASH INTERCEPTED BY NEXDEBUG v3.0 [***]");
                        Console.WriteLine("*****************************************************************");
                        Console.ResetColor();

                        string desc = exCode == EXCEPTION_ACCESS_VIOLATION ? "ACCESS VIOLATION (0xC0000005)"        :
                                      exCode == EXCEPTION_INT_DIV_ZERO     ? "INTEGER DIV-BY-ZERO (0xC0000094)"    :
                                      exCode == EXCEPTION_ILLEGAL_INSTR    ? "ILLEGAL INSTRUCTION (0xC000001D)"    :
                                                                              "STACK OVERFLOW (0xC00000FD)";
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("  Fault Type:    {0}", desc);
                        Console.WriteLine("  Fault Address: 0x{0:X16}", exAddr);
                        Console.ResetColor();

                        if (exCode == EXCEPTION_ACCESS_VIOLATION && numParams >= 2)
                        {
                            ulong accessType = BitConverter.ToUInt64(dbgEvent, 48);
                            ulong faultAddr  = BitConverter.ToUInt64(dbgEvent, 56);
                            string op = accessType == 0 ? "READ" : accessType == 1 ? "WRITE" : "EXECUTE";
                            Console.WriteLine("  Memory Op:     Attempted {0} at 0x{1:X16}", op, faultAddr);
                        }

                        IntPtr hThread = OpenThread(THREAD_ALL_ACCESS, false, tid);
                        if (hThread != IntPtr.Zero)
                        {
                            DumpContextAndStack(pi.hProcess, hThread, exAddr);
                            CloseHandle(hThread);
                        }

                        // --watch dump
                        if (watchSize > 0)
                        {
                            byte[] watchBuf = new byte[watchSize];
                            IntPtr nRead;
                            if (ReadProcessMemory(pi.hProcess, (IntPtr)watchAddr, watchBuf, watchSize, out nRead))
                            {
                                Console.WriteLine();
                                Console.ForegroundColor = ConsoleColor.Cyan;
                                Console.WriteLine("--- WATCH MEMORY DUMP: 0x{0:X16} ({1} bytes) ---", watchAddr, watchSize);
                                Console.ResetColor();
                                PrintHexDump(watchBuf, (int)nRead, watchAddr);
                            }
                        }

                        TerminateProcess(pi.hProcess, exCode);
                        running  = false;
                        exitCode = 1;
                        continueStatus = DBG_CONTINUE;
                    }
                    else
                    {
                        continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                    }
                }

                ContinueDebugEvent(pid, tid, continueStatus);
            }

            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return exitCode;
        }

        static void DumpContextAndStack(IntPtr hProc, IntPtr hThread, ulong exAddr)
        {
            int   ctxSize     = 1232;
            IntPtr pCtx       = Marshal.AllocHGlobal(ctxSize + 16);
            long   rawAddr    = pCtx.ToInt64();
            long   alignedA   = (rawAddr + 15) & ~15L;
            IntPtr alignedCtx = new IntPtr(alignedA);

            Marshal.WriteInt32(alignedCtx, 0x30, 0x10000B); // CONTEXT_ALL x64

            if (GetThreadContext(hThread, alignedCtx))
            {
                ulong rax    = (ulong)Marshal.ReadInt64(alignedCtx, 0x78);
                ulong rcx    = (ulong)Marshal.ReadInt64(alignedCtx, 0x80);
                ulong rdx    = (ulong)Marshal.ReadInt64(alignedCtx, 0x88);
                ulong rbx    = (ulong)Marshal.ReadInt64(alignedCtx, 0x90);
                ulong rsp    = (ulong)Marshal.ReadInt64(alignedCtx, 0x98);
                ulong rbp    = (ulong)Marshal.ReadInt64(alignedCtx, 0xA0);
                ulong rsi    = (ulong)Marshal.ReadInt64(alignedCtx, 0xA8);
                ulong rdi    = (ulong)Marshal.ReadInt64(alignedCtx, 0xB0);
                ulong r8     = (ulong)Marshal.ReadInt64(alignedCtx, 0xB8);
                ulong r9     = (ulong)Marshal.ReadInt64(alignedCtx, 0xC0);
                ulong r10    = (ulong)Marshal.ReadInt64(alignedCtx, 0xC8);
                ulong r11    = (ulong)Marshal.ReadInt64(alignedCtx, 0xD0);
                ulong r12    = (ulong)Marshal.ReadInt64(alignedCtx, 0xD8);
                ulong r13    = (ulong)Marshal.ReadInt64(alignedCtx, 0xE0);
                ulong r14    = (ulong)Marshal.ReadInt64(alignedCtx, 0xE8);
                ulong r15    = (ulong)Marshal.ReadInt64(alignedCtx, 0xF0);
                ulong rip    = (ulong)Marshal.ReadInt64(alignedCtx, 0xF8);
                uint  eflags = (uint) Marshal.ReadInt32(alignedCtx, 0x44);

                string flagStr = string.Format("CF={0} PF={1} ZF={2} SF={3} OF={4} DF={5} IF={6}",
                    (eflags>>0)&1, (eflags>>2)&1, (eflags>>6)&1,
                    (eflags>>7)&1, (eflags>>11)&1, (eflags>>10)&1, (eflags>>9)&1);

                Console.WriteLine();
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("--- x86-64 HARDWARE REGISTER DUMP ---");
                Console.ResetColor();
                Console.WriteLine("  RIP = 0x{0:X16}   EFLAGS = 0x{1:X8}  [{2}]", rip, eflags, flagStr);
                Console.WriteLine("  RAX = 0x{0:X16}   RBX    = 0x{1:X16}", rax, rbx);
                Console.WriteLine("  RCX = 0x{0:X16}   RDX    = 0x{1:X16}", rcx, rdx);
                Console.WriteLine("  RSI = 0x{0:X16}   RDI    = 0x{1:X16}", rsi, rdi);
                Console.WriteLine("  RBP = 0x{0:X16}   RSP    = 0x{1:X16}", rbp, rsp);
                Console.WriteLine("  R8  = 0x{0:X16}   R9     = 0x{1:X16}", r8,  r9);
                Console.WriteLine("  R10 = 0x{0:X16}   R11    = 0x{1:X16}", r10, r11);
                Console.WriteLine("  R12 = 0x{0:X16}   R13    = 0x{1:X16}", r12, r13);
                Console.WriteLine("  R14 = 0x{0:X16}   R15    = 0x{1:X16}", r14, r15);

                // Mini-disasm: 6 instructions at RIP
                byte[] codeBytes = new byte[64];
                IntPtr nRead;
                if (ReadProcessMemory(hProc, (IntPtr)rip, codeBytes, codeBytes.Length, out nRead) && (int)nRead > 0)
                {
                    Console.WriteLine();
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("--- FAULTING CODE AT RIP (up to 6 instructions) ---");
                    Console.ResetColor();
                    int ipOff = 0;
                    for (int instrNum = 0; instrNum < 6 && ipOff < (int)nRead; instrNum++)
                    {
                        int    startOff = ipOff;
                        string asmStr   = MiniDecode(codeBytes, ref ipOff, (int)nRead);
                        int    len      = ipOff - startOff;

                        StringBuilder hexB = new StringBuilder();
                        for (int j = 0; j < len && (startOff+j) < (int)nRead; j++)
                            hexB.Append(codeBytes[startOff+j].ToString("X2")).Append(" ");

                        string marker = instrNum == 0 ? "  <-- FAULTING" : "";
                        Console.ForegroundColor = instrNum == 0 ? ConsoleColor.Red : ConsoleColor.Gray;
                        Console.WriteLine("  [RIP+0x{0:X2}]: {1,-18} -> {2}{3}", startOff, hexB.ToString().Trim(), asmStr, marker);
                        Console.ResetColor();
                    }
                }

                // Stack dump: 32 entries (256 bytes)
                byte[] stackBytes = new byte[256];
                if (ReadProcessMemory(hProc, (IntPtr)rsp, stackBytes, stackBytes.Length, out nRead) && (int)nRead >= 8)
                {
                    Console.WriteLine();
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("--- CALL STACK TRACE AT RSP (32 entries) ---");
                    Console.ResetColor();
                    for (int i = 0; i < (int)nRead; i += 8)
                    {
                        ulong  val  = BitConverter.ToUInt64(stackBytes, i);
                        string note = "";
                        if (val >= 0x401000 && val < 0x409000)
                            note = string.Format(" <-- [.text return addr, RVA 0x{0:X}]", val - 0x400000);
                        else if (val >= 0x406028 && val < 0x406060)
                            note = " <-- [IAT range]";
                        Console.WriteLine("  [RSP + 0x{0:X2}]: 0x{1:X16}{2}", i, val, note);
                    }
                }
            }
            Marshal.FreeHGlobal(pCtx);
        }

        static void PrintHexDump(byte[] buf, int len, ulong baseAddr)
        {
            for (int i = 0; i < len; i += 16)
            {
                Console.Write("  0x{0:X16}: ", baseAddr + (ulong)i);
                for (int j = 0; j < 16; j++)
                {
                    if (i+j < len) Console.Write("{0:X2} ", buf[i+j]);
                    else           Console.Write("   ");
                    if (j == 7) Console.Write(" ");
                }
                Console.Write(" |");
                for (int j = 0; j < 16 && i+j < len; j++)
                {
                    byte c = buf[i+j];
                    Console.Write(c >= 32 && c < 127 ? (char)c : '.');
                }
                Console.WriteLine("|");
            }
        }

        // Lightweight mini-decoder for crash context (no dependency on nexdisasm)
        static readonly string[] R64 = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15" };
        static readonly string[] R32 = { "eax","ecx","edx","ebx","esp","ebp","esi","edi","r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d" };
        static readonly string[] JCC = { "jo","jno","jb","jae","je","jne","jbe","ja","js","jns","jp","jnp","jl","jge","jle","jg" };

        static string MiniDecode(byte[] b, ref int ip, int len)
        {
            if (ip >= len) return "???";
            bool rexW = false, rexR = false, rexB = false;

            while (ip < len)
            {
                byte p = b[ip];
                if (p >= 0x40 && p <= 0x4F)
                {
                    rexW = (p & 0x08) != 0;
                    rexR = (p & 0x04) != 0;
                    rexB = (p & 0x01) != 0;
                    ip++;
                    break;
                }
                else if (p == 0x66 || p == 0xF2 || p == 0xF3) { ip++; }
                else break;
            }

            if (ip >= len) return "prefix";
            byte op = b[ip++];

            if (op == 0x90) return "nop";
            if (op == 0xC3) return "ret";
            if (op == 0xCC) return "int3";
            if (op == 0x99) return rexW ? "cqo" : "cdq";
            if (op == 0xC9) return "leave";

            if (op >= 0x50 && op <= 0x57) return "push " + R64[(op-0x50)+(rexB?8:0)];
            if (op >= 0x58 && op <= 0x5F) return "pop "  + R64[(op-0x58)+(rexB?8:0)];

            if (op == 0xE8 && ip+4 <= len) { int d = BitConverter.ToInt32(b, ip); ip+=4; return string.Format("call +0x{0:X}", d); }
            if (op == 0xE9 && ip+4 <= len) { int d = BitConverter.ToInt32(b, ip); ip+=4; return string.Format("jmp  +0x{0:X}", d); }
            if (op == 0xEB && ip   < len)  { sbyte d = (sbyte)b[ip++]; return string.Format("jmp short {0}", d); }

            if (op >= 0x70 && op <= 0x7F && ip < len) { sbyte d = (sbyte)b[ip++]; return JCC[op-0x70] + " short " + d; }

            if (op >= 0xB8 && op <= 0xBF && ip+4 <= len)
            {
                int rIdx = (op-0xB8)+(rexB?8:0);
                if (rexW && ip+8 <= len) { ulong v = BitConverter.ToUInt64(b, ip); ip+=8; return string.Format("mov {0}, 0x{1:X16}", R64[rIdx], v); }
                else { uint v = BitConverter.ToUInt32(b, ip); ip+=4; return string.Format("mov {0}, 0x{1:X8}", R32[rIdx], v); }
            }

            if (op == 0x0F && ip < len)
            {
                byte op2 = b[ip++];
                if (op2 == 0x05) return "syscall";
                if (op2 >= 0x80 && op2 <= 0x8F && ip+4 <= len) { int d = BitConverter.ToInt32(b, ip); ip+=4; return JCC[op2-0x80]+" +0x"+d.ToString("X"); }
                if (ip < len) { ip++; } // skip ModRM
                return string.Format("0F {0:X2} ...", op2);
            }

            // ModRM opcodes: skip 1 byte ModRM (crude)
            if ((op == 0x89 || op == 0x8B || op == 0x88 || op == 0x8A) && ip < len)
            {
                byte modrm = b[ip]; int mod = (modrm>>6)&3; int reg = (modrm>>3)&7; int rm = modrm&7;
                ip++;
                if (mod != 3 && rm == 4 && ip < len) ip++; // SIB
                if (mod == 1 && ip < len) ip++;            // disp8
                else if ((mod == 2 || (mod == 0 && rm == 5)) && ip+4 <= len) ip+=4; // disp32
                string dst = rexW ? R64[(op==0x89||op==0x88)?(reg+(rexB?8:0)):(rm+(rexB?8:0))] : R32[(op==0x89||op==0x88)?(reg+(rexB?8:0)):(rm+(rexB?8:0))];
                string src = rexW ? R64[(op==0x89||op==0x88)?(rm+(rexB?8:0)):(reg+(rexR?8:0))]  : R32[(op==0x89||op==0x88)?(rm+(rexB?8:0)):(reg+(rexR?8:0))];
                return string.Format("mov {0}, {1}", dst, src);
            }

            if ((op == 0x83 || op == 0x81) && ip+1 < len)
            {
                byte modrm = b[ip]; int subOp = (modrm>>3)&7; int rm = modrm&7;
                string[] g1 = { "add","or","adc","sbb","and","sub","xor","cmp" };
                ip++;
                if (((modrm>>6)&3) != 3 && rm == 4 && ip < len) ip++; // SIB
                byte mod = (byte)((modrm>>6)&3);
                if (mod == 1 && ip < len) ip++;
                else if ((mod == 2 || (mod == 0 && rm == 5)) && ip+4 <= len) ip+=4;
                string rName = R64[(rm+(rexB?8:0))%16];
                if (op == 0x83 && ip < len) { sbyte i = (sbyte)b[ip++]; return string.Format("{0} {1}, {2}", g1[subOp], rName, i); }
                if (op == 0x81 && ip+4 <= len) { uint i = BitConverter.ToUInt32(b, ip); ip+=4; return string.Format("{0} {1}, 0x{2:X}", g1[subOp], rName, i); }
            }

            if (op == 0xF7 && ip < len)
            {
                byte modrm = b[ip++]; int subOp = (modrm>>3)&7; int rm = modrm&7;
                // skip SIB/disp
                byte mod = (byte)((modrm>>6)&3);
                if (mod != 3 && rm == 4 && ip < len) ip++;
                if (mod == 1 && ip < len) ip++;
                else if ((mod == 2 || (mod == 0 && rm == 5)) && ip+4 <= len) ip+=4;
                string[] ops = { "test","?","not","neg","mul","imul","div","idiv" };
                return ops[subOp] + " " + R64[(rm+(rexB?8:0))%16];
            }

            if (op == 0xFF && ip < len)
            {
                byte modrm = b[ip++]; int subOp = (modrm>>3)&7; int rm = modrm&7;
                byte mod = (byte)((modrm>>6)&3);
                if (mod != 3 && rm == 4 && ip < len) ip++;
                if (mod == 1 && ip < len) ip++;
                else if ((mod == 2 || (mod == 0 && rm == 5)) && ip+4 <= len) ip+=4;
                if (subOp == 2) return "call [" + R64[(rm+(rexB?8:0))%16] + "]";
                if (subOp == 4) return "jmp  [" + R64[(rm+(rexB?8:0))%16] + "]";
                return string.Format("FF/{0}", subOp);
            }

            return string.Format("op 0x{0:X2}", op);
        }
    }
}
