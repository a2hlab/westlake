#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
extern "C" int SetSelfTokenID(unsigned long long);
extern "C" int setcon(const char*);
int probe(int fd,const char* kind){
 if(fd<0){printf("%s open_errno=%d\n",kind,errno);return 1;}
 if(ftruncate(fd,4096)){printf("%s truncate_errno=%d\n",kind,errno);close(fd);return 2;}
 void* rw=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);int ew=errno;
 void* rx=mmap(nullptr,4096,PROT_READ|PROT_EXEC,MAP_SHARED,fd,0);int ex=errno;
 printf("%s uid=%u rw=%d rw_errno=%d rx=%d rx_errno=%d\n",kind,getuid(),rw!=MAP_FAILED,rw==MAP_FAILED?ew:0,rx!=MAP_FAILED,rx==MAP_FAILED?ex:0);
 int failures=0;
 if(rw!=MAP_FAILED&&rx!=MAP_FAILED){
  auto* code=static_cast<uint32_t*>(rw);code[1]=0xd65f03c0;
  for(unsigned n=1;n<=64;n++){
   code[0]=0xd2800000|(n<<5);__builtin___clear_cache(static_cast<char*>(rw),static_cast<char*>(rw)+8);__builtin___clear_cache(static_cast<char*>(rx),static_cast<char*>(rx)+8);
   if(reinterpret_cast<unsigned(*)()>(rx)()!=n)failures++;
  }
  printf("%s code_updates=64 failures=%d\n",kind,failures);
 }
 if(rw!=MAP_FAILED)munmap(rw,4096);if(rx!=MAP_FAILED)munmap(rx,4096);close(fd);return failures;
}
int main(int argc,char** argv){
 if(argc!=4)return 2;
 unsigned uid=strtoul(argv[1],nullptr,0);
 if(uid){
  if(SetSelfTokenID(strtoull(argv[2],nullptr,0)))return 3;
  for(int c=0;prctl(PR_CAPBSET_READ,c,0,0,0)>=0;c++)if(prctl(PR_CAPBSET_DROP,c,0,0,0))return 4;
  gid_t groups[]={1097,3003,3004,3099};
  if(setgroups(4,groups)||setresgid(uid,uid,uid)||setresuid(uid,uid,uid)||prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||setcon("u:r:normal_hap:s0"))return 5;
 }
 probe(syscall(SYS_memfd_create,"jit-dual-probe",1),"memfd");
 char name[512];snprintf(name,sizeof(name),"%s/probe-XXXXXX",argv[3]);
 int fd=mkostemp(name,O_CLOEXEC);if(fd>=0&&unlink(name))return 6;
 return probe(fd,"private-file");
}
