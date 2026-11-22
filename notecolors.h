#pragma once

#include <QColor>
#include <QList>
#include <QString>

// The seven note colours used by Windows Sticky Notes.
struct NoteColor {
    QString name;
    QColor title; // title bar
    QColor body;  // note background
    QColor text;  // text colour
};

inline const QList<NoteColor> &noteColors()
{
    static const QList<NoteColor> colors = {
        {"Yellow",   QColor("#FFF2AB"), QColor("#FFF7D1"), QColor("#202020")},
        {"Green",    QColor("#CBF1C4"), QColor("#E4F9E0"), QColor("#202020")},
        {"Pink",     QColor("#FFCCE5"), QColor("#FFE4F1"), QColor("#202020")},
        {"Purple",   QColor("#E7CFFF"), QColor("#F2E6FF"), QColor("#202020")},
        {"Blue",     QColor("#CDE9FF"), QColor("#E2F1FF"), QColor("#202020")},
        {"Gray",     QColor("#E0E0E0"), QColor("#F3F2F1"), QColor("#202020")},
        {"Charcoal", QColor("#333333"), QColor("#454545"), QColor("#F5F5F5")},
    };
    return colors;
}

inline const NoteColor &noteColor(int index)
{
    const auto &c = noteColors();
    return c.at(qBound(0, index, int(c.size()) - 1));
}
