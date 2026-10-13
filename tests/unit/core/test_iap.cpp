#include "jfx/jfx_iap.h"
#include "jfx/jfx_modeling3d.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>

int main() {
    // Null validation leaves outputs untouched.
    uint32_t fps = 123;
    assert(jfx_iap_info(nullptr, &fps, nullptr, nullptr, nullptr, nullptr) != JFX_SUCCESS);
    assert(fps == 123);
    assert(jfx_iap_validate(nullptr) != JFX_SUCCESS);
    assert(jfx_iap_export(nullptr, nullptr, 0, "x.iap") != JFX_SUCCESS);

    auto *s = jfx_scene3d_create();
    assert(s);
    assert(jfx_scene3d_command(s, "3d.add", 0, 0, 0, 0, "cube") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.key", 0, 4, 0, 0, "") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.key", 0, 4, 90, 360, "") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.cloner", 0, 2, 6, 3, "") == JFX_SUCCESS);

    // Missing asset fails without creating output.
    const char *missing[1] = {"does-not-exist-asset.bin"};
    assert(jfx_iap_export(s, missing, 1, "iap-test-missing.iap") != JFX_SUCCESS);
    std::ifstream probe("iap-test-missing.iap", std::ios::binary);
    assert(!probe.good());

    // Empty asset table exports and validates.
    assert(jfx_iap_export(s, nullptr, 0, "iap-test.iap") == JFX_SUCCESS);
    assert(jfx_iap_validate("iap-test.iap") == JFX_SUCCESS);
    uint32_t ofps = 0, oframes = 0, oobjs = 0, oassets = 0;
    uint64_t bytes = 0;
    assert(jfx_iap_info("iap-test.iap", &ofps, &oframes, &oobjs, &oassets, &bytes) == JFX_SUCCESS);
    assert(ofps == jfx_scene3d_fps(s) && oframes == jfx_scene3d_frames(s));
    assert(oobjs == 1 && oassets == 0 && bytes > 20);

    // Corrupt the CRC: flip the last byte, validation must fail.
    {
        std::ifstream in("iap-test.iap", std::ios::binary);
        std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(blob.size() > 20);
        blob.back() ^= 0xFF;
        std::ofstream out("iap-test-bad.iap", std::ios::binary);
        out.write(blob.data(), (std::streamsize)blob.size());
    }
    assert(jfx_iap_validate("iap-test-bad.iap") != JFX_SUCCESS);

    // Asset packing: one small file is embedded and reported.
    {
        std::ofstream asset("iap-test-asset.bin", std::ios::binary);
        asset.write("0123456789ABCDEF", 16);
    }
    const char *assets[1] = {"iap-test-asset.bin"};
    assert(jfx_iap_export(s, assets, 1, "iap-test-asset.iap") == JFX_SUCCESS);
    assert(jfx_iap_validate("iap-test-asset.iap") == JFX_SUCCESS);
    assert(jfx_iap_info("iap-test-asset.iap", nullptr, nullptr, nullptr, &oassets, nullptr) == JFX_SUCCESS);
    assert(oassets == 1);

    // Second object with its own keys/cloner: counts must reflect both.
    assert(jfx_scene3d_command(s, "3d.add", 0, 0, 0, 0, "plane") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.key", 1, 1, 0, 0, "") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.key", 1, 1, 45, 2, "") == JFX_SUCCESS);
    assert(jfx_scene3d_command(s, "3d.key", 1, 1, 90, 0, "") == JFX_SUCCESS);
    assert(jfx_iap_export(s, assets, 1, "iap-test-two.iap") == JFX_SUCCESS);
    assert(jfx_iap_validate("iap-test-two.iap") == JFX_SUCCESS);
    uint32_t two_objects = 0;
    assert(jfx_iap_info("iap-test-two.iap", nullptr, nullptr, &two_objects, &oassets, nullptr) == JFX_SUCCESS);
    assert(two_objects == 2 && oassets == 1);

    // Player import: fresh scene with matching primitives replays the pack.
    assert(jfx_iap_import(nullptr,"iap-test-two.iap") != JFX_SUCCESS);
    auto *play = jfx_scene3d_create(); assert(play);
    assert(jfx_iap_import(play,nullptr) != JFX_SUCCESS);
    assert(jfx_scene3d_command(play,"3d.add",0,0,0,0,"cube") == JFX_SUCCESS);
    assert(jfx_scene3d_command(play,"3d.add",0,0,0,0,"plane") == JFX_SUCCESS);
    float before[9]; assert(jfx_scene3d_sample(play,0,1.5f,before) == JFX_SUCCESS);
    assert(jfx_iap_import(play,"iap-test-two.iap") == JFX_SUCCESS);
    assert(jfx_scene3d_fps(play) == jfx_scene3d_fps(s) && jfx_scene3d_frames(play) == jfx_scene3d_frames(s));
    for (uint32_t o = 0; o < 2; ++o) {
        jfx_procedural3d_info_t want={}, got={}; want.size = got.size = sizeof(want);
        assert(jfx_scene3d_procedural_info(s,o,&want) == JFX_SUCCESS);
        assert(jfx_scene3d_procedural_info(play,o,&got) == JFX_SUCCESS);
        assert(want.cloner_mode == got.cloner_mode && want.instances == got.instances && want.spacing == got.spacing);
        for (double t : {0.0, 0.5, 1.5, 3.0}) {
            float a[9], b[9];
            assert(jfx_scene3d_sample(s,o,t,a) == JFX_SUCCESS);
            assert(jfx_scene3d_sample(play,o,t,b) == JFX_SUCCESS);
            for (int ch = 0; ch < 9; ++ch) assert(a[ch] == b[ch]);
        }
    }
    // Mismatched and corrupt packs fail without touching the scene.
    auto *solo = jfx_scene3d_create(); assert(solo);
    assert(jfx_scene3d_command(solo,"3d.add",0,0,0,0,"cube") == JFX_SUCCESS);
    float solo_before[9]; assert(jfx_scene3d_sample(solo,0,1.5f,solo_before) == JFX_SUCCESS);
    assert(jfx_iap_import(solo,"iap-test-two.iap") != JFX_SUCCESS);
    assert(jfx_iap_import(solo,"iap-test-bad.iap") != JFX_SUCCESS);
    assert(jfx_iap_import(solo,"does-not-exist.iap") != JFX_SUCCESS);
    float solo_after[9]; assert(jfx_scene3d_sample(solo,0,1.5f,solo_after) == JFX_SUCCESS);
    for (int ch = 0; ch < 9; ++ch) assert(solo_before[ch] == solo_after[ch]);
    // Only the "3d.add" step is on the stack: one undo empties the scene,
    // proving the failed imports pushed nothing.
    assert(jfx_scene3d_command(solo,"undo",0,0,0,0,"") == JFX_SUCCESS);
    assert(jfx_scene3d_object_count(solo) == 0);
    // Import is one undoable step.
    assert(jfx_scene3d_command(play,"undo",0,0,0,0,"") == JFX_SUCCESS);
    jfx_object3d_info_t info={}; info.size=sizeof(info);
    assert(jfx_scene3d_object_info(play,0,&info) == JFX_SUCCESS && info.keys == 0);
    assert(jfx_scene3d_command(play,"redo",0,0,0,0,"") == JFX_SUCCESS);
    assert(jfx_scene3d_object_info(play,0,&info) == JFX_SUCCESS && info.keys == 2);
    jfx_scene3d_destroy(solo); jfx_scene3d_destroy(play);

    jfx_scene3d_destroy(s);
    std::remove("iap-test.iap");
    std::remove("iap-test-bad.iap");
    std::remove("iap-test-asset.iap");
    std::remove("iap-test-two.iap");
    std::remove("iap-test-asset.bin");
    std::remove("iap-test-missing.iap.iap-part-0");
    std::printf("iap ok\n");
    return 0;
}
