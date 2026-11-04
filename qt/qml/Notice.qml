// Messages at the bottom of the window with an icon or an action
// ("Saved to your pieces · Show"): Notice.post(text, error, { icon, actionLabel, action }).
// The toast in Main shows them, as it does the screens' notify(text, error).
pragma Singleton
import QtQuick

QtObject {
    signal posted(string text, bool error, var options)
    function post(text, error, options) { posted(text, !!error, options || ({})) }
}
