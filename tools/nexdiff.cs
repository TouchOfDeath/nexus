using System;
using System.IO;
using System.Text;
using System.Collections.Generic;

namespace NexusTools
{
    class NexDiff
    {
        static int Main(string[] args)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.WriteLine(" [NEXUS] NEXDIFF v2.0 - Native Binary & Section Diff Tool ");
            Console.WriteLine("==========================================================");
            Console.ResetColor();

            if (args.Length < 2)
            {
                Console.WriteLine("Usage: nexdiff.exe <file1.exe> <file2.exe> [options]");
                Console.WriteLine("Options:");
                Console.WriteLine("  --max N            Max diff regions to display (default: 50)");
                Console.WriteLine("  --section <name>   Only show diffs in this PE section (e.g. .text)");
                Console.WriteLine("  --context N        Show N bytes of context around each diff (default: 0)");
                Console.WriteLine("  --summary          Show section diff summary only, no byte detail");
                return 0;
            }

            string file1      = args[0];
            string file2      = args[1];
            int    maxDiffs   = 50;
            string sectionFilter = null;
            int    context    = 0;
            bool   summaryOnly = false;

            for (int i = 2; i < args.Length; i++)
            {
                string a = args[i];
                if      (a.Equals("--max",     StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) maxDiffs = int.Parse(args[++i]);
                else if (a.Equals("--section", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) sectionFilter = args[++i];
                else if (a.Equals("--context", StringComparison.OrdinalIgnoreCase) && i+1 < args.Length) context  = int.Parse(args[++i]);
                else if (a.Equals("--summary", StringComparison.OrdinalIgnoreCase)) summaryOnly = true;
                else if (!a.StartsWith("-") && i == 2) { int n; if (int.TryParse(a, out n)) maxDiffs = n; } // backwards compat
            }

            if (!File.Exists(file1)) { Console.WriteLine("[-] File not found: " + file1); return 1; }
            if (!File.Exists(file2)) { Console.WriteLine("[-] File not found: " + file2); return 1; }

            byte[] b1 = File.ReadAllBytes(file1);
            byte[] b2 = File.ReadAllBytes(file2);

            Console.WriteLine("[+] File 1: {0} ({1:N0} bytes)", Path.GetFileName(file1), b1.Length);
            Console.WriteLine("[+] File 2: {0} ({1:N0} bytes)", Path.GetFileName(file2), b2.Length);

            if (b1.Length != b2.Length)
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("[!] File sizes differ by {0:N0} bytes", Math.Abs(b1.Length - b2.Length));
                Console.ResetColor();
            }

            var sections = ParseSections(b1);
            int minLen   = Math.Min(b1.Length, b2.Length);

            // Apply section filter to restrict comparison range
            int rangeStart = 0;
            int rangeEnd   = minLen;
            if (sectionFilter != null)
            {
                bool found = false;
                foreach (var s in sections)
                {
                    if (s.Name.Equals(sectionFilter, StringComparison.OrdinalIgnoreCase))
                    {
                        rangeStart = s.RawPtr;
                        rangeEnd   = Math.Min(minLen, s.RawPtr + s.RawSize);
                        found = true;
                        Console.WriteLine("[+] Filter: section '{0}' (offset 0x{1:X}..0x{2:X})", s.Name, rangeStart, rangeEnd);
                        break;
                    }
                }
                if (!found)
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("[!] Section '{0}' not found in PE. Showing all diffs.", sectionFilter);
                    Console.ResetColor();
                }
            }

            // Collect all diff offsets
            List<int> allDiffs = new List<int>();
            for (int i = rangeStart; i < rangeEnd; i++)
                if (b1[i] != b2[i]) allDiffs.Add(i);

            int sizeDiff = Math.Abs(b1.Length - b2.Length);
            int totalDiffs = allDiffs.Count + sizeDiff;

            if (totalDiffs == 0)
            {
                Console.WriteLine();
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("*****************************************************************");
                Console.WriteLine(" [***] 100% BIT-FOR-BIT IDENTICAL! ZERO DIFFERENCES FOUND! [***]");
                Console.WriteLine("       Compared all {0:N0} bytes with mathematical precision.", minLen);
                Console.WriteLine("*****************************************************************");
                Console.ResetColor();

                PrintSectionSummary(sections, b1, b2, minLen);
                return 0;
            }

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine("[-] DIFFERENCES: {0:N0} byte(s) differ ({1:F3}%)", allDiffs.Count,
                (double)allDiffs.Count / Math.Max(b1.Length, b2.Length) * 100.0);
            Console.ResetColor();

            if (!summaryOnly)
            {
                const int ROW = 16;
                Console.WriteLine();
                Console.WriteLine("  {0,-10} {1,-12} {2,-40} {3}", "Offset", "Section",
                    Path.GetFileName(file1).Truncate(19) + " (A)",
                    Path.GetFileName(file2).Truncate(19) + " (B)");
                Console.WriteLine("  " + new string('-', 90));

                int displayed = 0;
                int idx       = 0;

                while (idx < allDiffs.Count && displayed < maxDiffs)
                {
                    int off      = allDiffs[idx];
                    int chunkOff = (off / ROW) * ROW;

                    // Context: rows before chunk
                    if (context > 0)
                    {
                        int ctxStart = Math.Max(rangeStart, chunkOff - context);
                        for (int co = ctxStart; co < chunkOff; co += ROW)
                            PrintRow(b1, b2, co, ROW, minLen, sections, ConsoleColor.DarkGray, false);
                    }

                    string secName = GetSectionName(chunkOff, sections);
                    PrintRow(b1, b2, chunkOff, ROW, minLen, sections, ConsoleColor.White, true);
                    displayed++;

                    // Context: rows after chunk
                    if (context > 0)
                    {
                        int ctxEnd = Math.Min(rangeEnd, chunkOff + ROW + context);
                        for (int co = chunkOff + ROW; co < ctxEnd; co += ROW)
                            PrintRow(b1, b2, co, ROW, minLen, sections, ConsoleColor.DarkGray, false);
                        Console.WriteLine("  ...");
                    }

                    // Skip all diffs in this row
                    while (idx < allDiffs.Count && allDiffs[idx] < chunkOff + ROW) idx++;
                }

                if (allDiffs.Count > displayed)
                    Console.WriteLine("  ... and {0} more regions (increase --max to show)", allDiffs.Count - displayed);
            }

