#pragma once

// Light / dark appearance of the notes list window. "System" follows the desktop setting.
namespace Theme {

enum class Mode { System, Light, Dark };

Mode mode();               // saved preference
void setMode(Mode mode);
bool systemPrefersDark();  // desktop setting (GNOME color-scheme, Windows "apps" mode, KDE palette)
bool isDark();             // what to use now

} // namespace Theme
