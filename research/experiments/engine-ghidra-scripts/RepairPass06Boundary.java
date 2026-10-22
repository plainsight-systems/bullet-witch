// Remove a seed at an interior call instruction and restore its true wrapper.
// @category BulletWitch
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
public class RepairPass06Boundary extends GhidraScript {
 public void run() throws Exception {
  Function f=getFunctionAt(toAddr(0x461785));
  if(f!=null && f.getName().equals("FUN_00461785"))removeFunction(f);
  disassemble(toAddr(0x461770));
  if(getFunctionAt(toAddr(0x461770))==null)createFunction(toAddr(0x461770),null);
  println("Corrected 00461785 interior seed to 00461770; image unchanged");
 }
}
