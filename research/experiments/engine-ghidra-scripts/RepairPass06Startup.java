// Restore true boundary after rejecting a seed inside an instruction; image unchanged.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
public class RepairPass06Startup extends GhidraScript {
 public void run() throws Exception {
  Function f=getFunctionAt(toAddr(0x43a400)); if(f!=null)removeFunction(f);
  clearListing(toAddr(0x43a3a0),toAddr(0x43a48d));
  disassemble(toAddr(0x43a3a0));
  if(getFunctionAt(toAddr(0x43a3a0))==null)createFunction(toAddr(0x43a3a0),null);
  println("Rejected 0043a400; restored 0043a3a0 using original LLVM instruction witness");
 }
}
