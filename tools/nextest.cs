using System;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Collections.Generic;

namespace NexusTools
{
    class NexTest
    {
        static int Main(string[] args)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.WriteLine(" [NEXUS] NEXTEST v1.0 - Compiler Regression Test Runner  ");
            Console.WriteLine("==========================================================");
            Console.ResetColor();

            if (args.Length == 0)
            {
                PrintUsage();
                return 0;
            }

            // --- Argument parsing ---
            string compilerExe   = args[0];
            string testsDir      = null;
            int    iterPerTest   = 1;
            int    timeoutMs     = 5000;
            string globalStdin   = null;
            string filterStr     = null;
            bool   stopOnFail    = false;
            bool   verbose       = false;
            bool   genExpected   = false;

            for (int i = 1; i < args.Length; i++)
            {
                string a = args[i];
                if (a.Equals("--tests",             StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) { testsDir    = args[++i]; }
                else if (a.Equals("--iter",          StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) { iterPerTest = int.Parse(args[++i]); }
                else if (a.Equals("--timeout",       StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) { timeoutMs   = int.Parse(args[++i]); }
                else if (a.Equals("--stdin",         StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) { globalStdin = args[++i]; }
                else if (a.Equals("--filter",        StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) { filterStr   = args[++i]; }
                else if (a.Equals("--stop-on-fail",  StringComparison.OrdinalIgnoreCase))                      { stopOnFail  = true; }
                else if (a.Equals("--verbose",       StringComparison.OrdinalIgnoreCase))                      { verbose     = true; }
                else if (a.Equals("--generate-expected", StringComparison.OrdinalIgnoreCase))                  { genExpected = true; }
            }

            compilerExe = Path.GetFullPath(compilerExe);
            if (!File.Exists(compilerExe))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] Compiler not found: " + compilerExe);
                Console.ResetColor();
                return 1;
            }

            string compilerDir = Path.GetDirectoryName(compilerExe);
            string codeNexPath = Path.Combine(compilerDir, "code.nex");
            string appExePath  = Path.Combine(compilerDir, "app.exe");

            if (testsDir == null)
                testsDir = Path.GetFullPath(Path.Combine(compilerDir, "..", "examples"));

            testsDir = Path.GetFullPath(testsDir);
            if (!Directory.Exists(testsDir))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] Tests directory not found: " + testsDir);
                Console.ResetColor();
                return 1;
            }

            // Load global stdin bytes
            byte[] globalStdinBytes = null;
            if (globalStdin != null && File.Exists(globalStdin))
                globalStdinBytes = File.ReadAllBytes(globalStdin);

            // Discover test files
            string[] nexFiles = Directory.GetFiles(testsDir, "*.nex");
            Array.Sort(nexFiles);

            if (nexFiles.Length == 0)
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("[!] No .nex files found in: " + testsDir);
                Console.ResetColor();
                return 0;
            }

            // Apply filter
            List<string> testList = new List<string>();
            foreach (string f in nexFiles)
            {
                if (filterStr == null || Path.GetFileName(f).IndexOf(filterStr, StringComparison.OrdinalIgnoreCase) >= 0)
                    testList.Add(f);
            }

            Console.WriteLine("[+] Compiler:    {0}", compilerExe);
            Console.WriteLine("[+] Tests Dir:   {0}", testsDir);
            Console.WriteLine("[+] Tests Found: {0} ({1} after filter)", nexFiles.Length, testList.Count);
            Console.WriteLine("[+] Mode:        {0}", genExpected ? "GENERATE EXPECTED OUTPUT" : "REGRESSION TEST");
            Console.WriteLine();

            if (genExpected)
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("[!] GENERATE MODE: Will overwrite .expected files with current output.");
                Console.ResetColor();
                Console.WriteLine();
            }

            // --- Run tests ---
            int passed   = 0;
            int failed   = 0;
            int generated = 0;
            List<string> failures = new List<string>();

