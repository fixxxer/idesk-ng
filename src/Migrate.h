/* 
 * Idesk -- Migrate.h
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

#ifndef MIGRATE_CLASS
#define MIGRATE_CLASS

/*
 * idesk-ng --migrate-to-desktop (Path A, see DESIGN.md "Position
 * handling"): converts every .lnk in ~/.config/idesktop/ to a .desktop
 * file in the same directory, seeding the layout DB with the .lnk's
 * X/Y in the same pass. The original .lnk is renamed to <name>.lnk.bak
 * rather than deleted. Always explicit, never run automatically --
 * existing .lnk-only installs keep working untouched unless the user
 * runs this themselves.
 *
 * Field mapping: Caption->Name, ToolTip.Caption->Comment,
 * Command->Exec, Icon->Icon, Width/Height->X-Idesk-Width/Height (the
 * same vendor extension keys FreeDesktopIcon already reads). X/Y are
 * NOT written into the .desktop -- they go straight to the layout DB
 * instead (see IconLayout.h for why: a .desktop can be a copy of a
 * real system launcher, not safe to rewrite on every drag).
 *
 * Returns true if every .lnk found was migrated or validly skipped
 * (e.g. a same-named .desktop already exists), false if any real error
 * occurred. Prints a one-line summary per file plus a final count to
 * stderr either way.
 */
bool runMigration();

#endif
