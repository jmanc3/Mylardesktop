#pragma once

struct Container;

struct Event {
    float x;
    float y;

    int   button;
    int   state;

    Event(float x, float y, int button, int state) : x(x), y(y), button(button), state(state) {
       ; 
    }
    
    Event(float x, float y) : x(x), y(y) {
       ; 
    }

    Event () { 
        ;
    }
};

void move_event(Container*, const Event&);
void mouse_event(Container*, const Event&);

void paint_root(Container*);
