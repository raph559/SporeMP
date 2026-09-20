// Apply pinned SDK source labels to the local STATIC analysis database.
// @category SporeMP.Research
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import java.nio.file.Files;
import java.nio.file.Path;

public class SporeSdkLabels extends GhidraScript {
    @Override
    public void run() throws Exception {
        if (!"dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37".equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            throw new IllegalStateException("Unknown executable");
        }
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Supply the exported pinned SDK TSV");
        int count = 0;
        for (String line : Files.readAllLines(Path.of(args[0]))) {
            String[] fields = line.split("\t",3);
            if (fields.length != 3) throw new IllegalArgumentException("Invalid SDK label row");
            Address address = toAddr(Long.parseUnsignedLong(fields[0],16));
            boolean exists = false;
            for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(address)) {
                if (symbol.getName().equals(fields[1])) exists = true;
            }
            Function function = getFunctionAt(address);
            if (function != null && function.getSymbol().getSource() == SourceType.DEFAULT) {
                function.setName(fields[1],SourceType.IMPORTED);
            } else if (!exists) {
                currentProgram.getSymbolTable().createLabel(address,fields[1],SourceType.IMPORTED);
            }
            count++;
        }
        println("Applied " + count + " SDK source labels. Types, aliases and ABIs still require independent audit.");
    }
}
