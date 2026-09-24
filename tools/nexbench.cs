using System;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Collections.Generic;

namespace NexusTools
{
    class NexBench
    {
        static int Main(string[] args)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.WriteLine(" [NEXUS] NEXBENCH v2.0 - High-Precision Performance Bench ");
            Console.WriteLine("==========================================================");
            Console.ResetColor();

            if (args.Length == 0)
            {
                PrintUsage();
                return 0;
            }

            // --- Argument parsing ---
            string targetExe     = args[0];
            int    iterations    = 10;
            string stdinFile     = null;
            string extraArgs     = "";
            bool   quiet         = false;
            bool   verbose       = false;
            int    timeoutMs     = 10000;
            string compareFile   = null;

            for (int i = 1; i < args.Length; i++)
            {
                string a = args[i];
                if (a.Equals("--iter",    StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) iterations  = int.Parse(args[++i]);
                else if (a.Equals("--stdin",   StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) stdinFile   = args[++i];
                else if (a.Equals("--args",    StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) extraArgs   = args[++i];
                else if (a.Equals("--timeout", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) timeoutMs   = int.Parse(args[++i]);
                else if (a.Equals("--compare", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) compareFile = args[++i];
                else if (a.Equals("--quiet",   StringComparison.OrdinalIgnoreCase)) quiet   = true;
                else if (a.Equals("--verbose", StringComparison.OrdinalIgnoreCase)) verbose = true;
                else if (i == 1 && !a.StartsWith("-"))
                {
                    // Backwards-compatible positional arg: nexbench <exe> <N>
                    int n;
                    if (int.TryParse(a, out n)) iterations = n;
                }
            }

            if (!File.Exists(targetExe))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] Target not found: " + targetExe);
                Console.ResetColor();
                return 1;
            }

            // Load stdin bytes
            byte[] stdinBytes = null;
            if (stdinFile != null)
            {
                if (!File.Exists(stdinFile))
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("[-] Stdin file not found: " + stdinFile);
                    Console.ResetColor();
                    return 1;
                }
                stdinBytes = File.ReadAllBytes(stdinFile);
            }

            string targetName = Path.GetFileName(targetExe);
            string workDir    = Path.GetDirectoryName(Path.GetFullPath(targetExe));

            Console.WriteLine("[+] Target:      {0}", targetName);
            Console.WriteLine("[+] Iterations:  {0}", iterations);
            Console.WriteLine("[+] Timeout:     {0} ms / run", timeoutMs);
            if (stdinFile != null) Console.WriteLine("[+] Stdin:       {0} ({1} bytes)", stdinFile, stdinBytes.Length);
            if (compareFile != null) Console.WriteLine("[+] Compare:     {0}", compareFile);
            Console.WriteLine();

            // Warmup
            Console.Write("[+] Warmup run... ");
            string warmoutStdout;
            double warmMs; long warmMem;
            RunOnce(targetExe, extraArgs, workDir, stdinBytes, timeoutMs, out warmMs, out warmMem, out warmoutStdout);
            Console.WriteLine("Done.");
            Console.WriteLine();

            if (!quiet)
                Console.WriteLine("  {0,-10} {1,-18} {2,-18} {3}", "Run #", "Wall-Clock", "Peak Memory", "Status");
            if (!quiet)
                Console.WriteLine("  --------------------------------------------------------------------------");

            List<double> timesMs         = new List<double>();
            long         peakMemMax      = 0;
            bool         allOk           = true;
            string       lastStdout      = "";

            for (int i = 1; i <= iterations; i++)
            {
                double ms;
                long   peakMem;
                bool   ok = RunOnce(targetExe, extraArgs, workDir, stdinBytes, timeoutMs,
                                    out ms, out peakMem, out lastStdout);

                timesMs.Add(ms);
                if (peakMem > peakMemMax) peakMemMax = peakMem;
                if (!ok) allOk = false;

                if (!quiet)
                {
                    ConsoleColor col = ok ? ConsoleColor.Green : ConsoleColor.Red;
                    Console.Write("  Run #{0,-6} {1,10:F2} ms    {2,10:N0} KB   ", i, ms, peakMem/1024);
                    Console.ForegroundColor = col;
                    Console.WriteLine(ok ? "OK" : "FAIL");
                    Console.ResetColor();
                }

                if (verbose && !string.IsNullOrEmpty(lastStdout))
                {
                    Console.ForegroundColor = ConsoleColor.DarkGray;
                    Console.Write(lastStdout);
                    Console.ResetColor();
                }

                System.Threading.Thread.Sleep(20);
            }

            // Stats
            timesMs.Sort();
            double min    = timesMs[0];
            double max    = timesMs[timesMs.Count - 1];
            double median = timesMs[timesMs.Count / 2];
            double sum    = 0; foreach (double t in timesMs) sum += t;
            double mean   = sum / timesMs.Count;
            double sumSq  = 0; foreach (double t in timesMs) sumSq += (t-mean)*(t-mean);
            double stdDev = Math.Sqrt(sumSq / timesMs.Count);

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("--- BENCHMARK PERFORMANCE SUMMARY ---");
            Console.ResetColor();
            Console.WriteLine("  Fastest Run (Min):       {0:F2} ms", min);
            Console.WriteLine("  Slowest Run (Max):       {0:F2} ms", max);
            Console.WriteLine("  Median Execution Time:   {0:F2} ms", median);
            Console.WriteLine("  Mean (Average) Time:     {0:F2} ms (+/- {1:F2} ms std dev)", mean, stdDev);
            Console.WriteLine("  Peak Working Set (RAM):  {0:F2} MB ({1:N0} bytes)", (double)peakMemMax/(1024*1024), peakMemMax);
            Console.WriteLine("  Throughput:              {0:F1} runs / second", 1000.0 / mean);
            Console.WriteLine("  All runs clean exit:     {0}", allOk ? "YES" : "NO (some failed)");

            // Output comparison
            if (compareFile != null)
            {
                Console.WriteLine();
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("--- OUTPUT COMPARISON ---");
                Console.ResetColor();

                if (!File.Exists(compareFile))
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("  [-] Compare file not found: " + compareFile);
                    Console.ResetColor();
                }
                else
                {
                    string expected   = File.ReadAllText(compareFile, Encoding.UTF8);
                    string mismatch   = CompareOutput(lastStdout, expected);
                    if (mismatch == null)
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("  [+] PASS: Output matches expected.");
                        Console.ResetColor();
                    }
                    else
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("  [-] FAIL: " + mismatch);
                        Console.ResetColor();
                    }
                }
            }

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("[+] Benchmark completed.");
            Console.ResetColor();
            return allOk ? 0 : 1;
        }

        static bool RunOnce(string exe, string extraArgs, string dir, byte[] stdinBytes, int timeoutMs,
                            out double ms, out long peakMem, out string stdoutCapture)
        {
            ms            = 0;
            peakMem       = 0;
            stdoutCapture = "";

            ProcessStartInfo psi = new ProcessStartInfo(exe, extraArgs)
            {
                WorkingDirectory       = dir,
                UseShellExecute        = false,
                RedirectStandardOutput = true,
                RedirectStandardError  = true,
                RedirectStandardInput  = (stdinBytes != null),
                CreateNoWindow         = true
            };

            StringBuilder sbOut = new StringBuilder();
            Stopwatch sw = Stopwatch.StartNew();
            try
            {
                using (Process p = Process.Start(psi))
                {
                    if (stdinBytes != null)
                    {
                        p.StandardInput.BaseStream.Write(stdinBytes, 0, stdinBytes.Length);
                        p.StandardInput.Close();
                    }

                    p.OutputDataReceived += (s, e) => { if (e.Data != null) sbOut.AppendLine(e.Data); };
                    p.BeginOutputReadLine();
                    p.ErrorDataReceived += (s, e) => { };
                    p.BeginErrorReadLine();

                    bool done = p.WaitForExit(timeoutMs);
                    if (!done) { try { p.Kill(); } catch { } sw.Stop(); ms = sw.Elapsed.TotalMilliseconds; return false; }
                    p.WaitForExit();
                    sw.Stop();
                    ms = sw.Elapsed.TotalMilliseconds;

                    try { peakMem = p.PeakWorkingSet64; } catch { }
                    stdoutCapture = sbOut.ToString();
                    return p.ExitCode == 0;
                }
            }
            catch
            {
                sw.Stop(); ms = sw.Elapsed.TotalMilliseconds;
                return false;
            }
        }

        static string CompareOutput(string actual, string expected)
        {
            string[] aLines = Normalize(actual);
            string[] eLines = Normalize(expected);
            int minLen = Math.Min(aLines.Length, eLines.Length);
            for (int i = 0; i < minLen; i++)
                if (!aLines[i].Equals(eLines[i]))
                    return string.Format("line {0}: expected '{1}', got '{2}'",
                        i+1, Trunc(eLines[i], 50), Trunc(aLines[i], 50));
            if (aLines.Length != eLines.Length)
                return string.Format("line count differs: expected {0}, got {1}", eLines.Length, aLines.Length);
            return null;
        }

        static string[] Normalize(string s)
        {
            if (s == null) return new string[0];
            return s.Replace("\r\n","\n").Replace("\r","\n").TrimEnd('\n').Split('\n');
        }

        static string Trunc(string s, int max)
        {
            return s.Length <= max ? s : s.Substring(0, max) + "...";
        }

        static void PrintUsage()
        {
            Console.WriteLine("Usage: nexbench.exe <binary.exe> [options]");
            Console.WriteLine("Options:");
            Console.WriteLine("  --iter N          Number of iterations (default: 10)");
            Console.WriteLine("  --stdin <file>    Feed file as stdin to each run");
            Console.WriteLine("  --args \"...\"      Extra arguments for target binary");
            Console.WriteLine("  --timeout N       Per-run timeout in ms (default: 10000)");
            Console.WriteLine("  --compare <file>  Compare last run's stdout to this file");
            Console.WriteLine("  --quiet           Print summary only, not per-run results");
            Console.WriteLine("  --verbose         Print stdout of each run");
        }
    }
}
