"""Generate isolated EC test certificates, never install device trust roots."""
from pathlib import Path
import subprocess,json,hashlib
ROOT=Path(__file__).resolve().parent;OUT=ROOT/'cert-fixtures';OUT.mkdir(exist_ok=True)
assert not (OUT/'manifest.json').exists(),'Preserve fixture evidence'
key=['-keystore',str(OUT/'test-only.p12'),'-storepass','development','-keypass','development']
def run(*args):subprocess.run([str(x) for x in args],check=True)
for alias in ('root','other','leaf'):
    extensions=['-ext','bc=ca:true','-ext','ku=keyCertSign,cRLSign'] if alias!='leaf' else []
    run('keytool','-genkeypair',*key,'-alias',alias,'-keyalg','EC','-groupname','secp256r1','-dname','CN=Westlake Test '+alias,
        '-startdate','2025/01/01 00:00:00','-validity','3650',*extensions)
for alias in ('root','other'):run('keytool','-exportcert',*key,'-alias',alias,'-rfc','-file',OUT/(alias+'.pem'))
run('keytool','-certreq',*key,'-alias','leaf','-file',OUT/'leaf.csr')
for name,date,days in [('valid','2025/01/01 00:00:00','1825'),('expired','2020/01/01 00:00:00','1')]:
    run('keytool','-gencert',*key,'-alias','root','-infile',OUT/'leaf.csr','-outfile',OUT/(name+'.pem'),'-rfc',
        '-startdate',date,'-validity',days,'-ext','ku=digitalSignature','-ext','eku=serverAuth','-ext','san=dns:fixture.test')
(OUT/'manifest.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.glob('*.pem')},indent=2)+'\n')
