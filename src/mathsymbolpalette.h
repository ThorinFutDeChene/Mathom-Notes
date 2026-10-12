#ifndef MATHSYMBOLPALETTE_H
#define MATHSYMBOLPALETTE_H

#include "basket_export.h"

class FocusedTextEdit;
class QMenu;
class QWidget;

class BASKET_EXPORT MathSymbolPalette
{
public:
    // The palette is modeless and stays open for repeated insertions.
    static void open(QWidget *parent, FocusedTextEdit *target);
    static void populateMenu(QMenu *menu, FocusedTextEdit *target);
};

#endif
