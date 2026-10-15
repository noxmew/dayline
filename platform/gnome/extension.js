import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const bridgeInterface = `<node>
    <interface name="org.dayline.DesktopBridge">
        <method name="GetVersion"><arg type="u" direction="out"/></method>
    </interface>
</node>`;

// DING on Wayland uses an ordinary transparent window for the desktop icons.
// Its input surface covers X11 desktop windows even when they are visible.
export default class DaylineDesktopBridge extends Extension {
    enable() {
        this._pending = 0;
        this._signals = [];
        for (const [object, signal] of [
            [global.display, 'restacked'],
            [global.window_manager, 'map'],
            [global.window_manager, 'destroy'],
        ]) {
            this._signals.push([object, object.connect_after(signal, () => this._queueStack())]);
        }
        this._dbus = Gio.DBusExportedObject.wrapJSObject(bridgeInterface, {GetVersion: () => 1});
        this._dbus.export(Gio.DBus.session, '/org/dayline/DesktopBridge');
        this._busId = Gio.bus_own_name_on_connection(Gio.DBus.session, 'org.dayline.DesktopBridge',
            Gio.BusNameOwnerFlags.NONE, null, null);
        this._queueStack();
    }

    disable() {
        for (const [object, id] of this._signals)
            object.disconnect(id);
        this._signals = [];
        if (this._pending) {
            GLib.source_remove(this._pending);
            this._pending = 0;
        }
        this._dbus.unexport();
        this._dbus = null;
        Gio.bus_unown_name(this._busId);
        this._busId = 0;
    }

    _queueStack() {
        if (this._pending)
            return;
        this._pending = GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {
            this._pending = 0;
            this._stackCalendar();
            return GLib.SOURCE_REMOVE;
        });
    }

    _stackCalendar() {
        const group = global.window_group;
        // Read the actual group: DING deliberately hides its actors from
        // global.get_window_actors() on Wayland.
        const actors = group.get_children();
        const calendars = actors.filter(actor => {
            const window = actor.get_meta_window?.();
            return window?.get_wm_class() === 'Dayline' && window.get_role() === 'dayline-desktop';
        });
        if (!calendars.length)
            return;
        const desktops = actors.filter(actor => {
            const window = actor.get_meta_window?.();
            return window && !calendars.includes(actor) &&
                (window.get_window_type() === Meta.WindowType.DESKTOP ||
                 window.get_title()?.startsWith('Desktop Icons '));
        });
        let anchor = desktops.at(-1);
        if (!anchor)
            return;
        for (const calendar of calendars) {
            const children = group.get_children();
            if (children.indexOf(calendar) !== children.indexOf(anchor) + 1)
                group.set_child_above_sibling(calendar, anchor);
            anchor = calendar;
        }
    }
}
