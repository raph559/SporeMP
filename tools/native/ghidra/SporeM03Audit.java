// Export selected functions from the pinned original game for STATIC analysis.
// Inferred decompiler types and names are not verified callable native bindings.
// @category SporeMP.Research
import ghidra.app.util.headless.HeadlessScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

public class SporeM03Audit extends HeadlessScript {
    private static final String SHA = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
    @Override
    public void run() throws Exception {
        if (!SHA.equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            throw new IllegalStateException("Unknown executable; refusing static binding export");
        }
        String[] args = getScriptArgs();
        if (args.length < 1) throw new IllegalArgumentException("Supply a fresh output directory, then optional hex VAs");
        Path output = Path.of(args[0]);
        Files.createDirectory(output);
        String[] addresses = args.length > 1 ? java.util.Arrays.copyOfRange(args, 1, args.length) : new String[] {
            "C09570", "D672A0", "D67980", "D67CD0", "D67080", "C19800", "C02B40", "C075E0", "D2E8A0", "D39360"
        };
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
        try (PrintWriter index = new PrintWriter(Files.newBufferedWriter(output.resolve("index.txt"), StandardCharsets.UTF_8,
                StandardOpenOption.CREATE_NEW))) {
            index.println("STATIC DECOMPILATION; NOT NATIVE EXECUTION OR ABI ACCEPTANCE");
            index.println("Executable SHA-256: " + SHA);
            index.println("Analysis completeness: see originating run log; reused databases may be partial.");
            for (String hex : addresses) {
                monitor.checkCancelled();
                Address address = toAddr(Long.parseUnsignedLong(hex, 16));
                // Retain nearby existing instructions even when a partial
                // database has no containing function. A window is not a
                // function boundary or a verified ABI.
                try (PrintWriter window = new PrintWriter(Files.newBufferedWriter(output.resolve(hex + ".window.txt"),
                        StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW))) {
                    window.println("STATIC INSTRUCTION WINDOW; NOT A FUNCTION OR ABI CLAIM. SHA-256 " + SHA);
                    var near = currentProgram.getListing().getInstructions(address.subtract(512), true);
                    while (near.hasNext()) {
                        var instruction = near.next();
                        if (instruction.getAddress().compareTo(address.add(512)) > 0) break;
                        StringBuilder bytes = new StringBuilder();
                        for (byte value : instruction.getBytes()) bytes.append(String.format("%02x", value & 255));
                        window.println(instruction.getAddress() + " " + bytes + " " + instruction);
                    }
                }
                Function function = getFunctionAt(address);
                if (function == null) function = getFunctionContaining(address);
                if (function == null) {
                    disassemble(address);
                    function = createFunction(address, null);
                }
                if (function == null) {
                    index.println(hex + " MISSING FUNCTION");
                    continue;
                }
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                // Preserve actual x86 instructions alongside inferred C. ABI checks
                // must use receiver registers, stack cleanup and return instructions.
                try (PrintWriter instructions = new PrintWriter(Files.newBufferedWriter(output.resolve(hex + ".asm.txt"),
                        StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW))) {
                    instructions.println("STATIC X86 LISTING; NOT NATIVE EXECUTION. SHA-256 " + SHA);
                    var iterator = currentProgram.getListing().getInstructions(function.getBody(), true);
                    int count = 0;
                    while (iterator.hasNext()) {
                        var instruction = iterator.next();
                        StringBuilder bytes = new StringBuilder();
                        for (byte value : instruction.getBytes()) bytes.append(String.format("%02x", value & 255));
                        instructions.println(instruction.getAddress() + " " + bytes + " " + instruction);
                        if (++count == 4000) { instructions.println("TRUNCATED AT 4000 INSTRUCTIONS"); break; }
                    }
                }
                index.println(hex + " entry=" + function.getEntryPoint() + " name=" + function.getName()
                    + " completed=" + result.decompileCompleted() + " message=" + result.getErrorMessage());
                if (result.decompileCompleted()) {
                    Files.writeString(output.resolve(hex + ".c.txt"),
                        "// STATIC DECOMPILER OUTPUT. Types and names may be inferred incorrectly.\n"
                        + result.getDecompiledFunction().getC(), StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW);
                }
                for (Function callee : function.getCalledFunctions(monitor)) {
                    index.println("  calls " + callee.getEntryPoint() + " " + callee.getName());
                }
            }
            java.util.Set<String> referenceAddresses = new java.util.LinkedHashSet<>(java.util.Arrays.asList(addresses));
            referenceAddresses.addAll(java.util.Arrays.asList("D672A0", "1583F30", "1583F3C", "169E398", "D2E8A0", "167EAF4"));
            for (String hex : referenceAddresses) {
                Address address = toAddr(Long.parseUnsignedLong(hex,16));
                ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(address);
                index.println("References to " + hex + ":");
                int count = 0;
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Function caller = getFunctionContaining(reference.getFromAddress());
                    index.println("  " + reference.getFromAddress() + " " + reference.getReferenceType()
                        + " function=" + (caller == null ? "none" : caller.getEntryPoint()));
                    if (++count == 2000) throw new IllegalStateException("Reference output exceeded audit bound");
                }
            }
        } finally {
            decompiler.dispose();
        }
        println("SporeMP static function export written to " + output);
    }
}
