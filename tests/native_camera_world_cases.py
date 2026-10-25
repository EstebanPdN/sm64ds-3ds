"""Bounded ARM11 integration cases using the executor's real castle world.

No collision/service result is overridden. Controlled Actor/Camera positions
are fixture inputs; every collision uses the linked native original providers.
"""
import hashlib
import struct

STATS=struct.Struct('<10Q4I2i2hI')
NAMES=('publications consumed_ticks manual_updates duplicate_updates reseeds blocked '
       'constrained inverted_intervals last_tick last_vblank generation ready armed status '
       'desired_radius solved_radius yaw pitch elapsed_us').split()

def run(e,set_phase):
    cpu,call=e['cpu'],e['call'];camera,player=e['camera'],e['player']
    report=e['report'];queries=e['query_calls'];native=e['native']
    addr=e['data_address'];write=e['write'];sample=e['camera_sample']
    read=lambda p,n:bytes(cpu.mem_read(p,n))
    def u32(p):return struct.unpack('<I',read(p,4))[0]
    def put(p,v,fmt='<I'):cpu.mem_write(p,struct.pack(fmt,v))
    def stats():
        p=call('sm64ds_native_camera_stats')
        return dict(zip(NAMES,STATS.unpack(read(p,STATS.size))))
    def publish(x,y,t,v):
        call('sm64ds_native_camera_publish_q15',x,y,t&0xffffffff,t>>32,v&0xffffffff,v>>32)
    # Snapshot every writable mapped page, including private adapter state,
    # stack and the exact allocator arena. This permits true forked runs.
    def snapshot():
        return [(a,read(a,b-a+1)) for a,b,permissions in cpu.mem_regions() if permissions&2]
    def restore(state):
        for address,data in state:cpu.mem_write(address,data)
    game_spans=[(r['native'],r['end']-r['start']) for r in e['regions']]
    game_spans.append((e['heap_arena'],8*1024*1024))
    def video_hash():
        count=call('sm64ds_draw_count');p=call('sm64ds_draw_entries')
        assert count<=65536
        regs=call('_ZN6sm64ds20native_video_pointerEmj',0x04000000,0x2000)
        assert regs!=0
        return {'gx_words':count,'gx_sha256':hashlib.sha256(read(p,count*8)).hexdigest(),
                'register_sha256':hashlib.sha256(read(regs,0x2000)).hexdigest()}
    def game_hash():
        h=hashlib.sha256()
        for p,n in game_spans:h.update(read(p,n))
        return h.hexdigest()
    def semantics(q):
        keys=('kind','start_or_probe_fixed12','end_fixed12','return_value','hit',
              'hit_or_end_fixed12','distance_fixed12','ground_y_fixed12','completed')
        return {k:q[k] for k in keys if k in q}
    cases=[];report['manual_cases']=cases
    def invoke(label,function='func_02009e70'):
        set_phase(label);before_p=read(player,0x768);before_controls=e['controls']();start=len(queries)
        before=sample();result=call(function,camera);after=sample()
        record={'label':label,'result':result,'before':before,'after':after,'stats':stats(),
                'queries':[semantics(q) for q in queries[start:]],
                'player_unchanged':read(player,0x768)==before_p,
                'control_unchanged':e['controls']()==before_controls}
        if e['manual_finishes'] and e['manual_finishes'][-1]['phase']==label:
            record['finish_decision']=dict(e['manual_finishes'][-1])
        cases.append(record)
        assert result==1 and record['player_unchanged'] and record['control_unchanged'],label
        assert all(q['completed'] for q in queries[start:]),label
        return record
    def original_behavior(label):
        # Route only the candidate entry to the separately linked untouched
        # original body. Camera::Behavior and its real state PMF still run.
        # This branch replaces no collision/query/service or return value.
        entry=native['func_02009e70'];target=native['sm64ds_original_camera_body']
        original=read(entry,4);delta=(target-entry-8)//4
        assert (target-entry-8)%4==0 and -(1<<23)<=delta<(1<<23)
        try:
            cpu.mem_write(entry,struct.pack('<I',0xea000000|(delta&0xffffff)))
            return invoke(label,'_ZN6Camera8BehaviorEv')
        finally:cpu.mem_write(entry,original)
    base=snapshot()
    for label,pause in [('whole_behavior_neutral',0),('whole_behavior_paused',1)]:
        restore(base);write('data_0209fc48',pause);publish(0,0,1,0);seeded=snapshot()
        golden=original_behavior(label+'_golden');expected=game_hash()
        restore(seeded);actual=invoke(label+'_candidate','_ZN6Camera8BehaviorEv')
        assert game_hash()==expected and actual['queries']==golden['queries'],label
        actual['inactive_game_ram_equal']=True;actual['inactive_queries_equal']=True
    # Body-independent clock reset is startup only, not scene lifecycle.
    # Zero input, every admission gate, and disabled mode must preserve all
    # original game RAM effects and real query endpoints/results exactly.
    def inactive_pair(label,configure=lambda:None):
        restore(base);configure();publish(32768,32768,3,2)
        seeded=snapshot();start=len(queries)
        golden=invoke(label+'_golden','sm64ds_original_camera_body')
        expected=game_hash();expected_q=golden['queries']
        restore(seeded)
        actual=invoke(label+'_candidate');observed=game_hash()
        actual['inactive_game_ram_equal']=expected==observed
        actual['inactive_queries_equal']=expected_q==actual['queries']
        assert expected==observed and expected_q==actual['queries'],label
        assert actual['stats']['manual_updates']==0 and actual['stats']['armed']==0,label
    inactive_pair('no_previous_clock')
    # Establish a valid clock, leaving the controller unarmed.
    restore(base);publish(0,0,1,0);invoke('clock_initial');publish(0,0,2,1);invoke('neutral_unarmed')
    assert stats()['armed']==0
    base=snapshot()
    inactive_pair('paused_gate',lambda:write('data_0209fc48',1))
    for global_name in ('data_0209f294','data_0209f2c4','data_0209f20c'):
        inactive_pair(global_name+'_gate',lambda n=global_name:write(n,1,'<B'))
    inactive_pair('talk_gate',lambda:write('data_0209b454',0x40000000))
    for flag in (8,16,0x4000,0x8000):
        inactive_pair('flags_'+hex(flag),lambda f=flag:put(camera+0x154,u32(camera+0x154)|f))
    inactive_pair('registry_gate',lambda:write('data_0209f318',0))
    inactive_pair('camera_uid_gate',lambda:put(camera+4,u32(camera+4)+1))
    inactive_pair('camera_killed_gate',lambda:put(camera+0xf,1,'<B'))
    inactive_pair('target_killed_gate',lambda:put(player+0xf,1,'<B'))
    inactive_pair('recenter_gate',lambda:put(camera+0x1a0,1,'<H'))
    inactive_pair('keys_gate',lambda:write('data_0209f49c',0x100,'<H'))
    inactive_pair('state_gate',lambda:put(camera+0x138,addr('data_0209b008')+9*16))
    # All cases above are full body comparisons, not helper-only substitutes.
    # Extra argument-level gates cover resolver outputs that are not present
    # in this castle entrance; they do not claim those modes were executed.
    restore(base);publish(32768,0,3,2)
    for label,mode,view,secondary,minimum,maximum in [
        ('unproved_mode',addr('data_02086fcc'),0,0,1,1000000),
        ('scripted_view',addr('data_0208715c'),e['arena'],0,1,1000000),
        ('secondary_resolver',addr('data_0208715c'),0,1,1,1000000),
        ('bad_radius',addr('data_0208715c'),0,0,2,1)]:
        before=sample();result=call('sm64ds_native_camera_prepare',camera,mode,view,secondary,minimum,maximum)
        assert result==0 and sample()==before and stats()['armed']==0,label
        cases.append({'label':label,'helper_argument_gate_only':True,'result':result,'stats':stats(),'camera_unchanged':True})
    # Controlled consumption at 60/1: fresh acquisition has no retroactive
    # motion. Following input produces candidates before the real queries.
    restore(base);tick,vblank=3,2
    publish(32768,0,tick,vblank);seed=invoke('manual_acquisition')
    assert seed['stats']['armed']==1 and seed['stats']['manual_updates']==0
    tick+=1;vblank+=1;publish(32768,0,tick,vblank);active=invoke('manual_first_candidate')
    assert active['stats']['manual_updates']==1 and active['stats']['armed']==1
    assert active['stats']['elapsed_us']==16667
    assert active['queries'] and len(active['queries'])==9
    original_inputs=read(player,0x768),e['controls']()
    for index in range(12):
        tick+=1;vblank+=1;publish(32768,16384,tick,vblank);invoke('held_yaw_pitch_'+str(index))
    assert (read(player,0x768),e['controls']())==original_inputs
    # Duplicate publication freezes the sample and elapsed time. Dispatch may
    # solve spatial geometry again but cannot consume/integrate another tick.
    before=stats();publish(-32768,-32768,tick,vblank+100)
    duplicate=invoke('duplicate_tick')
    assert duplicate['stats']['consumed_ticks']==before['consumed_ticks']
    assert duplicate['stats']['last_vblank']==before['last_vblank']
    assert duplicate['stats']['duplicate_updates']==before['duplicate_updates']+1
    armed=stats()['armed'];assert armed==1
    for index in range(10):
        tick+=1;vblank+=1;publish(0,0,tick,vblank);neutral=invoke('neutral_'+str(index))
        assert neutral['stats']['armed']==1
    for index in range(10):
        tick+=1;vblank+=1;publish(-32768,-16384,tick,vblank);invoke('reverse_'+str(index))
    duplicate_state=snapshot()
    # If a later dispatch in the SAME tick takes the no-target early tail,
    # prepare is skipped. The earlier duplicate cannot keep ownership alive.
    invoke('early_tail_duplicate_prime');put(camera+0x110,0)
    tail=invoke('no_target_early_tail')
    assert tail['stats']['armed']==0 and tail['queries']==[]
    restore(duplicate_state)
    # A short pause (two VBlanks <=6) is precisely the catch-up regression:
    # original Behavior skips9e70, yet publication must stamp and disarm.
    restore(base);publish(32768,0,3,2);invoke('pause_acquisition')
    publish(32768,0,4,3);invoke('pause_active')
    before=stats();write('data_0209fc48',1)
    publish(32768,0,5,4);paused=invoke('short_pause_behavior','_ZN6Camera8BehaviorEv')
    assert paused['queries']==[] and paused['stats']['armed']==0
    assert paused['stats']['last_tick']==5 and paused['stats']['last_vblank']==4
    write('data_0209fc48',0);publish(32768,0,6,5);resumed=invoke('short_pause_resume','_ZN6Camera8BehaviorEv')
    assert resumed['stats']['manual_updates']==before['manual_updates']
    assert resumed['stats']['elapsed_us']==16667 and resumed['stats']['armed']==1
    publish(32768,0,7,6);r=invoke('short_pause_next','_ZN6Camera8BehaviorEv')
    assert r['stats']['manual_updates']==before['manual_updates']+1
    write('data_0209fc48',1);publish(32768,0,8,106)
    long=invoke('long_pause_behavior','_ZN6Camera8BehaviorEv')
    assert long['queries']==[] and long['stats']['elapsed_us']==0 and long['stats']['armed']==0
    write('data_0209fc48',0);publish(32768,0,9,107);r=invoke('long_pause_resume','_ZN6Camera8BehaviorEv')
    assert r['stats']['elapsed_us']==16666 and r['stats']['armed']==1
    # Equal logical time with one vs two VBlanks per original tick. The body
    # still executes its original updates/queries per consumed tick; controller
    # angular integration does not depend on the number of camera dispatches.
    restore(base);publish(32768,0,3,2);invoke('cadence_acquisition');cadence=snapshot()
    angular=[]
    for step,count,label in [(1,4,'one_vblank'),(2,2,'two_vblank')]:
        restore(cadence);vb=2;elapsed=0
        for i in range(count):
            vb+=step;publish(32768,0,4+i,vb);r=invoke(label+'_'+str(i));elapsed+=r['stats']['elapsed_us']
        angular.append({'cadence':label,'elapsed_us_total':elapsed,'yaw':r['stats']['yaw'],
                        'radius':r['stats']['desired_radius'],'constrained':r['stats']['constrained']})
    assert angular[0]['elapsed_us_total']==angular[1]['elapsed_us_total']==66667
    assert abs(angular[0]['yaw']-angular[1]['yaw'])<=16 and angular[0]['radius']==angular[1]['radius']
    assert all(a['constrained']==0 for a in angular)
    report['logical_clock_cadence']=angular
    # Find interiors in the actual loaded KCL, then certify each with the
    # original Camera query filter and original Lin provider before use.
    from native_camera_world_geometry import triangles
    triangles_actual=triangles(e['kcl'])
    proofs=[];report['spatial_proofs']=proofs
    vec=e['arena']+0x8500;obj=e['arena']+0x8400
    def point(c,n,units):return tuple(round(c[i]+n[i]*units*4096/1024) for i in range(3))
    def ray(label,start,end):
        set_phase(label);cpu.mem_write(obj-16,bytes([0xa5])*(0x78+32))
        cpu.mem_write(vec,struct.pack('<3i',*start));cpu.mem_write(vec+16,struct.pack('<3i',*end))
        call('_ZN9dBgCh_LinC1Ev',obj);call('func_0200897c',camera,obj)
        call('_ZN9dBgCh_Lin13SetObjAndLineERK7Vector3S2_P8dActor_c',obj,vec,vec+16,0)
        answer=call('_ZN9dBgCh_Lin10DetectClsnEv',obj);q=semantics(queries[-1])
        assert read(obj-16,16)==bytes([0xa5])*16 and read(obj+0x78,16)==bytes([0xa5])*16
        call('_ZN9dBgCh_LinD1Ev',obj)
        return answer,q
    def select(kind):
        center=e['resource_position']
        choices=[t for t in triangles_actual if
            (kind=='wall' and abs(t['normal_q10'][1])<10 and abs(t['normal_q10'][0])>1000) or
            (kind=='ceiling' and t['normal_q10'][1]<-1000)]
        choices.sort(key=lambda t:sum((a-b)**2 for a,b in zip(t['center_fixed12'],center)))
        for t in choices[:24]:
            c,n=t['center_fixed12'],t['normal_q10']
            answer,q=ray('certify_'+kind+'_'+str(t['prism']),point(c,n,64),point(c,n,-64))
            proofs.append({**t,'kind':kind,'ray':q,'answer':answer})
            if answer==1:return t
        raise AssertionError('No real '+kind+' prism passed the Camera filter')
    restore(base);wall=select('wall');ceiling=select('ceiling')
    spatial=[];report['spatial_cases']=spatial
    def pose(position,eye,previous):
        cpu.mem_write(player+0x5c,struct.pack('<3i',*position))
        cpu.mem_write(player+0x74,struct.pack('<3i',*position))
        cpu.mem_write(camera+0x98,struct.pack('<3i',*position))
        look=(position[0],position[1]+130*4096,position[2])
        cpu.mem_write(camera+0x80,struct.pack('<3i',*look))
        cpu.mem_write(camera+0xc8,struct.pack('<3i',*look))
        cpu.mem_write(camera+0x8c,struct.pack('<3i',*eye))
        cpu.mem_write(camera+0xd4,struct.pack('<3i',*previous))
        put(camera+0x154,4)
    def spatial_run(label,position,eye,previous,x=1,y=0):
        restore(base);pose(position,eye,previous)
        publish(x,y,3,2);seed=invoke(label+'_acquisition')
        seed_state=snapshot();publish(x,y,4,3);active=invoke(label+'_candidate')
        decision=e['manual_decisions'][-1]
        h=e['height_decisions'][-1] if e['height_decisions'] and e['height_decisions'][-1]['phase']==label+'_candidate' else None
        r={'label':label,'position_fixed12':position,'initial_eye_fixed12':eye,
            'previous_eye_fixed12':previous,'seed':seed,'active':active,'decision':dict(decision),'height':h}
        spatial.append(r)
        return r,seed_state
    wc,wn=wall['center_fixed12'],wall['normal_q10']
    wp=point(wc,wn,180);wp=(wp[0],wc[1]-130*4096,wp[2])
    w,wall_seed=spatial_run('wall',wp,point(wc,wn,-600),point(wc,wn,48))
    wall_active=snapshot();wall_held=[]
    for index in range(20):
        publish(32768,0,5+index,4+index);r=invoke('wall_held_'+str(index));wall_held.append(r)
        assert r['stats']['armed']==1 and r['stats']['ready']==1
        assert r['stats']['generation']==w['active']['stats']['generation']
        assert r['stats']['reseeds']==w['active']['stats']['reseeds'], 'wall caused policy reacquisition'
        assert r['stats']['desired_radius']==w['active']['stats']['desired_radius'], 'wall lost desired radius'
    report['wall_held_sequence']=wall_held
    cc,cn=ceiling['center_fixed12'],ceiling['normal_q10']
    cp=(cc[0],cc[1]-300*4096,cc[2])
    c,ceiling_seed=spatial_run('ceiling',cp,(cc[0]+400*4096,cc[1]+300*4096,cc[2]),point(cc,cn,64),0,-1)
    fp=e['resource_position'];fe=(fp[0]+700*4096,fp[1]-500*4096,fp[2])
    f,floor_seed=spatial_run('floor',fp,fe,(fp[0]+500*4096,fp[1]+100*4096,fp[2]),0,1)
    # Prior-eye sweep: the same orbit seed and requested eye with two genuine
    # previous positions must still reach the original rc2 query endpoints.
    restore(base)
    behind=point(wc,wn,-180);behind=(behind[0],wc[1]-130*4096,behind[2])
    prev_outside=point(wc,wn,400)
    pose(behind,point(wc,wn,-700),prev_outside)
    publish(1,0,3,2)
    # Acquire at the exact controller boundary using the real mode limits.
    # No body/return/query is replaced. This controlled Camera fixture places
    # its previous eye across a wall from target/current eye for rc2 coverage.
    bounds=w['decision']['radius_limits_fixed12']
    result=call('sm64ds_native_camera_prepare',camera,addr('data_0208715c'),0,0,*bounds)
    assert result==0 and stats()['armed']==1
    sweep_seed=snapshot();publish(1,0,4,3)
    sweep=invoke('previous_eye_sweep')
    spatial.append({'label':'previous_eye_sweep','previous_eye_fixed12':prev_outside,'active':sweep})
    # Release the collision by moving the player and retained orbit well away
    # from every castle octree. Queries actually miss; no query is patched.
    restore(sweep_seed);publish(1,0,4,3);blocked=invoke('wall_before_release')
    desired=blocked['stats']['desired_radius']
    delta=(30000*4096,10000*4096,30000*4096)
    for actor,offsets in [(player,[0x5c,0x74]),(camera,[0x80,0x8c,0x98,0xc8,0xd4])]:
        for offset in offsets:
            current=struct.unpack('<3i',read(actor+offset,12))
            cpu.mem_write(actor+offset,struct.pack('<3i',*[a+b for a,b in zip(current,delta)]))
    put(camera+0x154,4)
    publish(1,0,5,4);released=invoke('wall_release')
    spatial.append({'label':'wall_release','desired_radius_before':desired,'active':released})
    # Detailed assertions follow certification: these test actual behavior,
    # rather than treating selected KCL centroids as proof of collision.
    assert w['decision']['accepted']==1 and w['active']['stats']['constrained']>0,'wall candidate not solved'
    # A known-wall solve can rotate while keeping radius; shortening is
    # certified separately by rc2's previous-eye sweep below.
    assert sweep['stats']['solved_radius']<sweep['stats']['desired_radius'], 'previous sweep did not shorten radius'
    assert c['decision']['accepted']==1 and c['active']['stats']['constrained']>0,'ceiling candidate not solved'
    assert f['decision']['accepted']==1 and f['active']['stats']['constrained']>0,'floor candidate not solved'
    assert w['active']['finish_decision']['ownership']==2, 'known-wall original solve missing'
    assert c['active']['finish_decision']['ownership']==2, 'ceiling precheck original priority missing'
    assert f['active']['finish_decision']['ownership']==2, 'floor precheck original priority missing'
    assert any(q['hit'] for q in w['active']['queries'] if q['kind']=='line')
    assert any(q['hit'] for q in c['active']['queries'] if q['kind']=='line')
    assert any(q['hit'] for q in f['active']['queries'] if q['kind']=='ground')
    assert any(q['kind']=='line' and q['start_or_probe_fixed12']==prev_outside and q['hit']==1 for q in sweep['queries']), 'previous-eye sweep removed or not certified by real hit'
    assert released['stats']['desired_radius']==desired,'collision destroyed desired radius'
    assert released['stats']['solved_radius']>blocked['stats']['solved_radius'],'radius failed to recover outside wall'
    assert all(q['hit']==0 for q in released['queries']), 'release point was not outside collision world'
    # Continue lifetime tests from the entrance rather than an artificial pose.
    restore(base);publish(32768,0,3,2);invoke('lifecycle_acquisition')
    publish(32768,0,4,3);invoke('lifecycle_candidate');tick,vblank=4,3
    # A lifecycle callback disarms before original work but keeps consumed time.
    previous=stats();call('_ZN6Camera16OnPendingDestroyEv',camera);after=stats()
    assert after['ready']==0 and after['armed']==0
    assert (after['last_tick'],after['last_vblank'])==(previous['last_tick'],previous['last_vblank'])
    publish(32768,0,tick,vblank);destroyed=invoke('pending_destroy_inactive')
    assert destroyed['stats']['manual_updates']==previous['manual_updates']
    # Original cleanup and original init execute in their entirety. Compare
    # wrapper effects on game RAM against private originals from identical RAM.
    for public,private,label in [('_ZN6Camera16CleanupResourcesEv','sm64ds_original_camera_cleanup','cleanup'),
        ('_ZN6Camera13InitResourcesEv','sm64ds_original_camera_init','reinit')]:
        start=snapshot();expected_result=call(private,camera);expected=game_hash();expected_video=video_hash()
        restore(start);result=call(public,camera);observed=game_hash()
        assert result==expected_result==1 and expected==observed and video_hash()==expected_video,label
        cases.append({'label':label,'lifecycle_game_ram_equal':True,'native_video_and_gx_equal':True,
                      'video':expected_video,'result':result,'stats':stats()})
    assert stats()['generation']==previous['generation']+1 and stats()['ready']==1
    # Same published tick survives reinit but yields no elapsed acquisition.
    r=invoke('same_tick_after_reinit');assert r['stats']['manual_updates']==previous['manual_updates']
    tick+=1;vblank+=1;publish(32768,0,tick,vblank);r=invoke('reinit_acquisition');assert r['stats']['armed']==1
    tick+=1;vblank+=1;publish(32768,0,tick,vblank);r=invoke('reinit_candidate');assert r['stats']['armed']==1
    # UID change means a distinct incarnation even at the same address. It
    # disarms without dereferencing an unrelated object's allocation.
    put(camera+4,u32(camera+4)+1)
    tick+=1;vblank+=1;publish(32768,0,tick,vblank);r=invoke('uid_reuse_inactive');assert r['stats']['armed']==0
    # Direct interval gate does no writes to game RAM and does not invent pose.
    before=game_hash();result=call('sm64ds_native_camera_height_range',camera,100,99)
    assert result==0 and game_hash()==before and stats()['armed']==0
    cases.append({'label':'inverted_interval','result':result,'game_ram_unchanged':True,'stats':stats()})
    # Isolated exact height blocks close the inverted interval branch that
    # did not occur naturally in the castle. These are explicit block inputs;
    # no Lin/Gnd query is replaced or reported as a synthetic successful hit.
    ranges=[('inverted_goal_below',500,510,520,480,420,300),
            ('inverted_goal_above',500,510,520,480,420,900),
            ('equal_interval',500,510,520,None,None,300),
            ('ordinary_floor',250,260,254,1000,1100,100),
            ('ordinary_ceiling',250,260,254,1000,1100,1500)]
    height_cases=[];report['isolated_height_cases']=height_cases
    out=e['arena']+0x9000
    for label,sb,r6,sp04,ceil,probe,eye in ranges:
        restore(base);put(camera+0x90,eye*4096,'<i')
        # The original floor increment is 0x25bc8 fixed12, not an integer
        # 37 units. Exercise equality with the exact original fixed value.
        ceiling_fx=sb*4096+0x25bc8 if ceil is None else ceil*4096
        probe_fx=ceiling_fx if probe is None else probe*4096
        input_values=[sb*4096,r6*4096,sp04*4096,700*4096,ceiling_fx,-2147483648,probe_fx,0,0,0,0,0,0,0]
        cpu.mem_write(out-16,bytes([0xa5])*(56+32));cpu.mem_write(out,struct.pack('<14i',*input_values))
        publish(1,0,3,2)
        result=call('sm64ds_native_camera_prepare',camera,addr('data_0208715c'),0,0,*w['decision']['radius_limits_fixed12'])
        assert result==0 and stats()['armed']==1
        expected_controls=e['controls']()
        seeded=snapshot();call('native_camera_height_golden',camera,out)
        expected=read(camera,0x1a8);goal_golden=struct.unpack('<14i',read(out,56));expected_player=read(player,0x768)
        restore(seeded);call('native_camera_height_candidate',camera,out)
        actual=read(camera,0x1a8);goal_candidate=struct.unpack('<14i',read(out,56))
        guard_inverted=goal_candidate[10]>goal_candidate[11]
        assert read(out-16,16)==bytes([0xa5])*16 and read(out+56,16)==bytes([0xa5])*16,label
        assert read(player,0x768)==expected_player and e['controls']()==expected_controls,label
        if guard_inverted:
            assert actual==expected and goal_candidate[9:12]==goal_golden[9:12],label
            assert goal_candidate[12:]==(0,0) and stats()['armed']==0,label
        else:
            assert goal_candidate[12:]==(1,1),label
            solved=struct.unpack('<i',read(camera+0x90,4))[0]
            assert goal_candidate[10]<=solved<=goal_candidate[11],label
        height_cases.append({'label':label,'inverted':guard_inverted,'original_goal_and_bounds':goal_golden[9:12],
            'candidate_goal_and_bounds':goal_candidate[9:12], 'camera_full_bytes_match_original':actual==expected,
            'original_eye_y_fixed12':struct.unpack_from('<i',expected,0x90)[0],
            'candidate_eye_y_fixed12':struct.unpack_from('<i',actual,0x90)[0],
            'disarmed':stats()['armed']==0,'explicit_block_inputs_not_collisions':True,'canaries':True})
    assert [c['inverted'] for c in height_cases]==[True,True,False,False,False]
    report['manual_summary']={'status':'pass','case_count':len(cases),
        'inactive_full_body_pairs':sum('inactive_game_ram_equal' in c for c in cases),
        'lifecycle_full_original_pairs':2,'real_collision_queries':sum(len(c.get('queries',[])) for c in cases),
        'helper_gates_distinguished':True,'isolated_exact_height_block_cases':len(height_cases),
        'remaining_collision_scenarios':False}
    report['status']='manual_completed'
