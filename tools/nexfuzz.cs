using System;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Threading;
using System.Collections.Generic;

namespace NexusTools
{
    class NexFuzz
    {
        static Random rnd = new Random();

        enum FuzzMode { Basic, Functions, FileIO, Division, Full }

        static int Main(string[] args)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.WriteLine(" [NEXUS] NEXFUZZ v2.0 - Automated Grammar & Stress Fuzzer ");
            Console.WriteLine("==========================================================");
            Console.ResetColor();

            // Defaults
            string   compilerExe   = args.Length >= 1 ? args[0] : @"..\compiler\nexc.exe";
            int      iterations    = 20;
            FuzzMode mode          = FuzzMode.Full;
            int      seed          = Environment.TickCount;
            string   failuresDir   = null;
            string   replayFile    = null;

            for (int i = 1; i < args.Length; i++)
            {
                string a = args[i];
                if      (a.Equals("--iter",          StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) iterations  = int.Parse(args[++i]);
                else if (a.Equals("--seed",          StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) seed        = int.Parse(args[++i]);
                else if (a.Equals("--save-failures", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) failuresDir = args[++i];
                else if (a.Equals("--replay",        StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) replayFile  = args[++i];
                else if (a.Equals("--mode",          StringComparison.OrdinalIgnoreCase) && i+1 < args.Length)
                {
                    string m = args[++i].ToLower();
                    if      (m == "basic")     mode = FuzzMode.Basic;
                    else if (m == "functions") mode = FuzzMode.Functions;
                    else if (m == "fileio")    mode = FuzzMode.FileIO;
                    else if (m == "division")  mode = FuzzMode.Division;
                    else if (m == "full")      mode = FuzzMode.Full;
                    else { Console.WriteLine("[-] Unknown mode: " + m); return 1; }
                }
                else if (i == 1 && !a.StartsWith("-"))
                {
                    // Positional backwards compat: nexfuzz <compiler> <N>
                    int n;
                    if (int.TryParse(a, out n)) iterations = n;
                }
            }

            rnd = new Random(seed);

            compilerExe = Path.GetFullPath(compilerExe);
            if (!File.Exists(compilerExe))
            {
                Console.WriteLine("[-] Compiler not found: " + compilerExe);
                return 1;
            }

            string compDir      = Path.GetDirectoryName(compilerExe);
            string codeNex      = Path.Combine(compDir, "code.nex");
            string appExe       = Path.Combine(compDir, "app.exe");
            string backupNex    = Path.Combine(compDir, "code.nex.fuzzbak");

            if (failuresDir != null && !Directory.Exists(failuresDir))
                Directory.CreateDirectory(failuresDir);

            Console.WriteLine("[+] Compiler:    {0}", compilerExe);
            Console.WriteLine("[+] Seed:        {0}", seed);

            // --- Replay mode ---
            if (replayFile != null)
            {
                replayFile = Path.GetFullPath(replayFile);
                if (!File.Exists(replayFile))
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("[-] Replay file not found: " + replayFile);
                    Console.ResetColor();
                    return 1;
                }
                Console.WriteLine("[+] Mode:        REPLAY - {0}", replayFile);
                Console.WriteLine();
                string src = File.ReadAllText(replayFile);
                Console.WriteLine(src);
                Console.WriteLine("--- Compiling... ---");
                File.Copy(replayFile, codeNex, true);
                SafeDelete(appExe);
                string dummyOut;
                string err = RunProc(compilerExe, compDir, 5000, null, out dummyOut);
                if (err != null) { Console.ForegroundColor = ConsoleColor.Red; Console.WriteLine("FAIL: " + err); Console.ResetColor(); return 1; }
                Console.WriteLine("--- Running app.exe... ---");
                string outp;
                err = RunProc(appExe, compDir, 3000, null, out outp);
                Console.Write(outp);
                if (err != null) { Console.ForegroundColor = ConsoleColor.Red; Console.WriteLine("FAIL: " + err); Console.ResetColor(); return 1; }
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("PASS");
                Console.ResetColor();
                return 0;
            }

            Console.WriteLine("[+] Mode:        {0}", mode);
            Console.WriteLine("[+] Iterations:  {0}", iterations);
            if (failuresDir != null) Console.WriteLine("[+] Save fails:  {0}", failuresDir);
            Console.WriteLine();

            if (File.Exists(codeNex)) File.Copy(codeNex, backupNex, true);

            int passed = 0, failed = 0, crashes = 0;

            try
            {
                for (int i = 1; i <= iterations; i++)
                {
                    FuzzMode thisMode = mode;
                    if (mode == FuzzMode.Full)
                        thisMode = (FuzzMode)(rnd.Next(0, 4)); // Basic/Functions/FileIO/Division

                    string fuzzSrc = GenerateProgram(thisMode, i);
                    string modeTag = thisMode.ToString().ToUpper();

                    File.WriteAllText(codeNex, fuzzSrc);
                    SafeDelete(appExe);

                    string failureReason = null;
                    string compStdout;
                    string runStdout;

                    // Compile
                    string compErr = RunProc(compilerExe, compDir, 3000, null, out compStdout);
                    if (compErr != null)
                    {
                        if (compErr.Contains("Timeout")) failureReason = "COMPILER HANG";
                        else { failureReason = "COMPILER CRASH: " + compErr; crashes++; }
                    }

                    if (failureReason == null && !File.Exists(appExe))
                        failureReason = "NO app.exe PRODUCED";

                    // Run
                    if (failureReason == null)
                    {
                        string runErr = RunProc(appExe, compDir, 2000, null, out runStdout);
                        if (runErr != null)
                        {
                            if (runErr.Contains("Timeout")) failureReason = "RUNTIME INFINITE LOOP";
                            else failureReason = "RUNTIME CRASH: " + runErr;
                        }
                    }

                    if (failureReason != null)
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("  [-] Test #{0:D3} [{1,-9}]: FAIL - {2}", i, modeTag, failureReason);
                        Console.ResetColor();
                        failed++;

                        // Save failure for replay
                        if (failuresDir != null)
                        {
                            string failPath = Path.Combine(failuresDir, string.Format("fuzz_fail_{0:D3}.nex", i));
                            File.WriteAllText(failPath, fuzzSrc);
                            Console.ForegroundColor = ConsoleColor.DarkYellow;
                            Console.WriteLine("      Saved to: " + failPath);
                            Console.ResetColor();
                        }
                    }
                    else
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("  [+] Test #{0:D3} [{1,-9}]: PASS", i, modeTag);
                        Console.ResetColor();
                        passed++;
                    }

                    Thread.Sleep(30);
                }
            }
            finally
            {
                if (File.Exists(backupNex))
                {
                    File.Copy(backupNex, codeNex, true);
                    File.Delete(backupNex);
                }
            }

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.WriteLine(" NEXFUZZ RESULTS: {0}/{1} Passed ({2:F1}%)", passed, iterations, (double)passed/iterations*100.0);
            Console.WriteLine(" Compiler Crashes: {0}  |  Failures: {1}", crashes, failed);
            Console.WriteLine("==========================================================");
            Console.ResetColor();
            return failed > 0 ? 1 : 0;
        }

