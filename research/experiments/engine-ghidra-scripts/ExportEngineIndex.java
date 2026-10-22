// Read-only full-program native navigation index. @category BulletWitch
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.io.*;
public class ExportEngineIndex extends GhidraScript {
 public void run() throws Exception {
  Path out=Paths.get(getScriptArgs()[0]); Files.createDirectories(out);
  try(PrintWriter fs=new PrintWriter(Files.newBufferedWriter(out.resolve("functions.tsv")));
      PrintWriter rs=new PrintWriter(Files.newBufferedWriter(out.resolve("references.tsv")))) {
   fs.println("entry\tmin\tmax\tbytes\tname");
   for(Function f:currentProgram.getFunctionManager().getFunctions(true))
    fs.println(f.getEntryPoint()+"\t"+f.getBody().getMinAddress()+"\t"+f.getBody().getMaxAddress()+"\t"+f.getBody().getNumAddresses()+"\t"+f.getName());
   rs.println("from\tto\ttype\towner");
   AddressIterator ai=currentProgram.getReferenceManager().getReferenceSourceIterator(currentProgram.getMemory(),true);
   while(ai.hasNext()) {
    Address a=ai.next(); Function f=getFunctionContaining(a);
    for(Reference r:currentProgram.getReferenceManager().getReferencesFrom(a))
     rs.println(a+"\t"+r.getToAddress()+"\t"+r.getReferenceType()+"\t"+(f==null?"data":f.getEntryPoint()));
   }
  }
 }
}
