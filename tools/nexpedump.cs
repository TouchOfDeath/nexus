using System;
using System.IO;
using System.Text;
using System.Collections.Generic;

namespace NexusTools
{
    class Program
    {
        static void Main(string[] args)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("==========================================================");
            Console.WriteLine(" [NEXUS] NEXPEDUMP v1.0 - Native PE32+ (x86-64) Inspector ");
            Console.WriteLine("==========================================================");
            Console.ResetColor();

            if (args.Length == 0)
            {
                Console.WriteLine("Usage: nexpedump.exe <path-to-binary.exe>");
                return;
            }

            string filePath = args[0];
            if (!File.Exists(filePath))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] Error: File not found: " + filePath);
                Console.ResetColor();
                return;
            }

            byte[] bytes = File.ReadAllBytes(filePath);
            Console.WriteLine("[+] File: {0} ({1:N0} bytes)", Path.GetFileName(filePath), bytes.Length);

            if (bytes.Length < 64 || bytes[0] != 'M' || bytes[1] != 'Z')
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] Invalid DOS Header (Missing 'MZ' magic).");
                Console.ResetColor();
                return;
            }

            int peOffset = BitConverter.ToInt32(bytes, 0x3C);
            Console.WriteLine("[+] DOS Header e_lfanew: 0x{0:X4}", peOffset);

            if (peOffset + 24 > bytes.Length || bytes[peOffset] != 'P' || bytes[peOffset + 1] != 'E' || bytes[peOffset + 2] != 0 || bytes[peOffset + 3] != 0)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("[-] Invalid PE Signature (Expected 'PE\\0\\0').");
                Console.ResetColor();
                return;
            }

            // COFF Header
            int coffOffset = peOffset + 4;
            ushort machine = BitConverter.ToUInt16(bytes, coffOffset);
            ushort numSections = BitConverter.ToUInt16(bytes, coffOffset + 2);
            uint timeDateStamp = BitConverter.ToUInt32(bytes, coffOffset + 4);
            ushort optHeaderSize = BitConverter.ToUInt16(bytes, coffOffset + 16);
            ushort characteristics = BitConverter.ToUInt16(bytes, coffOffset + 18);

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("--- COFF FILE HEADER ---");
            Console.ResetColor();
            Console.WriteLine("  Machine:             0x{0:X4} ({1})", machine, machine == 0x8664 ? "AMD64 / x86-64" : "Other");
            Console.WriteLine("  Sections:            {0}", numSections);
            Console.WriteLine("  TimeDateStamp:       0x{0:X8}", timeDateStamp);
            Console.WriteLine("  SizeOfOptionalHeader:0x{0:X4} ({1} bytes)", optHeaderSize, optHeaderSize);
            Console.WriteLine("  Characteristics:     0x{0:X4}", characteristics);

            // Optional Header (PE32+ 64-bit)
            int optOffset = coffOffset + 20;
            ushort optMagic = BitConverter.ToUInt16(bytes, optOffset);
            bool isPE64 = (optMagic == 0x20B);

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("--- OPTIONAL HEADER ({0}) ---", isPE64 ? "PE32+ 64-bit" : "PE32 32-bit");
            Console.ResetColor();

            uint entryPointRVA = BitConverter.ToUInt32(bytes, optOffset + 16);
            ulong imageBase = isPE64 ? BitConverter.ToUInt64(bytes, optOffset + 24) : BitConverter.ToUInt32(bytes, optOffset + 28);
            uint sectionAlignment = BitConverter.ToUInt32(bytes, optOffset + (isPE64 ? 32 : 32));
            uint fileAlignment = BitConverter.ToUInt32(bytes, optOffset + (isPE64 ? 36 : 36));
            uint sizeOfImage = BitConverter.ToUInt32(bytes, optOffset + (isPE64 ? 56 : 56));
            uint sizeOfHeaders = BitConverter.ToUInt32(bytes, optOffset + (isPE64 ? 60 : 60));
            ushort subsystem = BitConverter.ToUInt16(bytes, optOffset + (isPE64 ? 68 : 68));

            string subDesc = subsystem == 2 ? "Windows GUI" : (subsystem == 3 ? "Windows Console" : "Unknown (" + subsystem + ")");
            Console.WriteLine("  ImageBase:           0x{0:X16}", imageBase);
            Console.WriteLine("  AddressOfEntryPoint: 0x{0:X8} (VA: 0x{1:X16})", entryPointRVA, imageBase + entryPointRVA);
            Console.WriteLine("  SectionAlignment:    0x{0:X8} ({1:N0} bytes)", sectionAlignment, sectionAlignment);
            Console.WriteLine("  FileAlignment:       0x{0:X8} ({1:N0} bytes)", fileAlignment, fileAlignment);
            Console.WriteLine("  SizeOfImage:         0x{0:X8} ({1:N0} bytes)", sizeOfImage, sizeOfImage);
            Console.WriteLine("  SizeOfHeaders:       0x{0:X8} ({1:N0} bytes)", sizeOfHeaders, sizeOfHeaders);
            Console.WriteLine("  Subsystem:           {0} ({1})", subsystem, subDesc);

            // Data Directories
            int dataDirOffset = optOffset + (isPE64 ? 112 : 96);
            uint importRVA = BitConverter.ToUInt32(bytes, dataDirOffset + 8);
            uint importSize = BitConverter.ToUInt32(bytes, dataDirOffset + 12);
            uint iatRVA = BitConverter.ToUInt32(bytes, dataDirOffset + 96);
            uint iatSize = BitConverter.ToUInt32(bytes, dataDirOffset + 100);

            Console.WriteLine("  Import Directory:    RVA 0x{0:X8} (Size: 0x{1:X4})", importRVA, importSize);
            Console.WriteLine("  IAT Directory:       RVA 0x{0:X8} (Size: 0x{1:X4})", iatRVA, iatSize);

            // Section Table
            int sectionTableOffset = optOffset + optHeaderSize;
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("--- SECTION TABLE ({0} Sections) ---", numSections);
            Console.ResetColor();
            Console.WriteLine("  {0,-10} {1,-12} {2,-12} {3,-12} {4,-12} {5}", "Name", "VirtAddress", "VirtSize", "RawDataPtr", "RawDataSize", "Characteristics");
            Console.WriteLine("  ----------------------------------------------------------------------------------");

            List<Section> sections = new List<Section>();
            for (int i = 0; i < numSections; i++)
            {
                int secOff = sectionTableOffset + (i * 40);
                string name = Encoding.ASCII.GetString(bytes, secOff, 8).TrimEnd('\0');
                uint vSize = BitConverter.ToUInt32(bytes, secOff + 8);
                uint vAddr = BitConverter.ToUInt32(bytes, secOff + 12);
                uint rawSize = BitConverter.ToUInt32(bytes, secOff + 16);
                uint rawPtr = BitConverter.ToUInt32(bytes, secOff + 20);
                uint charact = BitConverter.ToUInt32(bytes, secOff + 36);

                sections.Add(new Section { Name = name, VirtAddr = vAddr, VirtSize = vSize, RawPtr = rawPtr, RawSize = rawSize, Characteristics = charact });
                Console.WriteLine("  {0,-10} 0x{1:X8}   0x{2:X8}   0x{3:X8}   0x{4:X8}   0x{5:X8}", name, vAddr, vSize, rawPtr, rawSize, charact);
            }

            // Dump Imports if present
            if (importRVA != 0)
            {
                Console.WriteLine();
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("--- IMPORT DIRECTORY & FUNCTIONS ---");
                Console.ResetColor();

                int importFileOff = RvaToOffset(importRVA, sections);
                if (importFileOff < 0)
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("[-] Import RVA 0x{0:X8} could not be mapped to file offset.", importRVA);
                    Console.ResetColor();
                }
                else
                {
                    int descOff = importFileOff;
                    while (descOff + 20 <= bytes.Length)
                    {
                        uint origFirstThunk = BitConverter.ToUInt32(bytes, descOff);
                        uint timeStamp = BitConverter.ToUInt32(bytes, descOff + 4);
                        uint forwarder = BitConverter.ToUInt32(bytes, descOff + 8);
                        uint nameRVA = BitConverter.ToUInt32(bytes, descOff + 12);
                        uint firstThunk = BitConverter.ToUInt32(bytes, descOff + 16);

                        if (origFirstThunk == 0 && firstThunk == 0 && nameRVA == 0)
                            break; // Null terminator

                        string dllName = ReadAsciiZ(bytes, RvaToOffset(nameRVA, sections));
                        Console.WriteLine("  [DLL] {0} (FirstThunk IAT RVA: 0x{1:X8}, ILT RVA: 0x{2:X8})", dllName, firstThunk, origFirstThunk);

                        uint thunkRVA = origFirstThunk != 0 ? origFirstThunk : firstThunk;
                        int thunkFileOff = RvaToOffset(thunkRVA, sections);

                        if (thunkFileOff >= 0)
                        {
                            int thunkIdx = 0;
                            while (thunkFileOff + 8 <= bytes.Length)
                            {
                                ulong thunkVal = BitConverter.ToUInt64(bytes, thunkFileOff);
                                if (thunkVal == 0) break;

                                uint iatEntryRVA = firstThunk + (uint)(thunkIdx * 8);
                                if ((thunkVal & 0x8000000000000000UL) != 0)
                                {
                                    Console.WriteLine("    IAT [0x{0:X8}] Ordinal: {1}", iatEntryRVA, thunkVal & 0xFFFF);
                                }
                                else
                                {
                                    int hintNameOff = RvaToOffset((uint)thunkVal, sections);
                                    if (hintNameOff >= 0 && hintNameOff + 2 < bytes.Length)
                                    {
                                        ushort hint = BitConverter.ToUInt16(bytes, hintNameOff);
                                        string funcName = ReadAsciiZ(bytes, hintNameOff + 2);
                                        Console.WriteLine("    IAT [0x{0:X8}] Hint: {1,-3} API: {2}", iatEntryRVA, hint, funcName);
                                    }
                                }
                                thunkFileOff += 8;
                                thunkIdx++;
                            }
                        }

                        descOff += 20;
                    }
                }
            }

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("[+] PE Analysis Completed Successfully!");
            Console.ResetColor();
        }

        static int RvaToOffset(uint rva, List<Section> sections)
        {
            foreach (var sec in sections)
            {
                if (rva >= sec.VirtAddr && rva < sec.VirtAddr + Math.Max(sec.VirtSize, sec.RawSize))
                {
                    return (int)(sec.RawPtr + (rva - sec.VirtAddr));
                }
            }
            return -1;
        }

        static string ReadAsciiZ(byte[] bytes, int offset)
        {
            if (offset < 0 || offset >= bytes.Length) return "<invalid>";
            int end = offset;
            while (end < bytes.Length && bytes[end] != 0) end++;
            return Encoding.ASCII.GetString(bytes, offset, end - offset);
        }

        class Section
        {
            public string Name;
            public uint VirtAddr;
            public uint VirtSize;
            public uint RawPtr;
            public uint RawSize;
            public uint Characteristics;
        }
    }
}
