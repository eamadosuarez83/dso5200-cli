// Etiqueta registros del FX2, crea funciones en los manejadores vendor y decompila todo.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class DecompFx2 extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String regs = args[0], out = args[1];
        AddressSpace ext = currentProgram.getAddressFactory().getAddressSpace("EXTMEM");
        AddressSpace code = currentProgram.getAddressFactory().getAddressSpace("CODE");
        for (String line : Files.readAllLines(Paths.get(regs))) {
            String[] p = line.trim().split("\\s+");
            if (p.length < 2) continue;
            Address a = ext.getAddress(Long.parseLong(p[0], 16));
            try { currentProgram.getSymbolTable().createLabel(a, p[1], SourceType.USER_DEFINED); } catch (Exception e) {}
        }
        String[][] vend = {{"016A","vend_B2"},{"0186","vend_B3"},{"0203","vend_B4"},{"025D","vend_B5"},
            {"035D","vend_B6"},{"03A0","vend_B7"},{"03E1","vend_B8"},{"0422","vend_B9"},{"0463","vend_BA"},
            {"062C","vend_BB"},{"01C4","vend_BC"},{"04A4","vend_A2_A3"}};
        for (String[] v : vend) {
            Address a = code.getAddress(Long.parseLong(v[0], 16));
            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            Function f = getFunctionAt(a);
            if (f == null) { Function c = getFunctionContaining(a); if (c != null && !c.getEntryPoint().equals(a)) {} f = createFunction(a, v[1]); }
            if (f != null) f.setName(v[1], SourceType.USER_DEFINED);
        }
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                DecompileResults r = d.decompileFunction(f, 60, monitor);
                w.println("// ===== " + f.getName() + " @ " + f.getEntryPoint() + " =====");
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// fallo: " + r.getErrorMessage());
            }
        }
    }
}
