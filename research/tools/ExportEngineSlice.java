// Bounded engine export with incoming references, instructions and data-reference witnesses.
// @category BulletWitch
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.util.*;
import java.io.*;
public class ExportEngineSlice extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();Path out=Paths.get(args[0]);Files.createDirectories(out.resolve("functions"));Files.createDirectories(out.resolve("instructions"));
  TreeMap<Address,Function> selected=new TreeMap<>();
  for(String value:Files.readAllLines(Paths.get(args[1]))) {
   if(value.isBlank()||value.startsWith("#"))continue;
   Address a=toAddr(Long.parseUnsignedLong(value.trim(),16));Function f=getFunctionAt(a);if(f==null)f=getFunctionContaining(a);
   if(f==null){disassemble(a);f=createFunction(a,null);}if(f==null){println("UNRESOLVED "+a);continue;}
   selected.put(f.getEntryPoint(),f);
   for(Function c:f.getCalledFunctions(monitor))if(!c.isExternal())selected.put(c.getEntryPoint(),c);
   for(Reference r:getReferencesTo(a)){Function caller=getFunctionContaining(r.getFromAddress());if(caller!=null)selected.put(caller.getEntryPoint(),caller);}
  }
  DecompInterface dc=new DecompInterface();dc.openProgram(currentProgram);
  try(PrintWriter idx=new PrintWriter(Files.newBufferedWriter(out.resolve("status.tsv")));PrintWriter refs=new PrintWriter(Files.newBufferedWriter(out.resolve("references.tsv")))) {
   idx.println("address\tname\tbytes\tstatus");refs.println("target\tfrom\ttype\tcontaining_function");
   for(Function f:selected.values()) {
    Address a=f.getEntryPoint();for(Reference r:getReferencesTo(a)){Function c=getFunctionContaining(r.getFromAddress());refs.println(a+"\t"+r.getFromAddress()+"\t"+r.getReferenceType()+"\t"+(c==null?"data":c.getEntryPoint()));}
    DecompileResults d=dc.decompileFunction(f,35,monitor);boolean ok=d.decompileCompleted()&&d.getDecompiledFunction()!=null;
    if(ok)Files.writeString(out.resolve("functions/"+a+".c"),"/* Ghidra pseudocode: inferred types, not original source. */\n"+d.getDecompiledFunction().getC());
    idx.println(a+"\t"+f.getName()+"\t"+f.getBody().getNumAddresses()+"\t"+(ok?"ok":d.getErrorMessage()));idx.flush();
    try(PrintWriter ins=new PrintWriter(Files.newBufferedWriter(out.resolve("instructions/"+a+".txt")))){
     InstructionIterator it=currentProgram.getListing().getInstructions(f.getBody(),true);
     while(it.hasNext()){Instruction i=it.next();ins.println(i.getAddress()+"  "+i);for(Reference r:i.getReferencesFrom()){Data data=getDataContaining(r.getToAddress());if(data!=null)ins.println("  ref "+r.getToAddress()+" "+data.toString());}}
    }
   }
  }finally{dc.dispose();}
  println("Engine slice exported "+selected.size()+" functions");
 }
}