        // Runs a process, returns null on success or error description on failure
        static string RunProc(string exe, string workDir, int timeoutMs, byte[] stdinBytes, out string stdout)
        {
            stdout = "";
            var psi = new ProcessStartInfo(exe)
            {
                WorkingDirectory       = workDir,
                UseShellExecute        = false,
                RedirectStandardOutput = true,
                RedirectStandardError  = true,
                RedirectStandardInput  = stdinBytes != null,
                CreateNoWindow         = true
            };
            StringBuilder sb = new StringBuilder();
            try
            {
                using (Process p = Process.Start(psi))
                {
                    if (stdinBytes != null) { p.StandardInput.BaseStream.Write(stdinBytes, 0, stdinBytes.Length); p.StandardInput.Close(); }
                    p.OutputDataReceived += (s, e) => { if (e.Data != null) sb.AppendLine(e.Data); };
                    p.BeginOutputReadLine();
                    p.ErrorDataReceived += (s, e) => { };
                    p.BeginErrorReadLine();
                    bool done = p.WaitForExit(timeoutMs);
                    if (!done) { try { p.Kill(); } catch { } stdout = sb.ToString(); return "Timeout (>" + timeoutMs + "ms)"; }
                    p.WaitForExit();
                    stdout = sb.ToString();
                    return p.ExitCode == 0 ? null : string.Format("exit 0x{0:X8}", p.ExitCode);
                }
            }
            catch (Exception ex) { return "Exception: " + ex.Message; }
        }

