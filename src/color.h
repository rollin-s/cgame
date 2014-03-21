#ifndef COLOR_H
#define COLOR_H

#include "scalar.h"
#include "script_export.h"
#include "saveload.h"

SCRIPT(color,

       typedef struct Color Color;
       struct Color { Scalar r, g, b, a; };

       EXPORT extern Color color_black;
       EXPORT extern Color color_white;
       EXPORT extern Color color_red;
       EXPORT extern Color color_green;
       EXPORT extern Color color_blue;

       EXPORT void color_save(Color *c, Serializer *s);
       EXPORT void color_load(Color *c, Deserializer *s);

    )

#define color(r, g, b, a) ((Color) { (r), (g), (b), (a) })

#endif
