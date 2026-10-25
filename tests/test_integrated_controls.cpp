// Settings persistence and scoped dump deletion against production sources.
#include "platform/3ds/enhance_settings.h"
#include "platform/3ds/diagnostic_files.hpp"
#include "platform/3ds/audio_gain.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
int main(){
 for(int sample=-32768;sample<=32767;++sample){assert(enhance::audio_gain(int16_t(sample),100)==sample);assert(enhance::audio_gain(int16_t(sample),0)==0);const auto boosted=enhance::audio_gain(int16_t(sample),200);assert(sample>=0?boosted>=sample:boosted<=sample);}
 assert(enhance::audio_gain(32767,200)==32767 && enhance::audio_gain(-32768,200)==-32768);
 auto s=enhance::Settings::safe();assert(s.audio_volume==100 && s.res3d==enhance::Res3d::Native && s.screen_mode==1);
 auto audio=enhance::parse_ini("[audio]\nvolume=200\n",s);assert(audio.applied==1 && s.audio_volume==200);
 audio=enhance::parse_ini("[audio]\nvolume=201\n",s);assert(audio.invalid==1 && s.audio_volume==200);
assert(s.camera_cstick==2 && s.circle_pad_analog);
 auto r=enhance::parse_ini("[display]\nmode=wide\n[input]\nmode=2\ncamera_sensitivity=200\ncamera_pitch_sensitivity=25\ncamera_deadzone=30\nmove_sensitivity=125\nrun_threshold=50\n[debug]\ndumps=1\nselect_overlay=1\n",s);
 assert(r.invalid==0 && r.unknown==0);assert(s.screen_mode==1 && s.controller_mode==2);
 assert(s.camera_sensitivity==200 && s.camera_pitch_sensitivity==25 && s.camera_deadzone==30);
 assert(s.move_sensitivity==125 && s.run_threshold==50 && s.developer_dumps && s.developer_overlay);
 auto before=s;r=enhance::parse_ini("[input]\ncamera_sensitivity=201\nrun_threshold=24\ncamera_deadzone=31\nmode=3\n",s);
 assert(r.invalid==4 && s==before);
 char dir[]="/tmp/sm64ds-options-XXXXXX";assert(mkdtemp(dir));
 std::string path=std::string(dir)+"/settings.ini";
 assert(enhance::save_file(path.c_str(),s));auto loaded=enhance::Settings::safe();assert(enhance::load_file(path.c_str(),loaded));assert(loaded==s);
 enhance::publish(loaded);assert(enhance::snapshot()==s && enhance::live().controller_mode==2 && enhance::live().audio_volume==200);
 auto create=[&](const char* name){std::string f=std::string(dir)+"/"+name;FILE* out=fopen(f.c_str(),"wb");assert(out);assert(fclose(out)==0);return f;};
 auto render=create("native-0123456789abcdef.render");
 auto vm=create("native-0123456789abcdef.vm"),trace=create("native-0123456789abcdef.trace"),meta=create("native-0123456789abcdef.anchors.json");
 auto rom=create("rom.nds"),wrong=create("native-0123456789abcdef.vm.bak"),save=create("save.bin");
 std::string link=std::string(dir)+"/native-ffffffffffffffff.vm";assert(symlink(rom.c_str(),link.c_str())==0);
 auto result=native_diagnostics::delete_dumps(dir);assert(result.removed==4 && result.failed==0);
 assert(access(rom.c_str(),F_OK)==0 && access(path.c_str(),F_OK)==0 && access(save.c_str(),F_OK)==0 && access(wrong.c_str(),F_OK)==0);
 assert(access(render.c_str(),F_OK)!=0);
 assert(access(vm.c_str(),F_OK)!=0 && access(trace.c_str(),F_OK)!=0 && access(meta.c_str(),F_OK)!=0);
 for(auto& f:{rom,path,save,wrong,link})assert(remove(f.c_str())==0);assert(rmdir(dir)==0);
 puts("PASS settings roundtrip, bounds, live values and scoped dump deletion; ROM/save/settings and symlinks preserved");
}
