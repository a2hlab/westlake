#!/usr/bin/env python3
"""Compare source-built popen probes through OH HDC and Android ADB shell identities."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['hdc','adb','oh-serial','android-serial','run']:
        parser.add_argument('--'+name,required=True)
    for name in ['oh-build','android-build','native-build','out']:
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    if not re.fullmatch('[a-z0-9-]+',args.run) or args.out.exists():
        parser.error('Use a simple run name and a new output directory')
    args.out.mkdir(parents=True)
    remote='/data/local/tmp/a2hlab-parity-'+args.run
    results={}
    for platform,transport,build in [('oh',[args.hdc,'-t',args.oh_serial],args.oh_build),
                                      ('android',[args.adb,'-s',args.android_serial],args.android_build)]:
        def call(arguments,cwd=None):
            proc=subprocess.run(transport+arguments,cwd=cwd,capture_output=True,timeout=45)
            text=proc.stdout.decode(errors='replace').replace('\r','')
            with (args.out/'commands.jsonl').open('a') as journal:
                journal.write(json.dumps(dict(platform=platform,args=arguments,rc=proc.returncode,stdout=text,
                                              stderr=proc.stderr.decode(errors='replace')))+'\n')
            if proc.returncode or '[Fail]' in text:raise RuntimeError(text+proc.stderr.decode(errors='replace'))
            return text
        if 'EXISTS' in call(['shell','if [ -e '+remote+' ]; then echo EXISTS; fi']):
            raise ValueError('Refusing to overwrite an existing remote probe directory')
        call(['shell','mkdir '+remote+' && chmod 755 '+remote])
        paths=[build/name for name in ['popen_contract_probe','popen_stress_probe']]
        if platform=='oh':paths.append(args.native_build/'liboh_popen_boundary.so')
        hashes={}
        for path in paths:
            target=remote+'/'+path.name
            call((['file','send'] if platform=='oh' else ['push'])+[path.name,target],cwd=path.parent)
            digest=hashlib.sha256(path.read_bytes()).hexdigest()
            if call(['shell','sha256sum '+target]).split()[0]!=digest:raise ValueError('Transport hash mismatch')
            call(['shell','chmod 755 '+target]);hashes[path.name]=digest
        identity=call(['shell','id; cat /proc/self/attr/current'])
        (args.out/(platform+'-identity.log')).write_text(identity)
        preload='LD_PRELOAD='+remote+'/liboh_popen_boundary.so ' if platform=='oh' else ''
        for name,binary,option in [('allowed','popen_contract_probe',''),('denied','popen_contract_probe',' --deny-exec'),('stress','popen_stress_probe','')]:
            text=call(['shell',preload+remote+'/'+binary+option])
            (args.out/(platform+'-'+name+'.log')).write_text(text)
            if 'failures=0' not in text:raise ValueError('Probe assertions failed')
        results[platform]=dict(input_sha256=hashes,contract_allowed=True,contract_denied=True,stress=True,
                               identity_scope='transport shell; not an ordinary app-domain acceptance test')
    (args.out/'verification.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PARITY_NATIVE_CONTRACTS_PASS')

if __name__=='__main__':main()
