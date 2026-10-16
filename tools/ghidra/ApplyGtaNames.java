// Applies gta_sa.exe 1.0 US names from gta-reversed and plugin-sdk to the current program.
// The names come from the TSV that tools/ghidra/gta_names.py writes; this script is safe to re-run.
//
// Functions: the entry gets Ns::Name (SourceType IMPORTED). A missing function is created when the
// address is code that no other function contains. Names a person (or ReVa) set are kept and counted
// as conflicts. vtable and data rows become labels; an existing user label stays primary. Code the
// Hoodlum exe moved out of a named function gets Ns::Name_hoodlum (relocated copy) or Ns::Name_body
// (rest of a function behind a detour stub); see nameMovedCode.
//
// tools/ghidra/update-project.ps1 runs it. By hand (the project must not be open elsewhere, e.g. in a
// running ReVa session; a project folder under .tools must be given by its 8.3 name):
//   analyzeHeadless <project dir> gta_sa -process gta_sa.exe -noanalysis
//       -scriptPath <repo>\tools\ghidra -postScript ApplyGtaNames.java <names.tsv>
// A per-entry report of everything not applied goes to <names.tsv>.report.txt.
//@category GTA SA

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.SymbolUtilities;

public class ApplyGtaNames extends GhidraScript {

	private final Map<String, Integer> counts = new TreeMap<>();
	private final List<String> report = new ArrayList<>();
	private SymbolTable symbols;
	private String detail;

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File tsv = args.length > 0 ? new File(args[0]) : askFile("Names TSV from gta_names.py", "Apply");
		symbols = currentProgram.getSymbolTable();
		FunctionManager functions = currentProgram.getFunctionManager();
		int functionsBefore = functions.getFunctionCount();

		for (String line : Files.readAllLines(tsv.toPath(), StandardCharsets.UTF_8)) {
			if (line.isBlank() || line.startsWith("#")) {
				continue;
			}
			monitor.checkCancelled();
			String[] col = line.split("\t", -1);
			Address addr = toAddr(Long.decode(col[0]));
			String kind = col[1];
			String name = col[2];
			String origin = col[3] + " " + col[4];
			try {
				detail = "";
				String outcome = kind.equals("func") ? applyFunction(addr, name, col[5])
						: applyLabel(addr, name);
				count(kind + " " + outcome);
				if (!outcome.equals("named") && !outcome.equals("already") &&
					!outcome.equals("created") && !outcome.equals("updated")) {
					report.add(String.format("%s\t%s\t%s\t%s\t%s\t%s", outcome, addr, kind, name, origin,
						detail));
				}
			}
			catch (Exception e) {
				count(kind + " error");
				report.add(String.format("error\t%s\t%s\t%s\t%s\t%s", addr, kind, name, origin, e));
			}
		}
		nameMovedCode(functions);

