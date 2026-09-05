from pathlib import Path
import subprocess
import zipfile
import hashlib
ROOT=Path(__file__).resolve().parent
SDK=Path('/home/dspfac/android-sdk'); BT=SDK/'build-tools/34.0.0'; API=SDK/'platforms/android-34/android.jar'
OUT=ROOT/'control-out';OUT.mkdir(exist_ok=True);(OUT/'classes').mkdir(exist_ok=True)
def run(*a): subprocess.run([str(x) for x in a],check=True)
run('javac','--release','8','-parameters','-encoding','UTF-8','-classpath',API,'-d',OUT/'classes',*sorted((ROOT/'control').glob('*.java')))
run(BT/'d8','--min-api','28','--lib',API,'--output',OUT,*sorted((OUT/'classes').rglob('*.class')))
run(BT/'aapt2','link','-I',API,'--manifest',ROOT/'control/AndroidManifest.xml','--min-sdk-version','28','--target-sdk-version','28','-o',OUT/'unsigned.apk')
with zipfile.ZipFile(OUT/'unsigned.apk','a') as z:
    z.write(OUT/'classes.dex','classes.dex')
    for p in (ROOT/'cert-fixtures').glob('*.pem'):z.write(p,'assets/certs/'+p.name)
key=OUT/'development.p12'
if not key.exists():run('keytool','-genkeypair','-keystore',key,'-storepass','android','-keypass','android','-alias','development','-keyalg','RSA','-keysize','2048','-validity','3650','-dname','CN=Westlake Development')
run(BT/'apksigner','sign','--ks',key,'--ks-pass','pass:android','--out',OUT/'control.apk',OUT/'unsigned.apk')
print('CONTROL_SHA256',hashlib.sha256((OUT/'control.apk').read_bytes()).hexdigest())
