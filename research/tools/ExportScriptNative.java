// Script API naming and incremental native decompilation export.
// @category BulletWitch
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.io.*;

public class ExportScriptNative extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs(); Path labels=Paths.get(args[0]);Path out=Paths.get(args[1]);Files.createDirectories(out.resolve("functions"));
  LinkedHashMap<Address,String> seeds=new LinkedHashMap<>();
  for(String line:Files.readAllLines(labels)) {String[] v=line.split("\t");Address a=toAddr(Long.parseUnsignedLong(v[0],16));seeds.put(a,v[1]);Function f=getFunctionAt(a);if(f==null){disassemble(a);f=createFunction(a,v[1]);}if(f!=null)f.setName(v[1],SourceType.USER_DEFINED);}
  // Include direct native dependencies recursively, while recording unresolved indirect calls.
  TreeMap<Address,Function> selected=new TreeMap<>();ArrayDeque<Function> q=new ArrayDeque<>();
  for(Address a:seeds.keySet()){Function f=getFunctionAt(a);if(f!=null)q.add(f);}
  while(!q.isEmpty()){Function f=q.removeFirst();if(selected.putIfAbsent(f.getEntryPoint(),f)!=null)continue;for(Function c:f.getCalledFunctions(monitor))if(!c.isExternal() && !selected.containsKey(c.getEntryPoint()))q.add(c);}
  DecompInterface decomp=new DecompInterface();DecompileOptions options=new DecompileOptions();options.grabFromProgram(currentProgram);decomp.setOptions(options);decomp.openProgram(currentProgram);
  int ok=0,failed=0;
  try(PrintWriter index=new PrintWriter(Files.newBufferedWriter(out.resolve("status.tsv")));PrintWriter calls=new PrintWriter(Files.newBufferedWriter(out.resolve("calls.tsv")));PrintWriter refs=new PrintWriter(Files.newBufferedWriter(out.resolve("seed-references.tsv")))){
   index.println("address\tname\tseed\tstatus\tbytes\tmessage");calls.println("caller\tsite\ttarget\tkind");refs.println("target\tfrom\ttype");
   for(Address a:seeds.keySet())for(Reference ref:getReferencesTo(a))refs.println(a+"\t"+ref.getFromAddress()+"\t"+ref.getReferenceType());refs.flush();
   for(Function f:selected.values()){
    if(monitor.isCancelled())break;String va=f.getEntryPoint().toString();
    try {
     DecompileResults result=decomp.decompileFunction(f,45,monitor);
     if(result.decompileCompleted() && result.getDecompiledFunction()!=null){String c="/* Automatic Ghidra 12.1.4 pseudocode; inferred types and names. Not buildable recovered source. */\n"+result.getDecompiledFunction().getC();Files.writeString(out.resolve("functions").resolve(va+".c"),c);index.println(va+"\t"+f.getName()+"\t"+seeds.containsKey(f.getEntryPoint())+"\tok\t"+f.getBody().getNumAddresses()+"\t"+result.getErrorMessage().replace('\n',' '));ok++;}
     else{index.println(va+"\t"+f.getName()+"\t"+seeds.containsKey(f.getEntryPoint())+"\tunresolved\t"+f.getBody().getNumAddresses()+"\t"+result.getErrorMessage().replace('\n',' '));failed++;}
    }catch(Exception ex){index.println(va+"\t"+f.getName()+"\t"+seeds.containsKey(f.getEntryPoint())+"\tunresolved\t0\t"+ex.toString().replace('\n',' '));failed++;}
    InstructionIterator it=currentProgram.getListing().getInstructions(f.getBody(),true);
    while(it.hasNext()){Instruction ins=it.next();if(!ins.getFlowType().isCall())continue;Address[] flows=ins.getFlows();if(flows.length==0)calls.println(va+"\t"+ins.getAddress()+"\t?\tindirect-unresolved");else for(Address target:flows)calls.println(va+"\t"+ins.getAddress()+"\t"+target+"\tdirect");}
    index.flush();calls.flush();if((ok+failed)%100==0)println("EXPORT progress "+(ok+failed)+"/"+selected.size()+" ok="+ok+" unresolved="+failed);
   }
  }finally{decomp.dispose();}
  Files.writeString(out.resolve("summary.txt"),"seed addresses="+seeds.size()+"\nselected functions="+selected.size()+"\ndecompiled="+ok+"\nunresolved="+failed+"\ntotal analyzed functions="+currentProgram.getFunctionManager().getFunctionCount()+"\nScope: script API wrappers, callbacks, startup anchors and recursively reachable direct callees. Indirect calls remain in calls.tsv.\n");
  println("EXPORT complete: "+ok+" decompiled, "+failed+" unresolved");
 }
}