            foreach (string nexFile in testList)
            {
                string testName   = Path.GetFileName(nexFile);
                string baseName   = Path.GetFileNameWithoutExtension(nexFile);
                string expectedFile = Path.ChangeExtension(nexFile, ".expected");
                string perTestStdinFile = Path.ChangeExtension(nexFile, ".stdin");

                bool hasExpected = File.Exists(expectedFile);

                // Per-test stdin overrides global
                byte[] stdinBytes = globalStdinBytes;
                if (File.Exists(perTestStdinFile))
                    stdinBytes = File.ReadAllBytes(perTestStdinFile);

                Console.Write("  [{0,-35}] ", testName);

                Stopwatch sw = Stopwatch.StartNew();

                // --- Step 1: Copy .nex to code.nex ---
                try { File.Copy(nexFile, codeNexPath, true); }
                catch (Exception ex)
                {
                    sw.Stop();
                    PrintFail("Cannot copy source: " + ex.Message);
                    failures.Add(testName + " : Cannot copy source");
                    failed++;
                    if (stopOnFail) break;
                    continue;
                }

                // Delete any stale app.exe
                SafeDelete(appExePath);

                // --- Step 2: Compile ---
                string compileOut;
                string compileError = RunProcess(compilerExe, "", compilerDir, timeoutMs, null, verbose, out compileOut);
                if (compileError != null)
                {
                    sw.Stop();
                    string reason = "Compiler error: " + compileError;
                    PrintFail(string.Format("{0} ({1:F0} ms)", reason, sw.Elapsed.TotalMilliseconds));
                    failures.Add(testName + " : " + reason);
                    failed++;
                    if (stopOnFail) break;
                    continue;
                }

                if (!File.Exists(appExePath))
                {
                    sw.Stop();
                    PrintFail(string.Format("app.exe not produced ({0:F0} ms)", sw.Elapsed.TotalMilliseconds));
                    failures.Add(testName + " : app.exe not produced");
                    failed++;
                    if (stopOnFail) break;
                    continue;
                }

                // --- Step 3: Run app.exe (optionally multiple times) ---
                string lastStdout = "";
                string runError   = null;

                for (int iter = 0; iter < iterPerTest; iter++)
                {
                    runError = RunProcess(appExePath, "", compilerDir, timeoutMs, stdinBytes, verbose, out lastStdout);
                    if (runError != null) break;
                }

                if (runError != null)
                {
                    sw.Stop();
                    string reason = "Runtime error: " + runError;
                    PrintFail(string.Format("{0} ({1:F0} ms)", reason, sw.Elapsed.TotalMilliseconds));
                    failures.Add(testName + " : " + reason);
                    failed++;
                    if (stopOnFail) break;
                    continue;
                }

                sw.Stop();

                // --- Step 4: Generate or Compare ---
                if (genExpected)
                {
                    File.WriteAllText(expectedFile, lastStdout, Encoding.UTF8);
                    Console.ForegroundColor = ConsoleColor.Cyan;
                    Console.WriteLine("GENERATED ({0:F0} ms)", sw.Elapsed.TotalMilliseconds);
                    Console.ResetColor();
                    generated++;
                    continue;
                }

                if (hasExpected)
                {
                    string expectedContent = File.ReadAllText(expectedFile, Encoding.UTF8);
                    string mismatch = CompareOutput(lastStdout, expectedContent);
                    if (mismatch != null)
                    {
                        PrintFail(string.Format("Output mismatch ({0:F0} ms)", sw.Elapsed.TotalMilliseconds));
                        Console.ForegroundColor = ConsoleColor.DarkRed;
                        Console.WriteLine("      " + mismatch);
                        Console.ResetColor();
                        failures.Add(testName + " : Output mismatch - " + mismatch);
                        failed++;
                        if (stopOnFail) break;
                        continue;
                    }
                }

                // Pass
                PrintPass(string.Format("({0:F0} ms){1}", sw.Elapsed.TotalMilliseconds, hasExpected ? "" : " [no .expected - exit code only]"));
                passed++;
            }

            // --- Summary ---
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");

