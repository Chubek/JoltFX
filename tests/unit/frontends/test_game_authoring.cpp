#include "../../../frontends/desktop/src/game_authoring.h"
#include <cassert>
#include <cstring>
#include <limits>

int main() {
    using namespace game_authoring;
    Entries entries{};
    scan("// fn cliche_fake() {}\n /* fn room_fake() {} */\n"
         "fn main() { let s = \"fn cliche_string() {}\"; }\n"
         "fn cliche_Player() -> i64 { return 0; }\n"
         "fn cliche_arg(x: i64) {} fn room_main() {}", entries);
    assert(entries.count == 3);
    assert(!std::strcmp(entries.names[1], "cliche_Player"));
    assert(!std::strcmp(entries.names[2], "room_main"));
    assert(identifier("Player_2") && !identifier("2Player") && !identifier("Player();"));
    char source[4096] = "fn main() { return 0; }\n";
    Draft draft{}; defaults(draft);
    assert(append_cliche(source, sizeof(source), draft));
    assert(std::strstr(source, "fn cliche_Player()"));
    assert(std::strstr(source, "engine_spawn_offset(0)"));
    char before[4096]; std::strcpy(before, source);
    assert(!append_cliche(source, sizeof(source), draft)); // duplicate, unchanged
    assert(!std::strcmp(source, before));
    std::strcpy(draft.name, "Enemy");
    assert(!append_cliche(source, std::strlen(source) + 8, draft));
    assert(!std::strcmp(source, before));
    draft.transform[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!append_cliche(source, sizeof(source), draft));
    assert(!std::strcmp(source, before));
    defaults(draft); draft.primitive = 17;
    assert(!append_cliche(source, sizeof(source), draft));
    defaults(draft); draft.color[1] = 2;
    assert(!append_cliche(source, sizeof(source), draft));
    defaults(draft); draft.transform[6] = 0;
    assert(!append_cliche(source, sizeof(source), draft));
    defaults(draft); std::strcpy(draft.name, "Variant");
    assert(append_cliche(source, sizeof(source), draft, "cliche_Player"));
    assert(std::strstr(source, "let object = cliche_Player();"));
    assert(!append_cliche(source, sizeof(source), draft, "missing"));
    scan(starter, entries);
    assert(entries.count >= 4);
    return 0;
}
