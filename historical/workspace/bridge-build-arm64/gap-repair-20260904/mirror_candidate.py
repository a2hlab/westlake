"""Copy a completely staged candidate into the private runtime, then verify it."""
import sys,json,re
import device
name=sys.argv[1];assert re.fullmatch('candidate[0-9]+',name)
base='/data/local/tmp/asx';mirror='/data/app/el2/100/base/org.westlake.imehost/files/android-runtime-gap-17'
rel='gap-repair-20260904/'+name
boot='fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/gap-repair-20260904-'+name
result=device.shell('VALIDATE_ONLY=1 sh '+base+'/'+rel+'/run.sh');assert 'GAP_INPUTS_OK' in result,result
for path in [rel,boot]:
 assert 'EXISTS' not in device.shell('test -e '+mirror+'/'+path+' && echo EXISTS')
 copied=device.shell('cp -R '+base+'/'+path+' '+mirror+'/'+path+' && chown -R 20010054:20010054 '+mirror+'/'+path+' && echo COPY_COMPLETE')
 assert 'COPY_COMPLETE' in copied,copied
checks=[]
for path,sha in json.loads((device.ROOT/name/'manifest.json').read_text()).items():
 if path.startswith('boot/'):
  if not path.endswith(('.art','.oat','.vdex')):continue
  target=mirror+'/'+boot+'/'+path.split('/')[-1]
 else:target=mirror+'/'+rel+'/'+path
 checks.append('test "$(sha256sum '+target+' | cut -d \' \' -f1)" = '+sha+' || exit 1')
result=device.save(name+'-private-verified.log','; '.join(checks)+'; echo PRIVATE_HASHES_OK')
assert 'PRIVATE_HASHES_OK' in result,result
print(result)
