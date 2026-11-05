#ifndef JFX_DESKTOP_WORKSPACE_HISTORY_H
#define JFX_DESKTOP_WORKSPACE_HISTORY_H
#include <array>
#include <cstring>

namespace jfx_history {
template<size_t N> struct Source {
    char text[N]{};
    bool operator==(const Source &other) const { return std::strcmp(text,other.text)==0; }
};
struct Sketch {
    Source<8192> source;
    int width=320,height=180;
    bool operator==(const Sketch &other) const {
        return source==other.source && width==other.width && height==other.height;
    }
};
// Allocated once through Tilly with the frontend. Fixed storage means history
// cannot fail after a widget mutates its bounded source buffer. Only authored
// source/canvas state is recorded, not playback, preview inputs or VM execution.
template<class State> struct History {
    State current{};
    std::array<State,32> past{},future{};
    size_t undo_count=0,redo_count=0;
    bool changed(const State &state) const { return !(state==current); }
    static void push(std::array<State,32> &stack,size_t &count,const State &state) noexcept {
        if(count==stack.size()){
            for(size_t i=1;i<count;++i)stack[i-1]=stack[i];
            --count;
        }
        stack[count++]=state;
    }
    void record(const State &state) noexcept {
        if(!changed(state))return;
        push(past,undo_count,current);current=state;redo_count=0;
    }
    bool step(bool redo) noexcept {
        if(redo){if(!redo_count)return false;push(past,undo_count,current);current=future[--redo_count];}
        else {if(!undo_count)return false;push(future,redo_count,current);current=past[--undo_count];}
        return true;
    }
};
using SketchHistory=History<Sketch>;
using GameHistory=History<Source<16384>>;
}
#endif