        static void SafeDelete(string path)
        {
            if (!File.Exists(path)) return;
            for (int r = 0; r < 5; r++) { try { File.Delete(path); return; } catch { Thread.Sleep(50); } }
        }

        static string GenerateProgram(FuzzMode mode, int seed)
        {
            rnd = new Random(rnd.Next()); // advance RNG each call
            switch (mode)
            {
                case FuzzMode.Functions: return GenerateFuzzFunctions();
                case FuzzMode.FileIO:    return GenerateFuzzFileIO();
                case FuzzMode.Division:  return GenerateFuzzDivision();
                default:                 return GenerateFuzzBasic(seed);
            }
        }

        static string GenerateFuzzBasic(int seed)
        {
            var sb = new StringBuilder();
            sb.AppendLine("# Fuzz test basic #" + seed);
            sb.AppendLine("let a = " + rnd.Next(1, 100));
            sb.AppendLine("let b = " + rnd.Next(1, 50));
            sb.AppendLine("let c = 0");

            // Chained math
            sb.Append("let x = a");
            for (int j = 0; j < rnd.Next(2, 7); j++)
            {
                int op = rnd.Next(0, 4);
                int n  = rnd.Next(1, 20);
                if (op == 0) sb.Append(" + " + n);
                else if (op == 1) sb.Append(" - " + n);
                else if (op == 2) sb.Append(" * " + n);
                else             sb.Append(" + " + n); // avoid div-by-zero in expr
            }
            sb.AppendLine();

            // Nested loops
            int outerMax = rnd.Next(2, 5);
            int innerMax = rnd.Next(2, 5);
            sb.AppendLine("let i = 1");
            sb.AppendLine("while i <= " + outerMax + " {");
            sb.AppendLine("    let j = 1");
            sb.AppendLine("    while j <= " + innerMax + " {");
            sb.AppendLine("        if i == j {");
            sb.AppendLine("            let c = c + 1");
            sb.AppendLine("        } else {");
            sb.AppendLine("            let c = c + 2");
            sb.AppendLine("        }");
            sb.AppendLine("        let j = j + 1");
            sb.AppendLine("    }");
            sb.AppendLine("    let i = i + 1");
            sb.AppendLine("}");

            // Heap
            sb.AppendLine("let p = alloc 128");
            sb.AppendLine("store [p + 0] 42");
            sb.AppendLine("let v = load [p + 0]");
            sb.AppendLine("print c");
            sb.AppendLine("print v");
            return sb.ToString();
        }

