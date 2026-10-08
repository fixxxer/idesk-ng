/* 
 * Idesk -- Install.h
 *
 * Copyright (c) 2026, iDesk-NG contributors
 * Some rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *      
 *      Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *      
 *      Neither the name of the <ORGANIZATION> nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * (See the included file COPYING / BSD )
 */

#ifndef INSTALL_CLASS
#define INSTALL_CLASS

/*
 * idesk-ng --install-ideskrc and --install-trash-icon: one-shot setup
 * commands, same family as Migrate.h's --migrate-to-desktop. Both are
 * explicit, never run automatically, and both skip (not overwrite)
 * when their target file already exists. Composable with each other
 * and with --migrate-to-desktop in a single invocation -- see
 * App::processArguments().
 */

// Writes ~/.config/idesktop/ideskrc with idesk-ng's own factory
// defaults, by reusing the exact same Database(path, true) mechanism
// normal startup already falls back to when no ideskrc exists --
// rather than duplicating that content a second time, this just
// persists what that path would have synthesized in memory anyway.
// Skips (prints a message, returns true -- not an error) if a
// ideskrc already exists at that path.
// Moves the legacy ~/.idesktop/ to ~/.config/idesktop/ (or $XDG_CONFIG_HOME)
// and ~/.ideskrc into it, so there is one place for everything. Never
// deletes: entries that would overwrite something are left where they are,
// and a ~/.ideskrc that can't take its place is set aside as ~/.ideskrc.bak.
// Messages go to stderr. Called at startup, except in kiosk mode.
void migrateLegacyConfig();

bool installIdeskrc();

// Adds a Trash icon (trash.desktop) to ~/.config/idesktop/: opens
// ~/.local/share/Trash/files with whatever the session's default file
// manager is via xdg-open, falling back to a zenity error message
// (always in English, see DESIGN.md) naming a few file managers to
// install if none is configured -- xdg-open's own failure mode on a
// minimal system is to silently do nothing. Icon=user-trash (the
// standard freedesktop.org icon name; resolved the same
// theme-discovery way as every other icon). The icon's own Name is
// localized to the session's language (LC_ALL, falling back to LANG)
// from a bounded table of common desktop languages -- not every
// language that exists, English for anything not in the table. Also
// sets X-Idesk-Protected=true (parsed by FreeDesktopIcon, not yet
// enforced anywhere -- the future Delete context-menu action is
// expected to check it and refuse). Skips if trash.desktop already
// exists.
bool installTrashIcon();

#endif
