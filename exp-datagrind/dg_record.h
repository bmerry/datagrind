/*
   This file is part of Datagrind, a tool for tracking data accesses.
   It borrows heavily from the Lackey tool.

   Copyright (C) 2010 Bruce Merry
      bmerry@users.sourceforge.net

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#ifndef __DG_RECORD_H
#define __DG_RECORD_H

#define DG_R_HEADER           0
#define DG_R_READ             1
#define DG_R_WRITE            2
#define DG_R_TRACK_RANGE      3
#define DG_R_UNTRACK_RANGE    4
#define DG_R_START_EVENT      5
#define DG_R_END_EVENT        6
#define DG_R_INSTR            7
#define DG_R_TEXT_AVMA        8

#endif /* __DG_RECORD_H */