        static string GenerateFuzzFunctions()
        {
            var sb = new StringBuilder();
            sb.AppendLine("# Fuzz test functions");

            // Generate 2-3 functions
            int numFns = rnd.Next(2, 4);
            string[] fnNames = { "compute_a", "compute_b", "compute_c" };
            int[] fnAddends  = { rnd.Next(1,50), rnd.Next(1,50), rnd.Next(1,50) };

            for (int fi = 0; fi < numFns; fi++)
            {
                sb.AppendLine("fn " + fnNames[fi] + " {");
                sb.AppendLine("    let r = a + " + fnAddends[fi]);
                sb.AppendLine("    let a = r");
                // Optionally call previous fn
                if (fi > 0 && rnd.Next(0, 2) == 0)
                    sb.AppendLine("    call " + fnNames[fi-1]);
                sb.AppendLine("    return");
                sb.AppendLine("}");
            }

            sb.AppendLine("let a = " + rnd.Next(1, 20));
            sb.AppendLine("let start = a");

            for (int fi = 0; fi < numFns; fi++)
                sb.AppendLine("call " + fnNames[fi]);

            sb.AppendLine("print a");

            // While loop calling a function
            sb.AppendLine("let a = 0");
            sb.AppendLine("let i = 0");
            sb.AppendLine("while i < 3 {");
            sb.AppendLine("    call " + fnNames[0]);
            sb.AppendLine("    let i = i + 1");
            sb.AppendLine("}");
            sb.AppendLine("print a");
            return sb.ToString();
        }

        static string GenerateFuzzDivision()
        {
            var sb = new StringBuilder();
            sb.AppendLine("# Fuzz test division edge cases");

            // Various divisors — always >= 1 to avoid deliberate crash
            int[] divisors = new int[6];
            for (int i = 0; i < divisors.Length; i++)
                divisors[i] = rnd.Next(1, 100);

            sb.AppendLine("let a = " + rnd.Next(1, 10000));
            sb.AppendLine("let b = " + rnd.Next(1, 10000));

            for (int i = 0; i < divisors.Length; i++)
            {
                int d = divisors[i];
                sb.AppendLine("let r" + i + " = a / " + d);
                sb.AppendLine("let m" + i + " = b % " + d);
            }

            // Chained modulo
            sb.AppendLine("let x = a % " + divisors[0] + " + b % " + divisors[1] + " + a % " + divisors[2]);
            sb.AppendLine("let y = x / " + divisors[3] + " + 1");

            // Nested division in while
            sb.AppendLine("let n = " + rnd.Next(10, 50));
            sb.AppendLine("let acc = 0");
            sb.AppendLine("while n > 0 {");
            sb.AppendLine("    let acc = acc + n % " + divisors[4]);
            sb.AppendLine("    let n = n / " + divisors[5]);
            sb.AppendLine("}");

            for (int i = 0; i < divisors.Length; i++)
                sb.AppendLine("print r" + i);
            sb.AppendLine("print x");
            sb.AppendLine("print y");
            sb.AppendLine("print acc");
            return sb.ToString();
        }

        static string GenerateFuzzFileIO()
        {
            var sb = new StringBuilder();
            sb.AppendLine("# Fuzz test file I/O");

            // Write numbers to a temp file then read them back
            int numValues = rnd.Next(3, 8);
            int[] values  = new int[numValues];
            for (int i = 0; i < numValues; i++) values[i] = rnd.Next(1, 10000);

            sb.AppendLine("let fh = file_create \"fuzz_tmp.dat\"");
            sb.AppendLine("let buf = alloc 128");

            // Store values as byte sequences
            for (int i = 0; i < numValues; i++)
            {
                int v = values[i];
                // Write single-byte representation (value % 256) for simplicity
                sb.AppendLine("store [buf + " + i + "] " + (v % 200 + 1));
            }
            sb.AppendLine("file_write fh buf " + numValues);
            sb.AppendLine("file_close fh");

            // Re-open and read back
            sb.AppendLine("let fh2 = file_open \"fuzz_tmp.dat\"");
            sb.AppendLine("let buf2 = alloc 128");
            sb.AppendLine("file_read fh2 buf2 " + numValues);
            sb.AppendLine("file_close fh2");

            // Verify: load back and print
            for (int i = 0; i < numValues; i++)
                sb.AppendLine("let v" + i + " = load [buf2 + " + i + "]");

            for (int i = 0; i < numValues; i++)
                sb.AppendLine("print v" + i);

            return sb.ToString();
        }
    }
}
