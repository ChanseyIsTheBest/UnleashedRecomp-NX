"""add_dump_hooks.py (run in the 0.0.3 translator's folder by build-003-translator.sh): adds to its main.cpp what the
current translator has for audits: XENOS_RECOMP_DUMP_DIR (each shader's HLSL and decoded SPIR-V, then exit without a
cache) and XENOS_RECOMP_ONLY=<hash>,<hash>... (translate only those)."""
import pathlib

p = pathlib.Path('XenosRecomp/main.cpp')
src = p.read_text(encoding='utf-8')
nl = '\r\n' if '\r\n' in src else '\n'
src = src.replace('\r\n', '\n')


def sub(old, new):
    global src
    assert src.count(old) == 1, old
    src = src.replace(old, new)


sub('#include "dxc_compiler.h"\n', '#include "dxc_compiler.h"\n#include <set>\n#include <string>\n')

sub('        std::atomic<uint32_t> progress = 0;\n',
    '''        std::atomic<uint32_t> progress = 0;

        const char* dumpDir = getenv("XENOS_RECOMP_DUMP_DIR");
        if (dumpDir != nullptr)
            std::filesystem::create_directories(dumpDir);
        std::set<XXH64_hash_t> only;
        if (const char* list = getenv("XENOS_RECOMP_ONLY"))
        {
            std::string s(list);
            size_t start = 0;
            while (start < s.size())
            {
                size_t comma = s.find(',', start);
                if (comma == std::string::npos)
                    comma = s.size();
                only.insert(std::stoull(s.substr(start, comma - start), nullptr, 16));
                start = comma + 1;
            }
        }
''')

sub('''                auto& shader = hashShaderPair.second;

                thread_local ShaderRecompiler recompiler;
                recompiler = {};
                recompiler.recompile(shader.data, include);
''', '''                auto& shader = hashShaderPair.second;
                if (!only.empty() && only.count(hashShaderPair.first) == 0)
                    return;

                thread_local ShaderRecompiler recompiler;
                recompiler = {};
                recompiler.recompile(shader.data, include);

                if (dumpDir != nullptr)
                {
                    const std::string path = fmt::format("{}/{:016X}.{}.hlsl", dumpDir, hashShaderPair.first, recompiler.isPixelShader ? "ps" : "vs");
                    writeAllBytes(path.c_str(), recompiler.out.data(), recompiler.out.size());
                }
''')

sub('''                bool result = smolv::Encode(spirv->GetBufferPointer(), spirv->GetBufferSize(), shader.spirv, smolv::kEncodeFlagStripDebugInfo);
                assert(result);
''', '''                bool result = smolv::Encode(spirv->GetBufferPointer(), spirv->GetBufferSize(), shader.spirv, smolv::kEncodeFlagStripDebugInfo);
                assert(result);

                if (dumpDir != nullptr)
                {
                    std::vector<uint8_t> decoded(smolv::GetDecodedBufferSize(shader.spirv.data(), shader.spirv.size()));
                    if (smolv::Decode(shader.spirv.data(), shader.spirv.size(), decoded.data(), decoded.size()))
                    {
                        const std::string path = fmt::format("{}/{:016X}.{}.spv", dumpDir, hashShaderPair.first, recompiler.isPixelShader ? "ps" : "vs");
                        writeAllBytes(path.c_str(), decoded.data(), decoded.size());
                    }
                }
''')

sub('''            });

        fmt::println("Creating shader cache...");
''', '''            });

        if (dumpDir != nullptr)
            return 0;

        fmt::println("Creating shader cache...");
''')

p.write_text(src.replace('\n', nl), encoding='utf-8', newline='')
print('main.cpp: dump + filter added')
