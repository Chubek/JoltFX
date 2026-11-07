#include "jolttype/project.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv) {
    using namespace jolttype;
    Diagnostic error;
    Document doc;
    const char *pagelets=R"(\jolttype{1}
\pagelet{Title}{
  \component{content}{Untitled}
  \config{ink}{#182840}
  \text{label}{0}{0}{200}{18}{@ink}{@content}
}
\kliche{RedTitle}{Title}{\config{ink}{#ff0000}}
\page{400}{500}
\place{heading}{RedTitle}{20}{30}{\component{content}{My title}}
\place{other}{Title}{20}{100}{\component{content}{Independent}}
\inline{badge}{10}{200}{\component{caption}{Inline}\text{label}{0}{0}{100}{12}{#000000}{@caption}}
)";
    assert(compile(pagelets,doc,error));
    assert(doc.pagelets.size()==2 && doc.entities.size()==3 && doc.elements.size()==3);
    assert(doc.elements[0].text=="My title" && doc.elements[0].color==0xff0000);
    assert(doc.elements[1].text=="Independent" && doc.elements[1].color==0x182840);
    assert(doc.elements[2].text=="Inline" && doc.elements[2].entity=="badge");
    Session live;assert(live.stage(pagelets,0) && live.commit(0));
    auto published=live.source();
    assert(live.set_component("heading","content","Escaped {text} \\ value",1));
    assert(live.preview().elements[0].text=="Escaped {text} \\ value");
    assert(live.move("heading",40,50,2));
    assert(live.preview().elements[0].x==40 && live.preview().elements[0].y==50);
    assert(live.source()==published && live.dirty());
    assert(!live.set_text("heading-label","Bad span",2));
    assert(live.commit(3) && live.history(false) && live.source()==published);
    assert(live.stage("\\jolttype{1}\\page{300}{300}\\rect{live}{1}{2}{3}{4}{#123456}",4));
    assert(live.refresh() && live.dirty() && live.source()==published);
    assert(live.preview().elements[0].id=="live");
    assert(live.stage("\\broken",5) && !live.refresh());
    assert(live.preview().elements[0].id=="live" && live.source()==published);
    const char *bad_pagelets[]={
        "\\kliche{Bad}{Title}{\\component{content}{Frozen contents}}",
        "\\kliche{Bad}{Title}{\\config{content}{Frozen contents}}",
        "\\kliche{Bad}{Missing}{}",
        "\\pagelet{Title}{}",
        "\\place{bad}{Title}{0}{0}{\\component{unknown}{x}}",
        "\\place{heading}{Title}{0}{0}{}",
        "\\place{bad}{Title}{399}{0}{}",
        "\\inline{bad}{0}{0}{\\text{x}{0}{0}{100}{12}{#000000}{@missing}}"
    };
    for(auto bad:bad_pagelets){auto input=tilly::string(pagelets)+bad;assert(!compile(input,doc,error));assert(doc.entities.size()==3);}
    const char *bad_definitions[]={
        "\\pagelet{Unused}{\\unknown{}}", "\\pagelet{Empty}{}",
        "\\pagelet{Recursive}{\\place{again}{Recursive}{0}{0}{}}",
        "\\pagelet{Bad}{\\component{x}{a}\\component{x}{b}\\rect{r}{0}{0}{1}{1}{#000000}}",
        "\\pagelet{Bad}{\\text{r}{0}{0}{100}{12}{#000000}{@absent}}"
    };
    for(auto bad:bad_definitions){assert(!compile(tilly::string(pagelets)+bad,doc,error));assert(error.message[0]);}
    assert(live.revert());
    assert(live.set_component("badge","caption","Changed inline",6));
    assert(live.preview().elements.back().text=="Changed inline");
    assert(live.draft().find("\\component{content}{My title}")!=live.draft().npos);
    const char *commented="\\jolttype{1}\\page{300}{300}\\inline{item}{0}{0}{\n% keep declaration comment\n\\component{content}{old}\n% keep body comment\n\\text{label}{0}{0}{200}{12}{#000000}{@content}}";
    Session comments;assert(comments.stage(commented,0) && comments.commit(0));
    assert(comments.set_component("item","content","new",1));
    auto expected=tilly::string(commented);expected.replace(expected.find("{old}"),5,"{new}");
    assert(comments.draft()==expected);
    const char *nested="\\jolttype{1}\\pagelet{Inner}{\\component{content}{Child}\\text{label}{1}{2}{100}{12}{#000000}{@content}}"
        "\\pagelet{Outer}{\\component{content}{Outer data}\\place{child}{Inner}{3}{4}{\\component{content}{@content}}}"
        "\\page{300}{300}\\place{root}{Outer}{10}{20}{}";
    Document tree;assert(compile(nested,tree,error));
    assert(tree.elements[0].text=="Outer data" && tree.elements[0].x==14 && tree.elements[0].y==26);
    assert(tree.elements[0].entity=="root");
    const char *scoped="\\jolttype{1}\\pagelet{Outer}{\n% ignored {@missing} and unmatched {\n"
        "\\inline{child}{3}{4}{\n% nested comment with }\n\\component{content}{Local}\n"
        "\\text{label}{1}{2}{100}{12}{#000000}{@content}}}\\page{300}{300}\\place{root}{Outer}{10}{20}{}";
    assert(compile(scoped,tree,error) && tree.elements[0].text=="Local");
    tilly::string repeated="\\jolttype{1}\\pagelet{Copy}{\\component{content}{"+tilly::string(3000,'x')+
        "}\\text{label}{0}{0}{2000}{4}{#000000}{@content}}\\page{4000}{4000}";
    for(int i=0;i<30;++i){char instance[96];std::snprintf(instance,sizeof(instance),"\\place{item%d}{Copy}{0}{%d}{}",i,i*30);repeated+=instance;}
    assert(compile(repeated,tree,error));tilly::string object;
    assert(compile_object(tree,object,error));Document decoded;
    assert(load_object(object,decoded,error) && decoded.elements.size()==30 && decoded.elements.back().text.size()==3000);
    auto retained=object;assert(!export_source(tree,object,error) && object==retained);
    Inventory assets;Asset module;module.id="titles";module.path="titles.jtype";module.kind=AssetKind::Pagelet;
    module.bytes="\\jolttype{1}\\pagelet{Title}{\\component{content}{Imported}\\text{label}{0}{0}{100}{12}{#000000}{@content}}";
    assert(assets.put(module,error));
    assert(compile("\\jolttype{1}\\use{titles}\\page{300}{300}\\place{imported}{Title}{0}{0}{}",doc,error,&assets));
    assert(doc.elements[0].text=="Imported");
    module.bytes="\\jolttype{1}\\use{titles}";assert(assets.put(module,error));
    assert(!compile("\\jolttype{1}\\use{titles}\\page{300}{300}",doc,error,&assets));
    tilly::string frozen;assert(compile_object(doc,frozen,error));Document thawed;
    assert(load_object(frozen,thawed,error) && thawed.elements[0].text=="Imported");
    assert(export_source(doc,frozen,error) && compile(frozen,thawed,error,&assets));
    assert(thawed.entities.empty() && thawed.elements[0].text=="Imported");
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