		int named = 0;
		for (Function f : functions.getFunctions(true)) {
			if (f.getSymbol().getSource() != SourceType.DEFAULT) {
				named++;
			}
		}
		File reportFile = new File(tsv.getPath() + ".report.txt");
		try (PrintWriter w = new PrintWriter(reportFile, StandardCharsets.UTF_8)) {
			w.println("# outcome\taddress\tkind\tname\tsource origin\tdetail");
			report.forEach(w::println);
		}
		println("ApplyGtaNames: " + tsv);
		counts.forEach((k, v) -> println(String.format("  %-28s %6d", k, v)));
		println(String.format("  functions before %d, after %d, with a non-default name %d",
			functionsBefore, functions.getFunctionCount(), named));
		println("  report: " + reportFile);
	}

	private void count(String key) {
		counts.merge(key, 1, Integer::sum);
	}

	/**
	 * The Hoodlum 1.0 US exe moved code out of many functions, so Ghidra shows the real code as FUN_:
	 * - relocated: the function at the original address is only "jmp" (after NOPs) into another
	 *   section, e.g. CRunningScript::UpdatePC 0x464DA0: jmp 0x15620B0 (.HOODLUM). The jump target
	 *   gets Ns::Name_hoodlum.
	 * - detoured: the function ends in a jmp to an obfuscated stub that comes back right after that
	 *   jmp, e.g. CTheScripts::Init 0x468D50: ...; jmp 0x401CF8 -> mov ecx,0x106B2; jmp 0x468D5D.
	 *   The stub is emulated (at most 64 instructions) and only a proven return names the code after
	 *   the jmp Ns::Name_body.
	 * Only functions named from the TSV are followed; names others set are not touched.
	 */
	private void nameMovedCode(FunctionManager functions) throws Exception {
		List<Function> ours = new ArrayList<>();
		for (Function f : functions.getFunctions(true)) {
			String n = f.getName();
			if (f.getSymbol().getSource() == SourceType.IMPORTED && !n.endsWith("_body") &&
				!n.endsWith("_hoodlum")) {
				ours.add(f);
			}
		}
		for (Function f : ours) {
			monitor.checkCancelled();
			Instruction first = getInstructionAt(f.getEntryPoint());
			while (first != null && first.getMnemonicString().equals("NOP")) {
				first = first.getNext();
			}
			Address target = unconditionalTarget(first);
			MemoryBlock home = getMemoryBlock(f.getEntryPoint());
			MemoryBlock there = target == null ? null : getMemoryBlock(target);
			if (there != null && there.isExecute() && !there.getStart().equals(home.getStart())) {
				nameMoved(target, f, "_hoodlum");
				continue;
			}
			Instruction last = getInstructionContaining(f.getBody().getMaxAddress());
			target = unconditionalTarget(last);
			if (target == null || f.getBody().contains(target)) {
				continue;
			}
			Address next = last.getMaxAddress().next();
			if (next != null && (next.getOffset() & 0xF) != 0 && stubReturnsTo(target, next)) {
				nameMoved(next, f, "_body");
			}
		}
	}

	/** Target of a direct JMP. Ghidra types a JMP to a function start as CALL_TERMINATOR, so the
	 *  mnemonic decides, not FlowType.isJump(). */
	private static Address unconditionalTarget(Instruction insn) {
		if (insn == null || !insn.getMnemonicString().equals("JMP") ||
			!insn.getFlowType().isUnConditional() || insn.getFlowType().isComputed()) {
			return null;
		}
		Address[] flows = insn.getFlows();
		return flows.length == 1 ? flows[0] : null;
	}

	@SuppressWarnings("removal") // EmulatorHelper is deprecated since Ghidra 12.1 but still shipped
	private boolean stubReturnsTo(Address stub, Address back) throws Exception {
		ghidra.app.emulator.EmulatorHelper emu = new ghidra.app.emulator.EmulatorHelper(currentProgram);
		boolean[] unsupported = { false };
		try {
			// LOCK/UNLOCK prefixes change nothing for one thread; any other user op (FPU and the
			// like) ends the trial quietly instead of logging an emulation failure.
			emu.registerDefaultCallOtherCallback(new ghidra.pcode.emulate.BreakCallBack() {
				@Override
				public boolean pcodeCallback(ghidra.pcode.pcoderaw.PcodeOpRaw op) {
					String name = currentProgram.getLanguage().getUserDefinedOpName(
						(int) op.getInput(0).getOffset());
					if (!"LOCK".equals(name) && !"UNLOCK".equals(name)) {
						unsupported[0] = true;
					}
					return true;
				}
			});
			// Reads of memory/registers the stub never set (bss, FS, x87) read as zero, without
			// Ghidra's per-read warnings.
			emu.setMemoryFaultHandler(new ghidra.pcode.memstate.MemoryFaultHandler() {
				@Override
				public boolean uninitializedRead(Address address, int size, byte[] buf, int off) {
					return emu.getEmulateExecutionState() !=
						ghidra.pcode.emulate.EmulateExecutionState.INSTRUCTION_DECODE;
				}

				@Override
				public boolean unknownAddress(Address address, boolean write) {
					return false;
				}
			});
			emu.writeRegister(emu.getStackPointerRegister(), 0x7ff00000L);
			emu.writeRegister(emu.getPCRegister(), stub.getOffset());
			for (int i = 0; i < 64; i++) {
				if (!emu.step(monitor) || unsupported[0]) {
					return false;
				}
				Address pc = emu.getExecutionAddress();
				if (pc.equals(back)) {
					return true;
				}
				MemoryBlock b = getMemoryBlock(pc);
				if (b == null || !b.isExecute()) {
					return false;
				}
			}
			return false;
		}
		catch (Exception e) {
			return false;
		}
		finally {
			emu.dispose();
		}
	}

	private void nameMoved(Address addr, Function owner, String suffix) throws Exception {
		String key = suffix.equals("_body") ? "func detoured-body " : "func relocated-copy ";
		Function moved = getFunctionAt(addr);
		if (moved == null) {
			if (getInstructionAt(addr) == null || getFunctionContaining(addr) != null) {
				count(key + "skip-no-function");
				report.add(String.format("skip-no-function\t%s\tfunc\t%s%s\t(moved code)\t", addr,
					owner.getName(true), suffix));
				return;
			}
			moved = createFunction(addr, null);
			if (moved == null) {
				count(key + "skip-create-failed");
				return;
			}
		}
		String name = owner.getName() + suffix;
		Symbol sym = moved.getSymbol();
		if (sym.getName().equals(name) && sym.getParentNamespace().equals(owner.getParentNamespace())) {
			count(key + "already");
		}
		else if (sym.getSource() == SourceType.DEFAULT) {
			sym.setNameAndNamespace(name, owner.getParentNamespace(), SourceType.IMPORTED);
			count(key + "named");
		}
		else {
			count(key + "skip-named");
			report.add(String.format("skip-named\t%s\tfunc\t%s%s\t(moved code)\talready %s", addr,
				owner.getName(true), suffix, sym.getName(true)));
		}
	}

	private String applyFunction(Address addr, String fullName, String flags) throws Exception {
		MemoryBlock block = currentProgram.getMemory().getBlock(addr);
		if (block == null || !block.isExecute()) {
			return "skip-not-code";
		}
		Function f = getFunctionAt(addr);
		boolean created = false;
		if (f == null) {
			Function container = getFunctionContaining(addr);
			if (container != null) {
				detail = "inside " + container.getName(true) + " @ " + container.getEntryPoint();
				return "skip-inside-function";
			}
			if (flags.contains("commented")) {
				return "skip-commented-no-function";
			}
			if (getInstructionAt(addr) == null) {
				if (getDataAt(addr) != null || !disassemble(addr)) {
					return "skip-not-instruction";
				}
			}
			Instruction insn = getInstructionAt(addr);
			if (insn == null) {
				return "skip-not-instruction";
			}
			f = createFunction(addr, null);
			if (f == null) {
				return "skip-create-failed";
			}
			created = true;
		}
		Symbol sym = f.getSymbol();
		String[] parts = splitName(fullName);
		Namespace ns = namespaceFor(parts);
		String simple = parts[parts.length - 1];
		if (sym.getName().equals(simple) && sym.getParentNamespace().equals(ns)) {
			return created ? "created" : "already";
		}
		SourceType src = sym.getSource();
		if (src == SourceType.USER_DEFINED) {
			detail = "user name " + sym.getName(true);
			return "skip-user-named";
		}
		boolean wasImported = src == SourceType.IMPORTED;
		sym.setNameAndNamespace(simple, ns, SourceType.IMPORTED);
		return created ? "created" : wasImported ? "updated" : "named";
	}

	private String applyLabel(Address addr, String fullName) throws Exception {
		if (currentProgram.getMemory().getBlock(addr) == null) {
			return "skip-unmapped";
		}
		String[] parts = splitName(fullName);
		Namespace ns = namespaceFor(parts);
		String simple = parts[parts.length - 1];
		for (Symbol s : symbols.getSymbols(addr)) {
			if (s.getName().equals(simple) && s.getParentNamespace().equals(ns)) {
				return "already";
			}
		}
		Symbol primary = symbols.getPrimarySymbol(addr);
		Symbol label = symbols.createLabel(addr, simple, ns, SourceType.IMPORTED);
		if (primary == null || primary.getSource() != SourceType.USER_DEFINED) {
			label.setPrimary();
			return "named";
		}
		return "added-secondary";
	}

	/** Split Ns::Name on "::" outside template brackets. */
	private static String[] splitName(String name) {
		List<String> parts = new ArrayList<>();
		int depth = 0, start = 0;
		for (int i = 0; i < name.length(); i++) {
			char c = name.charAt(i);
			if (c == '<') {
				depth++;
			}
			else if (c == '>') {
				depth--;
			}
			else if (depth == 0 && c == ':' && i + 1 < name.length() && name.charAt(i + 1) == ':') {
				parts.add(name.substring(start, i));
				start = i + 2;
				i++;
			}
		}
		parts.add(name.substring(start));
		for (int i = 0; i < parts.size(); i++) {
			parts.set(i, SymbolUtilities.replaceInvalidChars(parts.get(i), true));
		}
		return parts.toArray(new String[0]);
	}

	private Namespace namespaceFor(String[] parts) throws Exception {
		Namespace ns = currentProgram.getGlobalNamespace();
		for (int i = 0; i < parts.length - 1; i++) {
			ns = symbols.getOrCreateNameSpace(ns, parts[i], SourceType.IMPORTED);
		}
		return ns;
	}
}