            if (genExpected)
            {
                Console.WriteLine(" NEXTEST: Generated {0} .expected files.", generated);
            }
            else
            {
                int total = passed + failed;
                double pct = total > 0 ? (double)passed / total * 100.0 : 0;
                Console.WriteLine(" NEXTEST RESULTS: {0}/{1} Passed ({2:F1}%)", passed, total, pct);

                if (failures.Count > 0)
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine(" Failures:");
                    foreach (string f in failures)
                        Console.WriteLine("   - " + f);
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine(" All tests passed!");
                }
            }

            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.ResetColor();

            return failed > 0 ? 1 : 0;
        }

        // Returns null on success, error string on failure.
        // stdout is always captured and returned in stdoutOut.
        static string RunProcess(string exe, string arguments, string workDir, int timeoutMs, byte[] stdinBytes, bool verbose, out string stdoutOut)
        {
            stdoutOut = "";
            ProcessStartInfo psi = new ProcessStartInfo(exe, arguments)
            {
                WorkingDirectory       = workDir,
                UseShellExecute        = false,
                RedirectStandardOutput = true,
                RedirectStandardError  = true,
                RedirectStandardInput  = (stdinBytes != null),
                CreateNoWindow         = true
            };

            StringBuilder sbOut = new StringBuilder();
            StringBuilder sbErr = new StringBuilder();

            try
            {
                using (Process p = Process.Start(psi))
                {
                    if (stdinBytes != null)
                    {
                        p.StandardInput.BaseStream.Write(stdinBytes, 0, stdinBytes.Length);
                        p.StandardInput.Close();
                    }

                    // Read stdout/stderr to avoid deadlock
                    p.OutputDataReceived += (s, e) => { if (e.Data != null) sbOut.AppendLine(e.Data); };
                    p.ErrorDataReceived  += (s, e) => { if (e.Data != null) sbErr.AppendLine(e.Data); };
                    p.BeginOutputReadLine();
                    p.BeginErrorReadLine();

                    bool finished = p.WaitForExit(timeoutMs);
                    if (!finished)
                    {
                        try { p.Kill(); } catch { }
                        return "Timeout (>" + timeoutMs + " ms)";
                    }

                    // Ensure all async reads complete
                    p.WaitForExit();

                    stdoutOut = sbOut.ToString();

                    if (verbose && sbOut.Length > 0)
                    {
                        Console.ForegroundColor = ConsoleColor.DarkGray;
                        Console.Write(sbOut.ToString());
                        Console.ResetColor();
                    }

                    if (p.ExitCode != 0)
                        return string.Format("exit code 0x{0:X8}", p.ExitCode);

                    return null;
                }
            }
            catch (Exception ex)
            {
                return "Exception: " + ex.Message;
            }
        }

        // Returns null if outputs match, or a description of the first mismatch.
        static string CompareOutput(string actual, string expected)
        {
            string[] aLines = NormalizeLines(actual);
            string[] eLines = NormalizeLines(expected);

            int minLen = Math.Min(aLines.Length, eLines.Length);
            for (int i = 0; i < minLen; i++)
            {
                if (!aLines[i].Equals(eLines[i], StringComparison.Ordinal))
                    return string.Format("line {0}: expected '{1}', got '{2}'", i + 1, Truncate(eLines[i], 40), Truncate(aLines[i], 40));
            }

            if (aLines.Length != eLines.Length)
                return string.Format("line count differs: expected {0} lines, got {1}", eLines.Length, aLines.Length);

            return null;
        }

        static string[] NormalizeLines(string s)
        {
            if (s == null) return new string[0];
            string[] lines = s.Replace("\r\n", "\n").Replace("\r", "\n").TrimEnd('\n').Split('\n');
            List<string> result = new List<string>();
            foreach (string l in lines)
                result.Add(l.TrimEnd());
            return result.ToArray();
        }

        static string Truncate(string s, int max)
        {
            if (s == null) return "(null)";
            return s.Length <= max ? s : s.Substring(0, max) + "...";
        }

        static void SafeDelete(string path)
        {
            if (!File.Exists(path)) return;
            for (int r = 0; r < 5; r++)
            {
                try { File.Delete(path); return; }
                catch { System.Threading.Thread.Sleep(50); }
            }
        }

        static void PrintPass(string detail)
        {
            Console.ForegroundColor = ConsoleColor.Green;
            Console.Write("PASS ");
            Console.ResetColor();
            Console.WriteLine(detail);
        }

        static void PrintFail(string detail)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.Write("FAIL ");
            Console.ResetColor();
            Console.WriteLine(detail);
        }

        static void PrintUsage()
        {
            Console.WriteLine("Usage: nextest.exe <compiler.exe> [options]");
            Console.WriteLine("Options:");
            Console.WriteLine("  --tests <dir>            Directory of .nex test files (default: ..\\examples)");
            Console.WriteLine("  --iter N                 Run each app.exe N times to check stability (default: 1)");
            Console.WriteLine("  --timeout N              Per-step timeout in ms (default: 5000)");
            Console.WriteLine("  --stdin <file>           Feed file as stdin to all app.exe runs");
            Console.WriteLine("  --filter <pattern>       Only run tests whose filename contains pattern");
            Console.WriteLine("  --stop-on-fail           Halt after first failure");
            Console.WriteLine("  --verbose                Print stdout of each run");
            Console.WriteLine("  --generate-expected      Write .expected files from current output (baseline mode)");
            Console.WriteLine();
            Console.WriteLine("Test conventions:");
            Console.WriteLine("  <name>.nex               NEXUS source to compile and run");
            Console.WriteLine("  <name>.expected          Expected stdout (if absent, only exit-code is checked)");
            Console.WriteLine("  <name>.stdin             Stdin to pipe to app.exe for this specific test");
        }
    }
}
