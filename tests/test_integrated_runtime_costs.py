"""Execute the actual prefetch scheduler through scene changes, without SD IO."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from prepare_integrated_runtime_costs import derive_fs
from prepare_integrated_native_pause import block

DRIVER=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
using u32=unsigned;
static unsigned scene=1,waits=0,clears=0,player_calls=0,mg_calls=0;
static bool g_prefetch_stop=false;
static unsigned g_prefetch_count=0;
constexpr unsigned PREFETCH_MAX=2048,PREFETCH_MAX_FILE=100,PREFETCH_MAX_MG_FILE=200;
static bool prefetch_wanted(const char*){return true;}
static bool prefetch_recroom(const char*){return true;}
static bool prefetch_mg_common(const char*){return true;}
static bool prefetch_mg_rest(const char*){return true;}
static bool recroom_prefetch_enabled(){return true;}
static bool prefetch_minigame_side(){return scene==5||(scene>=361&&scene<=396);}
static bool prefetch_gameplay_side(){return scene==3||scene==4;}
struct PrefetchPass {const char* name;bool(*wanted)(const char*);u32 max_file;bool(*yield)();u32 files,bytes;unsigned long long ticks;bool done;};
static void prefetch_run(PrefetchPass& pass){
 if(pass.done||g_prefetch_stop)return;
 if(pass.yield&&pass.yield())return;
 if(!std::strcmp(pass.name,"player/2D")){assert(!prefetch_minigame_side());++player_calls;}
 else {assert(prefetch_minigame_side());++mg_calls;}
 pass.done=true;++g_prefetch_count;
}
static void sm64ds_prefetch_wait_begin(){++clears;}
static void sm64ds_prefetch_wait(){
 ++waits;assert(player_calls==1&&!mg_calls);
 if(waits==1)scene=3; // adventure remains free of MG reads
 else if(waits==2)scene=5; // explicit Rec Room entry wakes the scheduler
 else assert(false);
}
'''

class RuntimeCosts(unittest.TestCase):
    def test_real_prefetch_loop_defers_mg_and_wakes_for_scene(self):
        original=(ROOT/'third_party/sm64ds-3ds/port/hal/fs.cpp').read_text()
        patched=derive_fs(original)
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);compiler=shutil.which('clang++') or shutil.which('c++')
            self.assertTrue(compiler)
            for kind,text in [('original',original),('patched',patched)]:
                start,end,_=block(text,'static void prefetch_thread(void *)')
                driver=p/(kind+'.cpp')
                driver.write_text(DRIVER+text[start:end]+'''
int main(){prefetch_thread(nullptr);assert(player_calls==1&&mg_calls==3&&waits==2&&clears==3);}
''')
                executable=p/kind
                subprocess.run([compiler,'-std=c++17','-Wno-unused-function','-O1','-g',
                    '-fsanitize=address,undefined',str(driver),'-o',str(executable)],check=True,capture_output=True)
                result=subprocess.run([str(executable)],capture_output=True)
                if kind=='original':self.assertNotEqual(result.returncode,0)
                else:self.assertEqual(result.returncode,0,result.stderr.decode())
        with self.assertRaises((ValueError,AssertionError)):derive_fs(patched)

if __name__=='__main__':unittest.main()
