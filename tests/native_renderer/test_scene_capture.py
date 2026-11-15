"""Independent JSON checks against an authored native component snapshot."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "component.json"
    subprocess.run([sys.argv[1], str(path)], check=True)
    capture = json.loads(path.read_text(), parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
    skin_capture = json.loads(Path(str(path)+'.skin.json').read_text())
assert capture["format"] == "SM64_NATIVE_COMPONENT_SCENE_5"
assert capture["original_scene_accepted"] is False and capture["physical_capture"] is False
assert capture["scope"] == "native-component"
assert (capture['scene_screen'], capture['scene_order']) == (1, 1408)
image, = capture['image_layers']
assert (image['bank'], image['image'], image['screen'], image['order']) == (4096, 3, 1, 3707)
assert image['rectangle'] == [0, 160, 320, 20] and image['material']['depth'] == [0, 0]
assert image['source'] == 0 and image['part'] == 0
assert image['tone_target']==16
assert (capture["frame"], capture["scene"], capture["serial"], capture["legacy_words"]) == (121, 7, 121, 9)
assert capture["clear_known"] and capture["unmigrated_2d"]
assert capture["clear"] == [0x0a123400, 63, 1]
assert capture["occurrences"] == [[17, 0, 0, 1, 0], [1, 1, 0, 4, 0]]
part, = capture["parts"]
assert part["matrix"] == [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
assert part["visible"] and part["image"] == 2 and part["lighting"] == 0
assert part["material"]["fixed_uv"]["coefficient"][1] == -17
assert part["material"]["fixed_uv"]["offset"] == [-32768, 0]
quad, = capture["quad_states"]
assert (quad["image"], quad["polygon_id"], quad["fog"], quad["bank"]) == (4, 35, True, 1)
assert quad["material"]["blend"] == 1 and quad["material"]["tint"] == 0xfe123457
assert capture["quad_vertices"][-1] == [-1, 2, 0, 0, 1, 13, 14, 15, 16]
lights, = capture["light_sets"]
assert lights["generation"] == 2**64 - 1 and lights["known"] == 1 and lights["table_known"]
assert lights["lights"][0] == {"direction": [-1024, 511, 0], "colour": [31, 12, 0], "specular_reciprocal": 262144}
assert lights["shininess"] == list(reversed(range(128)))
state, = capture["lighting_states"]
assert state["normal"] == [4096, 0, -3, 0, 4096, 0, 0, 0, 4096]
assert state["material"] == [32767, 0, 1, 0, 1, 1, 0, 0] and state["lights"] == 0
assert capture['skin_vertices'] == [] and part['highlight'] is False
assert lights['toon_known'] and lights['toon'] == [i | (31-i)<<5 | (i*7%32)<<10 for i in range(32)]
assert skin_capture['occurrences'] == [[17,0,0,1,0],[17,2,0,3,0],[2**32-1,3,3,3,1]]
assert skin_capture['skin_vertices'][5] == [5,-2,3,.25,.75,17,23,31,255,-512,511,-1,2]
assert skin_capture['parts'][1]['highlight'] and skin_capture['parts'][1]['material']['blend'] == 3
assert skin_capture['lighting_states'][0]['material'] == [32767,0,0,0,1,0,1,0]
assert skin_capture['image_layers'][1]['source']==2 and skin_capture['image_layers'][1]['tone_target']==32
print("PASS: complete semantic snapshot, preserved unsupported policies, malformed data produces zero bytes, IO failure reported")
