pragma Singleton

import QtQuick

// How many bar buttons the pointer is on (one at most; briefly two while it
// moves from one to the next). The window can be dragged by its title bar
// only when none: a button there must get the click.
QtObject {
    property int hovered: 0
}
