#!/usr/bin/env python3
"""Restore five omitted locals from register lifetimes; require exact bytecode comparison."""
from pathlib import Path
import json,re,subprocess,hashlib
from lua50_read import parse,Reader
R=Path(__file__).resolve().parents[1];O=R/'evidence/script-corpus';E=R/'experiments/decompilation/repairs';E.mkdir(exist_ok=True)
fixes={
'root.lub':{'titlemenu':'''function titlemenu()
  _ExecCharacterLoad()
  _WaitCharacterLoad()
  _ExecTitleMenu()
  _WaitTitleMenu()
  local status = _GetTitleMenuStatus()
  if status == 1 then
    stage = 1
    _SetGlobalValue(0, stage)
    mode = 10
  elseif status == 2 then
    stage = _GetGlobalValue(0)
    mode = 10
  elseif status == 3 then
    stage = _GetLatestStageSelectNo()
    mode = 11
  elseif status == 6 then
    mode = 12
  elseif status == 7 then
    mode = 1
  else
    mode = 1
  end
end
''','ending':'''function ending()
  local count = _GetGlobalValue(4) + 1
  _SetGlobalValue(4, count)
  _ExecBonusOpen(0)
  _WaitBonusOpen()
  _ExecSaveGame(0)
  _WaitSaveGame()
  _PlayMovie("BW6_0600")
  _WaitMovie()
  mode = 1
end
'''},
'sample.lub':{'load':'''function load()
  _ExecLoadingScene(1)
  local checkpoint = _GetCheckPoint()
  if checkpoint == 0 then
    _LoadStage("st01_a")
    _LoadSky("sky01")
    _LoadCharaLight("st01")
    _LoadAddedEffect("effect_boss1")
    _LoadBgObjSetup("bgobj_idtest")
    _LoadSettbl("sample")
  end
  _LoadScoreFile("score")
end
'''},
 'test_save_01.lub':{'load':'''function load()
  local checkpoint = _GetCheckPoint()
  if checkpoint == 0 then
    _LoadStage("st01_a")
    _LoadSky("sky01")
    _LoadCharaLight("st01")
    _LoadBgObjSetup("st01_00")
    _LoadSettbl("test_save_01_00")
  elseif checkpoint == 1 then
    _UnloadAll()
    _LoadStage("st01_b")
    _LoadSky("sky01")
    _LoadCharaLight("st01")
    _LoadSettbl("test_save_01_01")
  end
end
''','start':'''function start()
  _PadPause()
  local checkpoint = _GetCheckPoint()
  if checkpoint == 0 then
    _SetPlayerPosition(237.23, 35.01, 322.26, -43)
    _SetEnemy(0)
    _SetTrapCallback("sheet01_trap00", 0, 0)
    _PlayBgm(2)
  elseif checkpoint == 1 then
    _SetPlayerPosition(41.13, 0.01, 246.28, -90)
    _SetTrapCallback("sheet02_trap00", 0, 0)
    _PlayBgm(3)
  end
  _FadeIn(0.5)
  _FadeWait()
  _PadResume()
  _WaitStageNameDisp()
end
'''}}
def norm(f):return {'params':f['parameter_count'],'upvalues':f['upvalue_count'],'vararg':f['vararg'],'stack':f['stack_size'],'constants':f['constants'],'instructions':[i['word'] for i in f['code']],'children':[norm(c) for c in f['children']]}
rows=[]
for item in json.loads((O/'inventory.json').read_text()):
 if item['name'] not in fixes:continue
 ident=item['id'];source=(R/'extracted/scripts-no-locals'/(ident+'.lua')).read_text()
 for name,body in fixes[item['name']].items():
  pattern=r'(?ms)^function '+re.escape(name)+r'\(\).*?(?=^function |\Z)'
  source,n=re.subn(pattern,lambda _:body+'\n',source);assert n==1
 path=E/(ident+'.lua');path.write_text(source);dest=E/(ident+'.lub')
 p=subprocess.run([str(R/'vendor/lua-5.0.3/bin/luac'),'-o',str(dest),str(path)],capture_output=True)
 row={'id':ident,'repairs':list(fixes[item['name']]),'source':str(path.relative_to(R)),'compile_exit':p.returncode,'stderr':p.stderr.decode()}
 if p.returncode==0:
  data=dest.read_bytes();assert data[:14]==bytes.fromhex('1b4c756150010408080608090908');r=Reader(data,8,8);r.take(22);rebuilt=r.function();assert r.pos==len(data)
  old=parse((R/item['extracted']).read_bytes())['root'];a=norm(old);b=norm(rebuilt);row['exact_normalized_match']=a==b
  if a!=b:(E/(ident+'.comparison.json')).write_text(json.dumps({'original':a,'recompiled':b},indent=2)+'\n')
 rows.append(row)
(O/'local-repairs.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows,indent=2))
