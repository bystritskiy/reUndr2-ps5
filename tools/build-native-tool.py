#!/usr/bin/env python3
"""Build pinned native writer with AudioOut2's separate import-library record.

The payload stub aggregates AudioOut2 symbols in the AudioOut module, while
firmware exports them through library libSceAudioOut2 in that same module.
Keep the pinned dependency intact; apply the narrow writer fix in build/.
"""
from pathlib import Path
import shutil,subprocess
ROOT=Path(__file__).resolve().parent.parent
src=ROOT/'build/deps/PS5_Vulkan/tooling/native'
out=ROOT/'build/native-tool-source';out.mkdir(exist_ok=True)
for p in src.iterdir():
    if p.is_file():shutil.copy2(p,out/p.name)
p=out/'sce_module_writer.cpp';s=p.read_text()
a='''    for (Import &import : imports)
    {
        const auto module ='''
b='''    int audio2_library_id = -1;
    for (const Import &import : imports) {
        if (import.provider->module_name == "libSceAudioOut" &&
            import.plain.starts_with("sceAudioOut2")) {
            if (audio2_library_id < 0) {
                const auto m = std::find(module_order.begin(), module_order.end(), import.provider);
                audio2_library_id = static_cast<int>(libraries.size());
                libraries.push_back({audio2_library_id,
                    static_cast<int>(m - module_order.begin()) + 1,
                    strings.add("libSceAudioOut2")});
            }
        }
    }
    for (Import &import : imports)
    {
        const auto module ='''
assert s.count(a)==1;s=s.replace(a,b)
a='''        import.library_id = index;
'''
b='''        import.library_id = import.provider->module_name == "libSceAudioOut" &&
                            import.plain.starts_with("sceAudioOut2") ? audio2_library_id : index;
''';assert s.count(a)==1;s=s.replace(a,b)
a='''                                            import.provider->library_name + "#" +'''
b='''                                            (import.provider->module_name == "libSceAudioOut" &&
                                             import.plain.starts_with("sceAudioOut2") ?
                                             "libSceAudioOut2" : import.provider->library_name) + "#" +'''
assert s.count(a)==1;s=s.replace(a,b);p.write_text(s)
subprocess.run(['/opt/homebrew/opt/llvm@18/bin/clang++','-std=c++20','-O2',
    *[str(out/f) for f in ['native_app_builder.cpp','self_container.cpp','elf_object.cpp','sce_module_writer.cpp']],
    '-lz','-o',str(ROOT/'build/ps5-native-tool')],check=True)
