
/*--------------------------------------------------------------------*/
/*--- Datagrind: Track data accesses.               dg_main.c ---*/
/*--------------------------------------------------------------------*/

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

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_vki.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_options.h"
#include "pub_tool_machine.h"

typedef enum
{
   EventType_Dr,
   EventType_Dw
} EventType;

typedef struct
{
   EventType type;
   IRExpr *addr;
   Int size;
} Event;

#define NEVENTS 4
#define OUT_BUF_SIZE 4096

static Event events[NEVENTS];
static Int nevents = 0;
static Int out_fd = -1;
static Char out_buf[OUT_BUF_SIZE];
static UInt out_buf_used = 0;

static Char *clo_datagrind_out_file = "datagrind.out";

static Bool dg_process_cmd_line_option(Char *arg)
{
   if (VG_STR_CLO(arg, "--datagrind-out-file", clo_datagrind_out_file)) {}
   else
       return False;
   return True;
}

static void dg_print_usage(void)
{
   VG_(printf)(
"    --datagrind-out-file=<file>      output file name [datagrind.out]\n"
   );
}

static void dg_print_debug_usage(void)
{
   VG_(printf)(
"    (none)\n"
   );
}

static void prepare_out_file(void)
{
   if (out_fd == -1)
   {
      SysRes sres;
      Char *filename = VG_(expand_file_name)("--datagrind-out-file", clo_datagrind_out_file);
      sres = VG_(open)(filename, VKI_O_CREAT | VKI_O_TRUNC | VKI_O_WRONLY,
                       VKI_S_IRUSR | VKI_S_IWUSR);
      if (sr_isError(sres))
      {
         VG_(message)(Vg_UserMsg,
                      "Error: can not open datagrind output file `%s'\n",
                      filename);
         VG_(exit)(1);
      }
      else
      {
         out_fd = (Int) sr_Res(sres);
      }
   }
}

static void dg_post_clo_init(void)
{
}

static void out_flush(void)
{
   VG_(write)(out_fd, out_buf, out_buf_used);
   out_buf_used = 0;
}

static void out_record(const void *buf, Int count)
{
   if (count > OUT_BUF_SIZE - out_buf_used)
      out_flush();
   VG_(memcpy)(out_buf + out_buf_used, buf, count);
   out_buf_used += count;
}

static VG_REGPARM(2) void trace_Dr(Addr addr, SizeT size)
{
   Addr meta = (size << 2) | 1;
   out_record(&meta, sizeof(Addr));
   out_record(&addr, sizeof(Addr));
   // VG_(printf)("Dr %08lx,%lu\n", addr, size);
}

static VG_REGPARM(2) void trace_Dw(Addr addr, SizeT size)
{
   Addr meta = (size << 2) | 2;
   out_record(&meta, sizeof(Addr));
   out_record(&addr, sizeof(Addr));
   // VG_(printf)("Dw %08lx,%lu\n", addr, size);
}

static void flushEvents(IRSB *sb)
{
   Int i;
   static const struct
   {
      const Char *name;
      void *fn;
   } helpers[2] =
   {
      { "trace_Dr", &trace_Dr },
      { "trace_Dw", &trace_Dw }
   };

   for (i = 0; i < nevents; i++)
   {
      Event *ev = &events[i];
      IRExpr **argv;
      IRDirty *di;

      argv = mkIRExprVec_2(ev->addr, mkIRExpr_HWord(ev->size));
      di = unsafeIRDirty_0_N(2, (Char *) helpers[ev->type].name,
                             VG_(fnptr_to_fnentry)(helpers[ev->type].fn),
                             argv);
      addStmtToIRSB(sb, IRStmt_Dirty(di));
   }
   nevents = 0;
}

static void addEvent(IRSB *sb, IRExpr *addr, Int size, EventType type)
{
   Event *ev;
   if (nevents == NEVENTS)
      flushEvents(sb);
   ev = &events[nevents];
   ev->type = type;
   ev->addr = addr;
   ev->size = size;
   nevents++;
}

static void addEvent_Dr(IRSB *sb, IRExpr *daddr, Int dsize)
{
   addEvent(sb, daddr, dsize, EventType_Dr);
}

static void addEvent_Dw(IRSB *sb, IRExpr *daddr, Int dsize)
{
   addEvent(sb, daddr, dsize, EventType_Dw);
}

static IRSB* dg_instrument(VgCallbackClosure* closure,
                           IRSB* sbIn,
                           VexGuestLayout* layout,
                           VexGuestExtents* vge,
                           IRType gWordTy, IRType hWordTy)
{
   IRSB* sbOut;
   Int i;

   if (gWordTy != hWordTy)
   {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   prepare_out_file();

   sbOut = deepCopyIRSBExceptStmts(sbIn);

   /* Copy preamble */
   for (i = 0; i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark; i++)
   {
      addStmtToIRSB(sbOut, sbIn->stmts[i]);
   }

   for (; i < sbIn->stmts_used; i++)
   {
      IRStmt *st = sbIn->stmts[i];
      if (!st || st->tag == Ist_NoOp) continue;

      switch (st->tag)
      {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
         case Ist_IMark:
         case Ist_Exit:
            break;
         case Ist_WrTmp:
            {
               IRExpr* data = st->Ist.WrTmp.data;
               if (data->tag == Iex_Load)
               {
                  addEvent_Dr(sbOut, data->Iex.Load.addr, sizeofIRType(data->Iex.Load.ty));
               }
            }
            break;
         case Ist_Store:
            {
               IRExpr* data = st->Ist.Store.data;
               addEvent_Dw(sbOut, st->Ist.Store.addr,
                           sizeofIRType(typeOfIRExpr(sbOut->tyenv, data)));
            }
            break;
         case Ist_Dirty:
            {
               IRDirty* d = st->Ist.Dirty.details;
               if (d->mFx != Ifx_None)
               {
                  tl_assert(d->mAddr != NULL);
                  tl_assert(d->mSize != 0);
                  if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify)
                     addEvent_Dr(sbOut, d->mAddr, d->mSize);
                  if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify)
                     addEvent_Dw(sbOut, d->mAddr, d->mSize);
               }
            }
            break;
         case Ist_CAS:
            {
               Int dataSize;
               IRCAS *cas = st->Ist.CAS.details;
               tl_assert(cas->addr != NULL);
               tl_assert(cas->dataLo != NULL);
               dataSize = sizeofIRType(typeOfIRExpr(sbOut->tyenv, cas->dataLo));
               if (cas->dataHi != NULL)
                  dataSize *= 2;
               addEvent_Dr(sbOut, cas->addr, dataSize);
               addEvent_Dw(sbOut, cas->addr, dataSize);
            }
            break;
         default:
            tl_assert(0);
      }
      addStmtToIRSB(sbOut, st);
   }

   flushEvents(sbOut);

   return sbOut;
}

static void dg_fini(Int exitcode)
{
   if (out_fd != -1)
   {
      out_flush();
      VG_(close)(out_fd);
   }
}

static void dg_pre_clo_init(void)
{
   VG_(details_name)            ("Datagrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("tracks data accesses");
   VG_(details_copyright_author)(
      "Copyright (C) 2010, and GNU GPL'd, by Bruce Merry.");
   VG_(details_bug_reports_to)  ("bmerry@users.sourceforge.net");

   VG_(basic_tool_funcs)        (dg_post_clo_init,
                                 dg_instrument,
                                 dg_fini);

   VG_(needs_command_line_options)(dg_process_cmd_line_option,
                                   dg_print_usage,
                                   dg_print_debug_usage);
}

VG_DETERMINE_INTERFACE_VERSION(dg_pre_clo_init)
