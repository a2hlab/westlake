"""Build the opt-in Android stdio process lifecycle boundary with strict linking."""
from pathlib import Path
import subprocess,sys,hashlib
root=Path(__file__).resolve().parent
sdk=Path('/home/dspfac/ohos-sdk-6.1/linux/native')
src=Path('/home/dspfac/bridge-build/src/framework/android-runtime/src/oh_popen_boundary.cpp')
out=root/(sys.argv[1] if len(sys.argv)>1 else 'liboh_popen_rebuilt.so')
assert out.parent==root and not out.exists()
subprocess.run([str(sdk/'llvm/bin/clang++'),'--target=aarch64-linux-ohos','--sysroot='+str(sdk/'sysroot'),'-shared','-fPIC','-O2','-std=c++17','-Wall','-Wextra','-Werror','-Wl,-z,defs',str(src),'-ldl','-o',str(out)],check=True)
print(hashlib.sha256(out.read_bytes()).hexdigest(),out.name)
