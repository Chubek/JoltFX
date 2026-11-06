#include "jolttype/jolttype.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv) {
    using namespace jolttype;
    Diagnostic error;
    Document doc;
    const char *source = "\\jolttype{1}\n% preserved\n\\page{210mm}{297mm}\n"
        "\\text{title}{20mm}{20mm}{100mm}{18pt}{#123456}{Hello \\{world\\}!}\n"
        "\\rect{rule}{20mm}{40mm}{100mm}{2pt}{#abcdef}\n\\page{612}{792}\n";
    assert(compile(source, doc, error));
    assert(doc.pages.size() == 2 && doc.elements.size() == 2);
    assert(std::abs(doc.pages[0].width - 595.2756) < .001);
    assert(doc.elements[0].text == "Hello {world}!");
    assert(!doc.elements[0].lines.empty());
    auto original = doc.elements[0].x;
    assert(!compile("\\jolttype{1}\n\\page{nan}{2}", doc, error));
    assert(error.line == 2 && error.column > 0 && doc.elements[0].x == original);
    const char *invalid[] = {
        "\\jolttype{2}", "\\jolttype{1}\\page{0}{20}",
        "\\jolttype{1}\\page{20}{20}\\unknown{}",
        "\\jolttype{1}\\page{20}{20}\\text{x}{0}{0}{20}{12}{#ffffff}{unclosed",
        "\\jolttype{1}\\page{20}{20}\\rect{x}{0}{0}{2}{2}{#ff0000}\\rect{x}{0}{0}{2}{2}{#ffffff}",
        "\\jolttype{1}\\page{20}{20}\\rect{x}{0}{0}{21}{2}{#ffffff}",
        "\\jolttype{1}\\page{200}{200}\\text{x}{0}{0}{100}{12}{#ffffff}{\\input{file}}",
        "\\jolttype{1}\\page{20}{20} trailing", "\\jolttype{1}\\page{20}{20}\xff"
    };
    for (const auto *bad : invalid) assert(!compile(bad, doc, error));
    tilly::string nul(source); nul.push_back('\0');
    assert(!compile(nul, doc, error));
    assert(!compile(tilly::string(max_source_bytes + 1, 'x'), doc, error));
    Document wrapped;
    assert(compile("\\jolttype{1}\\page{200}{200}\\text{a}{0}{0}{30}{10}{#000000}{one two abcdef\r\néééééé}", wrapped, error));
    const auto &lines = wrapped.elements[0].lines;
    assert(lines.size() == 6 && lines[0].text == "one" && lines[1].text == "two");
    assert(lines[2].text == "abcde" && lines[3].text == "f" && lines[4].text == "ééééé" && lines[5].text == "é");
    assert(wrapped.elements[0].height == 72);
    assert(compile("\\jolttype{1}\\page{100}{100}\\text{a}{0}{0}{30}{10}{#000000}{a\n}", wrapped, error));
    assert(wrapped.elements[0].lines.size() == 2 && wrapped.elements[0].height == 24);
    tilly::string many_pages = "\\jolttype{1}";
    for (size_t i = 0; i < max_pages; ++i) many_pages += "\\page{10}{10}";
    assert(compile(many_pages, wrapped, error));
    assert(!compile(many_pages + "\\page{10}{10}", wrapped, error));
    assert(wrapped.pages.size() == max_pages);

    Session session;
    assert(session.stage(source, 0));
    assert(session.dirty() && session.committed().pages.empty());
    assert(!session.tick(59, 60, false));
    assert(session.tick(60, 60, false));
    assert(!session.dirty());
    assert(session.move("title", 60, 60, 61));
    assert(session.dirty() && session.committed().elements[0].x == original);
    assert(session.draft().find("% preserved") != tilly::string::npos);
    assert(!session.tick(200, 60, true));
    assert(session.tick(201, 60, false));
    assert(session.committed().elements[0].x == 60);
    assert(session.history(false));
    assert(session.committed().elements[0].x == original);
    assert(session.history(true));
    assert(session.set_text("title", "a & <b> \\ {c}\nnext", 202));
    assert(session.commit(203));
    tilly::string svg;
    assert(export_svg(session.committed(), 0, svg, error));
    assert(svg.find("&amp;") != tilly::string::npos && svg.find("&lt;b&gt;") != tilly::string::npos);
    assert(session.stage("\\broken", 204));
    auto committed = session.source();
    assert(!session.commit(205) && session.source() == committed && session.dirty());
    assert(!session.move("title", 50, 50, 206));
    assert(!session.history(false));
    assert(!session.tick(1000, 0, false));
    assert(session.revert() && !session.dirty());
    assert(!session.move("title", -1, 5, 1001));
    assert(!session.dirty());
    assert(session.save("jolttype-test.jtype"));
    Session reopened;
    assert(reopened.load("jolttype-test.jtype", 0));
    assert(reopened.source() == session.source());
    std::remove("jolttype-test.jtype");
    assert(!reopened.load("missing-jolttype-file.jtype", 0));
    assert(reopened.source() == session.source());
    // A failed load, including a valid prefix followed by a NUL, is atomic.
    assert(write_file("jolttype-test.jtype", nul, error));
    assert(!reopened.load("jolttype-test.jtype", 0));
    assert(reopened.source() == session.source());
    std::remove("jolttype-test.jtype");
    for (int i = 0; i < 40; ++i) {
        assert(reopened.move("title", 60 + i, 60, i));
        assert(reopened.commit(i));
    }
    int undos = 0; while (reopened.history(false)) ++undos;
    assert(undos == 32 && reopened.can_history(true));
    assert(reopened.set_text("title", "A new branch", 100));
    assert(reopened.commit(100) && !reopened.can_history(true));
    if (argc == 2) { Session example; assert(example.load(argv[1], 0)); assert(example.committed().pages.size() == 2); }
    std::puts("JoltType compiler and commit tests passed");
}
