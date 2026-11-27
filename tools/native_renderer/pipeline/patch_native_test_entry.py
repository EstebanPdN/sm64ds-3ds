from pathlib import Path
p=Path('port/platform/3ds/game_entry.cpp');t=p.read_text()
old='''#ifdef SM64DS_USER_BUILD
 // Keep manual play independent of the automated probes' env.txt and saves.'''
assert t.count(old)==1
t=t.replace(old,'''#if defined(SM64DS_PUBLIC_BETA)
 // Manual native play uses the same save as the installed native renderer.
 setenv("SM64DS_FRESH_NATIVE","1",1);
 setenv("SM64DS_FRESH_NATIVE_ASSETS","sdmc:/3ds/sm64ds/native",1);
 setenv("SM64DS_SAVE_PATH","sdmc:/3ds/sm64ds/native-test.sav",1);
#elif defined(SM64DS_NATIVE_TEST_BUILD)
 // Native-renderer test CIA: own title, save and log; prepared native assets
 // in RomFS; never reads the shared env.txt. The ROM package is shared.
 romfsInit();
 setenv("SM64DS_FRESH_NATIVE","1",1);
 setenv("SM64DS_FRESH_NATIVE_ASSETS","romfs:/native",1);
 setenv("SM64DS_PIPELINE","0",1);
 setenv("SM64DS_SAVE_PATH","sdmc:/3ds/sm64ds/native-test.sav",1);
#elif defined(SM64DS_USER_BUILD)
 // Keep manual play independent of the automated probes' env.txt and saves.''',1)
old='''#ifdef SM64DS_USER_BUILD
 std::freopen("sdmc:/3ds/sm64ds/user-test.log","w",stdout);stderr=stdout;'''
assert t.count(old)==1
t=t.replace(old,'''#if defined(SM64DS_NATIVE_TEST_BUILD) || defined(SM64DS_PUBLIC_BETA)
 std::freopen("sdmc:/3ds/sm64ds/native-test.log","w",stdout);stderr=stdout;
#elif defined(SM64DS_USER_BUILD)
 std::freopen("sdmc:/3ds/sm64ds/user-test.log","w",stdout);stderr=stdout;''',1)
p.write_text(t)
b=Path('build3ds/build_game.py');t=b.read_text()
old="parser.add_argument('--user-build', action='store_true', help='Separate manual-play save/log; ignore automated test settings')"
assert t.count(old)==1
t=t.replace(old,old+"\nparser.add_argument('--native-test', action='store_true', help='Fresh native renderer test CIA entry (own save/log, RomFS assets)')")
old="if args.user_build:arguments+=['-DSM64DS_USER_BUILD=1']"
assert t.count(old)==1
t=t.replace(old,old+"\nif args.native_test:arguments+=['-DSM64DS_NATIVE_TEST_BUILD=1']")
b.write_text(t)
