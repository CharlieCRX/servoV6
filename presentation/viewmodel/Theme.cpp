#include "Theme.h"

Theme& Theme::instance() {
    static Theme theme;
    return theme;
}
