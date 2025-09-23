//
// Created by jmanc3 on 6/14/20.
//

#ifndef SCROLL_COMPONENTS_H
#define SCROLL_COMPONENTS_H

#include <stack>
#include "container.h"
#include "hypriso.h"

ScrollContainer *
make_newscrollpane_as_child(Container *parent, const ScrollPaneSettings &settings);

#endif// SCROLL_COMPONENTS_H