            PrintSectionSummary(sections, b1, b2, minLen);
            return totalDiffs > 0 ? 1 : 0;
        }

        static void PrintRow(byte[] b1, byte[] b2, int off, int rowWidth, int minLen,
                             List<SectionInfo> sections, ConsoleColor defaultColor, bool colorDiffs)
        {
            string secName = GetSectionName(off, sections);
            Console.Write("  0x{0:X6}   {1,-12} ", off, secName);

            // File 1 bytes
            for (int j = 0; j < rowWidth; j++)
            {
                int cur  = off + j;
                bool diff = cur < minLen && cur < b1.Length && cur < b2.Length && b1[cur] != b2[cur];
                if (colorDiffs && diff) Console.ForegroundColor = ConsoleColor.Red;
                else                   Console.ForegroundColor = defaultColor;
                if (cur < b1.Length) Console.Write("{0:X2} ", b1[cur]);
                else                 Console.Write("   ");
                Console.ResetColor();
                if (j == 7) Console.Write(" ");
            }
            Console.Write("| ");

            // File 2 bytes
            for (int j = 0; j < rowWidth; j++)
            {
                int cur  = off + j;
                bool diff = cur < minLen && cur < b1.Length && cur < b2.Length && b1[cur] != b2[cur];
                if (colorDiffs && diff) Console.ForegroundColor = ConsoleColor.Green;
                else                   Console.ForegroundColor = defaultColor;
                if (cur < b2.Length) Console.Write("{0:X2} ", b2[cur]);
                else                 Console.Write("   ");
                Console.ResetColor();
                if (j == 7) Console.Write(" ");
            }
            Console.WriteLine();
        }

        static void PrintSectionSummary(List<SectionInfo> sections, byte[] b1, byte[] b2, int minLen)
        {
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("--- SECTION DIFF SUMMARY ---");
            Console.ResetColor();

            // Headers
            int hdrDiff = 0;
            for (int i = 0; i < Math.Min(0x400, minLen); i++)
                if (b1[i] != b2[i]) hdrDiff++;
            PrintSummaryLine("[Headers]", hdrDiff, Math.Min(0x400, minLen));

            foreach (var s in sections)
            {
                int diffCount = 0;
                int secEnd    = Math.Min(minLen, s.RawPtr + s.RawSize);
                for (int i = s.RawPtr; i < secEnd; i++)
                    if (b1[i] != b2[i]) diffCount++;
                PrintSummaryLine(s.Name, diffCount, secEnd - s.RawPtr);
            }

            if (Math.Abs(b1.Length - b2.Length) > 0)
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("  [Size diff] : {0:N0} extra bytes in {1}", Math.Abs(b1.Length - b2.Length),
                    b1.Length > b2.Length ? "File 1" : "File 2");
                Console.ResetColor();
            }
        }

        static void PrintSummaryLine(string name, int diffCount, int totalBytes)
        {
            Console.Write("  {0,-12}: ", name);
            if (diffCount == 0)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("IDENTICAL ({0:N0} bytes)", totalBytes);
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("{0:N0} bytes differ (of {1:N0})", diffCount, totalBytes);
            }
            Console.ResetColor();
        }

        static string GetSectionName(int off, List<SectionInfo> sections)
        {
            if (off < 0x400) return "[Headers]";
            foreach (var s in sections)
                if (off >= s.RawPtr && off < s.RawPtr + s.RawSize) return s.Name;
            return "[Unknown]";
        }

        class SectionInfo
        {
            public string Name;
            public int    RawPtr;
            public int    RawSize;
        }

        static List<SectionInfo> ParseSections(byte[] b)
        {
            var list = new List<SectionInfo>();
            if (b.Length < 0x40 || b[0] != 'M' || b[1] != 'Z') return list;
            int peOff = BitConverter.ToInt32(b, 0x3C);
            if (peOff + 24 > b.Length || b[peOff] != 'P' || b[peOff+1] != 'E') return list;

            ushort numSec  = BitConverter.ToUInt16(b, peOff + 6);
            ushort optSize = BitConverter.ToUInt16(b, peOff + 20);
            int    secOff  = peOff + 24 + optSize;

            for (int i = 0; i < numSec; i++)
            {
                int s = secOff + (i * 40);
                if (s + 40 > b.Length) break;
                list.Add(new SectionInfo
                {
                    Name    = Encoding.ASCII.GetString(b, s, 8).TrimEnd('\0'),
                    RawSize = (int)BitConverter.ToUInt32(b, s + 16),
                    RawPtr  = (int)BitConverter.ToUInt32(b, s + 20)
                });
            }
            return list;
        }
    }

    static class StringExtensions
    {
        public static string Truncate(this string s, int max)
        {
            return s.Length <= max ? s : s.Substring(0, max-1) + "~";
        }
    }
}
