#include "windowplacement.h"

#include <QScreen>

void WindowPlacement::center(QWindow *window, QWindow *origin)
{
    if (!window || !origin || !origin->screen()) {
        qWarning("Cannot center a window without its originating screen.");
        return;
    }

    QScreen *screen = origin->screen();
    window->setScreen(screen);
    const QRect area = screen->availableGeometry();
    window->setPosition(area.x() + (area.width() - window->width()) / 2,
                        area.y() + (area.height() - window->height()) / 2);
}
