// Local harness: select existing unluac variable-finder or test a no-locals hypothesis.
// No upstream source changes. No-locals output is only accepted after bytecode comparison.
import unluac.*;
import unluac.parse.*;
import unluac.decompile.*;
import java.nio.*;
import java.nio.file.*;
public class UnluacCorpus {
 static void clearStripped(LFunction f){f.stripped=false;for(LFunction c:f.functions)clearStripped(c);}
 public static void main(String[] args)throws Exception{
  Configuration config=new Configuration();
  if(args[0].equals("finder"))config.variable=Configuration.VariableMode.FINDER;
  ByteBuffer b=ByteBuffer.wrap(Files.readAllBytes(Paths.get(args[1]))).order(ByteOrder.LITTLE_ENDIAN);
  LFunction main=new BHeader(b,config).main;
  if(args[0].equals("no-locals"))clearStripped(main);
  Decompiler d=new Decompiler(main);Decompiler.State state=d.decompile();Output out=new Output();d.print(state,out);out.finish();
 }
}
