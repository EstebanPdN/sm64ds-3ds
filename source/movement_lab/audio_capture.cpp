// Diagnostic-only tap of the exact stereo PCM submitted to NDSP.
// It never feeds the mixer, changes gain, or substitutes audio assets.
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
constexpr unsigned capacity=8*1024*1024;
char* pcm=nullptr;
unsigned used=0;
bool initialized=false,enabled=false;
RecursiveLock lock;
}
extern "C" void __real_ndspChnWaveBufAdd(int,ndspWaveBuf*);
extern "C" unsigned __real__ZN5Sound13PlayCharVoiceEjjRK7Vector3(unsigned,unsigned,const void*);
extern "C" unsigned __wrap__ZN5Sound13PlayCharVoiceEjjRK7Vector3(unsigned character,unsigned voice,const void* pos){
 const unsigned result=__real__ZN5Sound13PlayCharVoiceEjjRK7Vector3(character,voice,pos);
 if(std::getenv("SM64DS_LAB_REPLAY")||std::getenv("SM64DS_LAB_TRACE"))std::printf("MOVEMENT_DS_VOICE char=%u voice=%u handle=%u\n",character,voice,result);
 return result;
}
extern "C" void __wrap_ndspChnWaveBufAdd(int channel,ndspWaveBuf* buffer){
 __real_ndspChnWaveBufAdd(channel,buffer);
 if(channel!=0)return;
 if(!initialized){
  RecursiveLock_Init(&lock);
  enabled=std::getenv("SM64DS_LAB_REPLAY")!=nullptr;
  if(enabled)pcm=static_cast<char*>(std::malloc(capacity));
  enabled=enabled && pcm;initialized=true;
 }
 RecursiveLock_Lock(&lock);
 if(enabled){
  const unsigned bytes=buffer->nsamples*4;
  if(used+bytes<=capacity){std::memcpy(pcm+used,buffer->data_pcm16,bytes);used+=bytes;}
  else enabled=false;
 }
 RecursiveLock_Unlock(&lock);
}
extern "C" void movement_lab_finish_audio(){
 if(!initialized)return;
 RecursiveLock_Lock(&lock);enabled=false;
 if(pcm){
  auto* file=std::fopen("sdmc:/3ds/sm64ds/movement-lab.pcm","wb");
  const unsigned written=file?std::fwrite(pcm,1,used,file):0;
  if(file)std::fclose(file);
  std::printf("MOVEMENT_AUDIO bytes=%u written=%u rate=32728.498 stereo=s16le\n",used,written);
  std::free(pcm);pcm=nullptr;
 }
 RecursiveLock_Unlock(&lock);
}
