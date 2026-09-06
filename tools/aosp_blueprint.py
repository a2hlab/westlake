"""Resolve literal Blueprint filegroups for the source framework build."""
import ast,json,re
from pathlib import Path
# Remove comments without altering quoted strings; retain nesting boundaries.
lex=re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|[A-Za-z_][A-Za-z_0-9.]*|\d+|[^\s]',re.S)
def tokens(text): return [x for x in lex.findall(text) if not x.startswith('//') and not x.startswith('/*')]
def value(ts,i):
 t=ts[i]
 if t=='[':
  out=[];i+=1
  while ts[i]!=']':
   x,i=expr(ts,i);out.append(x)
   if ts[i]==',':i+=1
  return out,i+1
 if t=='{':
  out={};i+=1
  while ts[i]!='}':
   key=ts[i];i+=1
   if ts[i]!=':': raise ValueError('Expected colon: '+str(ts[i:i+4]))
   x,i=expr(ts,i+1);out[key]=x
   if ts[i]==',':i+=1
  return out,i+1
 if t.startswith('"'):return ast.literal_eval(t),i+1
 if t in ('true','false'):return t=='true',i+1
 if t.isdigit():return int(t),i+1
 if i+1<len(ts) and ts[i+1]=='(':
  start=i;i+=2;depth=1
  while depth:
   if ts[i]=='(':depth+=1
   if ts[i]==')':depth-=1
   i+=1
  return {'call':ts[start:i]},i
 return {'variable':t},i+1
def expr(ts,i):
 first,i=value(ts,i)
 while i<len(ts) and ts[i]=='+':
  other,i=value(ts,i+1);first={'add':[first,other]}
 return first,i
def evaluate(x,env):
 if isinstance(x,list):return [evaluate(v,env) for v in x]
 if isinstance(x,dict):
  if 'variable' in x:return evaluate(env[x['variable']],env)
  if 'add' in x:
   a,b=x['add'];return evaluate(a,env)+evaluate(b,env)
  raise ValueError('Unsupported expression '+str(x)[:180])
 return x

def index(root):
 modules={};fail=[]
 pending=sorted(root.glob('*/**/Android.bp'));seen=set()
 while pending:
  path=pending.pop(0)
  if '.git' in path.parts or path in seen:continue
  seen.add(path)
  try:
   ts=tokens(path.read_text());i=0;env={}
   while i<len(ts):
    name=ts[i];op=ts[i+1];i+=2
    if op=='{':
     fields,i=value(ts,i-1)
     if isinstance(fields.get('name'),str):modules.setdefault(fields['name'],[]).append({'kind':name,'fields':fields,'file':str(path.relative_to(root)),'environment':dict(env)})
    elif op=='=':env[name],i=expr(ts,i)
    elif op=='+' and ts[i]=='=':
     x,i=expr(ts,i+1);env[name]={'add':[env[name],x]}
    else:raise ValueError('Unexpected top-level token '+op)
   # Blueprint reads Android.bp plus its explicit build includes. Files such
   # as *_Android.bp under test-data directories are parser fixtures, not
   # active module declarations.
   for pattern in evaluate(env.get('build',[]),env):
    if Path(pattern).is_absolute() or '..' in Path(pattern).parts:raise ValueError('Build include escapes source directory')
    included=sorted(path.parent.glob(pattern))
    if not included:raise ValueError('Missing Blueprint build include '+pattern)
    pending.extend(included)
  except Exception as e:fail.append((str(path.relative_to(root)),str(e)))
 return modules,fail

class Sources:
 def __init__(self,root,modules):
  self.root=root;self.modules=modules;self.definitions=set()
 def module(self,name):
  choices=self.modules.get(name,[])
  if len(choices)!=1:raise ValueError('Missing/ambiguous Blueprint module '+name+': '+str([m['file'] for m in choices]))
  self.definitions.add(choices[0]['file']);return choices[0]
 def files(self,name,field='srcs',stack=()):
  if name in stack:raise ValueError('Recursive source group '+name)
  module=self.module(name);base=self.root/Path(module['file']).parent
  fields=module['fields'];patterns=evaluate(fields.get(field,[]),module['environment'])
  selected=set()
  for pattern in patterns:
   if pattern.startswith(':'):selected.update(self.files(pattern[1:],stack=stack+(name,)));continue
   matches={p for p in base.glob(pattern) if p.is_file()}
   if not matches and not any(c in pattern for c in '*?['):raise ValueError('Missing source '+name+': '+pattern)
   selected.update(matches)
  for pattern in evaluate(fields.get('exclude_srcs',[]),module['environment']):selected.difference_update(base.glob(pattern))
  return sorted(selected)
