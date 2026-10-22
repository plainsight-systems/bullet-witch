// Correct a research-created function that started at an interior callsite.
// @category BulletWitch
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
public class RepairEngineBoundary extends GhidraScript {
 public void run() throws Exception {
  Function f=getFunctionAt(toAddr(0x45ef63));
  if(f!=null && f.getName().equals("FUN_0045ef63"))removeFunction(f);
  Function old=getFunctionAt(toAddr(0x45ef10));
  if(old!=null && old.getName().equals("FUN_0045ef10"))removeFunction(old);
  disassemble(toAddr(0x45ef10));
  if(getFunctionAt(toAddr(0x45ef10))==null)createFunction(toAddr(0x45ef10),null);
  Function fragment=getFunctionAt(toAddr(0x44b63d));
  if(fragment!=null && fragment.getName().equals("FUN_0044b63d"))removeFunction(fragment);
  println("Corrected research function boundary to 0045ef10; original executable unchanged");
 }
}
