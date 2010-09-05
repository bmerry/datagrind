
/*
   ----------------------------------------------------------------

   Notice that the following BSD-style license applies to this one
   file (datagrind.h) only.  The rest of Valgrind is licensed under the
   terms of the GNU General Public License, version 2, unless
   otherwise indicated.  See the COPYING file in the source
   distribution for details.

   ----------------------------------------------------------------

   This file is part of DataGrind, a heavyweight Valgrind tool for
   tracking data accesses. It is based on callgrind.h from Valgrind 3.5.0.

   Copyright (C) 2010 Bruce Merry.  All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

   2. The origin of this software must not be misrepresented; you must 
      not claim that you wrote the original software.  If you use this 
      software in a product, an acknowledgment in the product 
      documentation would be appreciated but is not required.

   3. Altered source versions must be plainly marked as such, and must
      not be misrepresented as being the original software.

   4. The name of the author may not be used to endorse or promote 
      products derived from this software without specific prior written 
      permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
   OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   ----------------------------------------------------------------

   Notice that the above BSD-style license applies to this one file
   (datagrind.h) only.  The entire rest of Valgrind is licensed under
   the terms of the GNU General Public License, version 2.  See the
   COPYING file in the source distribution for details.

   ---------------------------------------------------------------- 
*/

#ifndef __DATAGRIND_H
#define __DATAGRIND_H

#include "valgrind.h"

typedef enum
{
   VG_USERREQ__TRACK_RANGE = VG_USERREQ_TOOL_BASE('D', 'G'),
   VG_USERREQ__UNTRACK_RANGE,
   VG_USERREQ__START_EVENT,
   VG_USERREQ__END_EVENT,

   _VG_USERREQ__DATAGRIND_RECORD_OVERLAP_ERROR = VG_USERREQ_TOOL_BASE('D', 'G') + 256
} Vg_DataGrindClientRequest;

/* Specify that an address range contains a structure of a specific type, with
 * a human-readable label. _qzz_type should be the typename that appears in
 * DWARF information in the binary.
 */
#define DATAGRIND_TRACK_RANGE(_qzz_addr, _qzz_len, _qzz_type, _qzz_label) \
   (__extension__({unsigned long _qzz_res;                                \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* default return */,          \
                               VG_USERREQ__TRACK_RANGE,                   \
                               _qzz_addr, _qzz_len, _qzz_type, _qzz_label, 0);  \
    _qzz_res;                                                             \
   }))

/* Cease tracking a range previously registered by DATAGRIND_TRACK_RANGE. The
 * address and length must match exactly.
 */
#define DATAGRIND_UNTRACK_RANGE(_qzz_addr, _qzz_len) \
   (__extension__({unsigned long _qzz_res;                                \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* default return */,          \
                               VG_USERREQ__UNTRACK_RANGE,                 \
                               _qzz_addr, _qzz_len, 0, 0, 0);             \
    _qzz_res;                                                             \
   }))

/* Mark the start of an event. */
#define DATAGRIND_START_EVENT(_qzz_label) \
   (__extension__({unsigned long _qzz_res;                                \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* default return */,          \
                               VG_USERREQ__START_EVENT,                   \
                               _qzz_label, 0, 0, 0, 0);                   \
    _qzz_res;                                                             \
   }))

/* Mark the end of an event */
#define DATAGRIND_END_EVENT(_qzz_label) \
   (__extension__({unsigned long _qzz_res;                                \
    VALGRIND_DO_CLIENT_REQUEST(_qzz_res, 0 /* default return */,          \
                               VG_USERREQ__END_EVENT,                     \
                               _qzz_label, 0, 0, 0, 0);                   \
    _qzz_res;                                                             \
   }))

#endif /* !__DATAGRIND_H */
