#pragma once

#include <QObject>

// commenting: line and block comments toggled with a language's markers.
class TestCommenting : public QObject
{
    Q_OBJECT
private slots:
    void commentsLinesAtTheLeastIndentation();
    void uncommentsWhenAllAre();
    void putsTheMarkerAtColumnZeroWhenAsked();
    void wrapsLinesWithoutALineMarker();
    void togglesABlock();
    void leavesTextWithoutMarkers();
};
