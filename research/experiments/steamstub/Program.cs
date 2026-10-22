// Local research host for atom0s/Steamless's unmodified variant 3.1 x86 plugin.
// Upstream source and license remain in research/vendor/Steamless.
// This reads PE bytes; it never loads or executes code from the game.
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using Steamless.API.Model;
using Steamless.API.Services;
using Steamless.Unpacker.Variant31.x86.Classes;

var root = Path.GetFullPath(AppContext.BaseDirectory + "../../../../../");
if (Path.GetFileName(root.TrimEnd(Path.DirectorySeparatorChar)) != "research")
    throw new InvalidOperationException("Expected build output under research/experiments/steamstub/bin/Debug/net10.0");
var work = Path.Combine(root, "experiments", "steamstub");
var source = "/Users/andyhunter/Games/BulletWitch-Steam/witch.exe";
var input = Path.Combine(work, "witch.original.exe");
var output = input + ".unpacked.exe";
const string expected = "f39bf4affcc534bf973c20752f9e6424684096acfa1d4607b67d5241a4c7f6cc";
string Hash(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
if (Hash(source) != expected) throw new InvalidOperationException("Unexpected source build");
if (File.Exists(input) || File.Exists(output)) throw new InvalidOperationException("Research copy/output already exists; preserve previous experiment");
File.Copy(source, input, false);
var logger = new LoggingService();
var lines = new List<string>();
logger.AddLogMessage += (_, e) => { var line = $"{e.MessageType}: {e.Message}"; lines.Add(line); Console.WriteLine(line); };
var unpacker = new Steamless.Unpacker.Variant31.x86.Main();
unpacker.Initialize(logger);
var detected = unpacker.CanProcessFile(input);
Console.WriteLine($"Upstream CanProcessFile: {detected}");
var options = new SteamlessOptions {
    KeepBindSection = false, DontRealignSections = true, ZeroDosStubData = false,
    DumpPayloadToDisk = false, DumpSteamDrmpToDisk = false,
    UseExperimentalFeatures = false, RecalculateFileChecksum = false
};
var success = detected && unpacker.ProcessFile(input, options);
var header = (SteamStub32Var31Header)typeof(Steamless.Unpacker.Variant31.x86.Main)
    .GetProperty("StubHeader", BindingFlags.NonPublic | BindingFlags.Instance).GetValue(unpacker);
var result = new {
    upstream_commit = "cd770bf9749d3e4f438d23ac643917ad1a804257",
    host = System.Runtime.InteropServices.RuntimeInformation.RuntimeIdentifier,
    detected, success, source_sha256_after = Hash(source), input_sha256_after = Hash(input),
    output_sha256 = File.Exists(output) ? Hash(output) : null,
    signature = $"0x{header.Signature:x}", app_id = header.SteamAppId,
    original_entry_rva = $"0x{header.OriginalEntryPoint:x}",
    code_section_address = $"0x{header.CodeSectionVirtualAddress:x}",
    code_section_raw_size = header.CodeSectionRawSize,
    flags = $"0x{header.Flags:x}", options, log = lines
};
File.WriteAllText(Path.Combine(work, "unpack-result.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true })+"\n");
if (Hash(source) != expected || Hash(input) != expected) throw new InvalidOperationException("Input changed");
return success ? 0 : 1;
