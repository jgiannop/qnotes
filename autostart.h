#pragma once

// "Start QNotes when I log in": ~/.config/autostart/qnotes.desktop on Linux, the HKCU Run key on Windows.
namespace Autostart {

bool isSupported();
bool isEnabled();
bool setEnabled(bool enabled);

} // namespace Autostart
